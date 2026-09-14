import Foundation
import Darwin
import ConfigRewrite
import Diagnostics
import OperationGuard
import ConfigSources

enum Operation: String, CaseIterable { case configure, official, cleanup }
struct AppError: LocalizedError {
    let message: String
    var errorDescription: String? { message }
}
struct OperationOutcome {
    let message: String
    let warning: Bool
}

// NSError codes distinguish permission, file occupation and I/O failures without
// serializing userInfo, which can contain file contents or credentials.
private func operationErrorDescription(_ error: Error) -> String {
    if let error = error as? AppError { return error.message }
    let value = error as NSError
    var detail = "\(value.localizedDescription) [\(value.domain):\(value.code)]"
    if let underlying = value.userInfo[NSUnderlyingErrorKey] as? NSError {
        detail += " [\(underlying.domain):\(underlying.code)]"
    }
    return detail
}

private final class DiagnosticLog {
    private var context: OpaquePointer?
    private var lastMainStage = "准备操作"

    init(root: URL, operation: Operation, key: String) {
        context = root.path.withCString { home in
            operation.rawValue.withCString { action in
                key.withCString { yilai_diagnostic_begin(home, action, $0) }
            }
        }
    }

    func event(_ stage: String, _ message: String) {
        switch stage {
        case "acquire_lock": lastMainStage = "检查配置目录占用"
        case "checking_apps": lastMainStage = "检查后台程序"
        case "prepare_sources": lastMainStage = "识别配置来源"
        case "apply_sources": lastMainStage = "处理连接覆盖配置"
        case "verify_sources": lastMainStage = "核验实际生效连接"
        case "prepare_config": lastMainStage = "准备连接配置"
        case "write_config": lastMainStage = "写入配置"
        case "delete_auth": lastMainStage = "删除旧登录文件"
        case "rename_config": lastMainStage = "停用旧配置"
        default: break
        }
        stage.withCString { name in
            message.withCString { yilai_diagnostic_event(context, name, $0) }
        }
    }

    func failureMessage(_ message: String) -> String {
        "\(lastMainStage)失败：\(sanitized(message))"
    }

    func sanitized(_ message: String) -> String {
        let output = message.withCString { yilai_diagnostic_sanitize(context, $0) }
        guard let output else { return "操作失败。请检查配置和应用状态后重试。" }
        defer { yilai_config_free(output) }
        return String(cString: output)
    }

    @discardableResult
    func finish(success: Bool, message: String) -> Bool {
        let saved = message.withCString { yilai_diagnostic_finish(context, success ? 1 : 0, $0) } != 0
        context = nil
        return saved
    }
}

final class PlatformService {
    let root: URL
    private let files = FileManager.default

    init(root: URL? = nil) {
        let env = ProcessInfo.processInfo.environment["CODEX_HOME"].flatMap {
            $0.isEmpty ? nil : URL(fileURLWithPath: $0, isDirectory: true)
        }
        self.root = root ?? env ?? FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent(".codex", isDirectory: true)
    }

    private var config: URL { root.appendingPathComponent("config.toml") }
    private var auth: URL { root.appendingPathComponent("auth.json") }
    func mode() -> String {
        guard files.fileExists(atPath: config.path) else { return "尚未配置" }
        guard let text = try? read(config) else { return "配置需要检查" }
        switch text.withCString({ yilai_config_mode($0) }) {
        case 0: return "OpenAI 官方"
        case 1: return "易来 API"
        case 2: return "其他 CCS / 第三方连接"
        default: return "配置需要检查"
        }
    }

    private func closed() throws {
        let process = Process()
        process.executableURL = URL(fileURLWithPath: "/usr/bin/pgrep")
        process.arguments = ["-x", "(Codex|codex|cc-switch|CC Switch)"]
        process.standardOutput = Pipe()
        process.standardError = Pipe()
        try process.run()
        process.waitUntilExit()
        guard process.terminationStatus == 0 || process.terminationStatus == 1 else {
            throw AppError(message: "无法检查后台程序（pgrep 退出码 \(process.terminationStatus)），未做修改。")
        }
        guard process.terminationStatus == 1 else {
            throw AppError(message: "请完全退出 Codex 和 CC-Switch 后再操作；关闭窗口后也请检查后台进程。")
        }
    }

    private func regular(_ url: URL) throws {
        // Check symbolic links before existence: a dangling link is still unsafe.
        if (try? files.destinationOfSymbolicLink(atPath: url.path)) != nil {
            throw AppError(message: "不能修改符号链接：\(url.path)")
        }
        guard files.fileExists(atPath: url.path) else { return }
        let info = try url.resourceValues(forKeys: [.isRegularFileKey, .isSymbolicLinkKey])
        guard info.isRegularFile == true, info.isSymbolicLink != true else {
            throw AppError(message: "预期为普通文件：\(url.path)")
        }
    }

    private func snapshot(_ url: URL) throws -> Data? {
        try regular(url)
        return files.fileExists(atPath: url.path) ? try Data(contentsOf: url) : nil
    }

    private func read(_ url: URL) throws -> String {
        try regular(url)
        return try String(contentsOf: url, encoding: .utf8)
    }

    private func desktopMode() -> String? {
        let state = root.appendingPathComponent(".codex-global-state.json")
        guard let data = try? Data(contentsOf: state),
              data.count <= 16 * 1024 * 1024,
              let object = (try? JSONSerialization.jsonObject(with: data)) as? [String: Any],
              let atom = object["electron-persisted-atom-state"] as? [String: Any],
              let modes = atom["agent-mode-by-host-id"] as? [String: Any]
        else { return nil }
        return modes["local"] as? String
    }

    private func preservingDesktopMode(_ text: String) throws -> String {
        guard let mode = desktopMode() else { return text }
        var failure: UnsafeMutablePointer<CChar>?
        let output = text.withCString { existing in
            mode.withCString { yilai_apply_desktop_mode(existing, $0, &failure) }
        }
        defer {
            if let output { yilai_config_free(output) }
            if let failure { yilai_config_free(failure) }
        }
        guard let output else {
            throw AppError(message: failure.map { String(cString: $0) } ?? "无法保留 Codex 默认权限设置")
        }
        return String(cString: output)
    }

    private func write(_ data: Data, _ url: URL) throws {
        try regular(url)
        try files.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
        try data.write(to: url, options: .atomic)
    }

    private func restore(_ data: Data?, _ url: URL) throws {
        if let data { try write(data, url) }
        else if files.fileExists(atPath: url.path) { try regular(url); try files.removeItem(at: url) }
    }

    // link(2) publishes a complete private file only if the destination is absent.
    // A new auth file created after the caller's snapshot must never be replaced.
    fileprivate func restoreAuthWithoutReplacing(_ data: Data) throws {
        let temporary = root.appendingPathComponent(".yilai-auth-restore-\(UUID().uuidString)")
        let descriptor = temporary.path.withCString { Darwin.open($0, O_WRONLY | O_CREAT | O_EXCL, mode_t(0o600)) }
        guard descriptor >= 0 else { throw NSError(domain: NSPOSIXErrorDomain, code: Int(errno)) }
        defer {
            _ = Darwin.close(descriptor)
            _ = temporary.path.withCString { Darwin.unlink($0) }
        }
        try data.withUnsafeBytes { bytes in
            guard let base = bytes.baseAddress else { return }
            var offset = 0
            while offset < bytes.count {
                let count = Darwin.write(descriptor, base.advanced(by: offset), bytes.count - offset)
                if count < 0 {
                    if errno == EINTR { continue }
                    throw NSError(domain: NSPOSIXErrorDomain, code: Int(errno))
                }
                guard count > 0 else { throw NSError(domain: NSPOSIXErrorDomain, code: Int(EIO)) }
                offset += count
            }
        }
        guard Darwin.fsync(descriptor) == 0 else { throw NSError(domain: NSPOSIXErrorDomain, code: Int(errno)) }
        let linked = temporary.path.withCString { source in
            auth.path.withCString { Darwin.link(source, $0) }
        }
        guard linked == 0 else { throw NSError(domain: NSPOSIXErrorDomain, code: Int(errno)) }
    }

    func run(_ operation: Operation, key: String = "", requireClosed: Bool = true, runtimeOverride: String = "") throws -> OperationOutcome {
        let log = DiagnosticLog(root: root, operation: operation, key: key.trimmingCharacters(in: .whitespacesAndNewlines))
        do {
            log.event("acquire_lock", "Acquiring exclusive operation lock for this Codex home")
            var lockError: UnsafeMutablePointer<CChar>?
            let lock = root.path.withCString { yilai_operation_lock($0, &lockError) }
            defer { if let lockError { yilai_config_free(lockError) } }
            guard let lock else {
                throw AppError(message: lockError.map { String(cString: $0) } ?? "另一个配置器正在操作此目录，请稍后重试。")
            }
            defer { yilai_operation_unlock(lock) }
            let result = try runOperation(operation, key: key, requireClosed: requireClosed, runtimeOverride: runtimeOverride, log: log)
            _ = log.finish(success: true, message: "Operation completed")
            return result
        } catch {
            var message = log.failureMessage(operationErrorDescription(error))
            log.event("failure", message)
            _ = log.finish(success: false, message: message)
            throw AppError(message: message)
        }
    }

    private func runOperation(_ operation: Operation, key: String, requireClosed: Bool, runtimeOverride: String, log: DiagnosticLog) throws -> OperationOutcome {
        log.event("checking_apps", requireClosed ? "Checking Codex and CC-Switch processes" : "Synthetic-home test: process check skipped")
        if requireClosed { try closed() }
        if operation == .official {
            log.event("prepare_config", "Validating official configuration update")
            let beforeConfig = try snapshot(config)
            let before: String
            if let beforeConfig {
                guard let text = String(data: beforeConfig, encoding: .utf8) else {
                    throw AppError(message: "配置不是有效 UTF-8，未修改")
                }
                before = text
            } else { before = "" }
            guard !before.utf8.contains(0) else {
                throw AppError(message: "配置包含无效空字符，未做修改。")
            }
            let prepared = try preservingDesktopMode(before)
            var failure: UnsafeMutablePointer<CChar>?
            let output = prepared.withCString { yilai_configure_official($0, &failure) }
            defer { if let output { yilai_config_free(output) } }
            defer { if let failure { yilai_config_free(failure) } }
            guard let output else { throw AppError(message: failure.map { String(cString: $0) } ?? "官方配置失败") }
            let after = Data(String(cString: output).utf8)
            guard try snapshot(config) == beforeConfig else {
                throw AppError(message: "配置已被其他程序改动，请关闭后重试。")
            }
            log.event("write_config", "Writing official configuration atomically")
            try write(after, config)
            return OperationOutcome(message: "已切换到官方。历史对话保持不变，请重新打开 Codex。", warning: false)
        }
        if operation == .cleanup {
            log.event("prepare_config", "Checking config before reset")
            guard let before = try snapshot(config) else {
                return OperationOutcome(message: "尚无配置，无需重置。", warning: false)
            }
            let disabled = root.appendingPathComponent("config.toml.disabled-\(UUID().uuidString)")
            guard try snapshot(config) == before else {
                throw AppError(message: "配置已被其他程序改动，请关闭后重试。")
            }
            log.event("rename_config", "Renaming config to a unique disabled file")
            try files.moveItem(at: config, to: disabled)
            return OperationOutcome(message: "配置已重置，旧文件已保留为 \(disabled.lastPathComponent)。请重新选择连接。", warning: false)
        }

        log.event("prepare_config", "Validating configuration update")
        if key.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
            throw AppError(message: "请先填写易来 API Key，再点击“切换到易来 API”。")
        }
        log.event("prepare_config", "Validating root configuration update")
        let beforeConfig = try snapshot(config)
        let before: String
        if let beforeConfig {
            guard let text = String(data: beforeConfig, encoding: .utf8) else {
                throw AppError(message: "配置文件不是有效 UTF-8，未做修改。")
            }
            before = text
        } else { before = "" }
        let token = key.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !before.utf8.contains(0), !token.utf8.contains(0) else {
            throw AppError(message: "配置或 Key 包含无效空字符。")
        }
        let prepared = try preservingDesktopMode(before)
        var failure: UnsafeMutablePointer<CChar>?
        let output = prepared.withCString { text in
            token.withCString { yilai_configure_api(text, $0, &failure) }
        }
        defer {
            if let output { yilai_config_free(output) }
            if let failure { yilai_config_free(failure) }
        }
        guard let output else {
            throw AppError(message: failure.map { String(cString: $0) } ?? "配置更新失败")
        }
        let catalogURL = root.appendingPathComponent("yilai-model-catalog.json")
        let beforeCatalog = try snapshot(catalogURL)
        let catalogData = Data(String(cString: yilai_model_catalog()).utf8)
        var catalogError: UnsafeMutablePointer<CChar>?
        let withCatalog = catalogURL.path.withCString { yilai_configure_catalog(output, $0, &catalogError) }
        defer {
            if let withCatalog { yilai_config_free(withCatalog) }
            if let catalogError { yilai_config_free(catalogError) }
        }
        guard let withCatalog else { throw AppError(message: "模型目录配置失败") }
        let after = Data(String(cString: withCatalog).utf8)
        let beforeAuth = try snapshot(auth)
        guard try snapshot(config) == beforeConfig else {
            throw AppError(message: "配置已被其他程序改动，请关闭后重试。")
        }
        var catalogChanged = false
        var configChanged = false
        var authRemoved = false
        do {
            log.event("write_catalog", "Writing the three managed models")
            guard try snapshot(catalogURL) == beforeCatalog else { throw AppError(message: "模型目录已被其他程序改动") }
            try write(catalogData, catalogURL)
            catalogChanged = true
            log.event("write_config", "Writing configuration atomically")
            try write(after, config)
            configChanged = true
            log.event("delete_auth", "Removing only auth.json when present")
            guard try snapshot(auth) == beforeAuth else {
                throw AppError(message: "登录文件已被其他程序改动，请关闭后重试。")
            }
            if beforeAuth != nil {
                try files.removeItem(at: auth)
                authRemoved = true
            }
        } catch {
            log.event("switch_failed", operationErrorDescription(error))
            var failures: [String] = []
            if configChanged {
                log.event("rollback_config", "Restoring prior configuration")
                do {
                    guard try snapshot(config) == after else {
                        throw AppError(message: "配置已被其他程序改动，未覆盖当前文件。")
                    }
                    try restore(beforeConfig, config)
                    log.event("rollback_config", "Restored")
                } catch {
                    failures.append("连接配置")
                    log.event("rollback_config", "Restore failed: \(operationErrorDescription(error))")
                }
            }
            if catalogChanged {
                do {
                    guard try snapshot(catalogURL) == catalogData else { throw AppError(message: "模型目录已被外部修改，未覆盖") }
                    try restore(beforeCatalog, catalogURL)
                } catch {
                    failures.append("模型目录")
                    log.event("rollback_catalog", operationErrorDescription(error))
                }
            }
            if authRemoved {
                log.event("rollback_auth", "Restoring prior auth.json")
                do {
                    guard try snapshot(auth) == nil else {
                        throw AppError(message: "登录文件已由其他程序创建，未覆盖当前文件。")
                    }
                    if let beforeAuth { try restoreAuthWithoutReplacing(beforeAuth) }
                    log.event("rollback_auth", "Restored")
                } catch {
                    failures.append("登录文件")
                    log.event("rollback_auth", "Restore failed: \(operationErrorDescription(error))")
                }
            }
            guard failures.isEmpty else {
                throw AppError(message: "切换失败，\(failures.joined(separator: "、"))未能还原：\(operationErrorDescription(error))")
            }
            throw error
        }
        var effectiveIssue = ""
        if requireClosed || !runtimeOverride.isEmpty {
            log.event("verify_effective", "Reading the final effective Codex configuration")
            var sourceError: UnsafeMutablePointer<CChar>?
            let sources = root.path.withCString { home in
                runtimeOverride.withCString {
                    yilai_sources_inspect(home, $0, &sourceError)
                }
            }
            if let sources {
                defer { yilai_sources_finish(sources) }
                if sourceError != nil {
                    yilai_config_free(sourceError)
                    sourceError = nil
                }
                let verified = token.withCString {
                    yilai_sources_verify(sources, $0, &sourceError)
                }
                if verified == 0 {
                    effectiveIssue = sourceError.map { String(cString: $0) }
                        ?? "无法确认最终生效配置"
                }
            } else {
                effectiveIssue = sourceError.map { String(cString: $0) }
                    ?? "无法读取 Codex 最终配置"
            }
            if let sourceError { yilai_config_free(sourceError) }
        }
        if !effectiveIssue.isEmpty {
            var message = "API 已配置并保留，但功能探测未完全通过：\(log.sanitized(effectiveIssue))。不会回滚连接，请截图此提示。"
            return OperationOutcome(message: message, warning: true)
        }
        return OperationOutcome(
            message: "API 已配置，最终探测确认连接、模型和生图均已生效。历史对话保持不变，请重新打开 Codex。",
            warning: false)
    }
}

func selfTest() throws {
    for test in [yilai_config_self_test, yilai_diagnostic_self_test] {
        var error: UnsafeMutablePointer<CChar>?
        let ok = test(&error)
        let detail = error.map { String(cString: $0) } ?? "Core test failed"
        if let error { yilai_config_free(error) }
        guard ok != 0 else { throw AppError(message: detail) }
    }
    let files = FileManager.default
    let root = files.temporaryDirectory.appendingPathComponent("YilaiCodexSwitcher-swift-\(UUID().uuidString)", isDirectory: true)
    try files.createDirectory(at: root, withIntermediateDirectories: true)
    defer { try? files.removeItem(at: root) }
    func put(_ name: String, _ value: String) throws {
        let url = root.appendingPathComponent(name)
        try files.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
        try Data(value.utf8).write(to: url)
    }
    func text(_ name: String) throws -> String {
        try String(contentsOf: root.appendingPathComponent(name), encoding: .utf8)
    }
    func check(_ value: Bool, _ message: String) throws {
        if !value { throw AppError(message: message) }
    }
    // Historical data is opaque to this application, including malformed files.
    let untouchedFiles: [String: Data] = [
        "unrelated/active.jsonl": Data("{\"type\":\"session_meta\",\"payload\":{\"model_provider\":\"yilai\"}}\n".utf8),
        "unrelated/broken.jsonl": Data("invalid json\n".utf8),
        "unrelated/old.jsonl": Data("archived opaque bytes\n".utf8),
        "unrelated-state.bin": Data([0, 255, 42, 7, 0, 19]),
        "unrelated-state-wal.bin": Data([11, 128, 0, 15]),
        "unrelated-state-shm.bin": Data([27, 0, 129]),
        "session_index.jsonl": Data("invalid index\n".utf8),
        "history.jsonl": Data("opaque command history\n".utf8),
        "unrelated/pending.json": Data("invalid pending journal\n".utf8)
    ]
    for (name, data) in untouchedFiles {
        let url = root.appendingPathComponent(name)
        try files.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
        try data.write(to: url)
    }
    func checkUntouchedFiles() throws {
        for (name, data) in untouchedFiles {
            try check(try Data(contentsOf: root.appendingPathComponent(name)) == data, "Unrelated file changed: \(name)")
        }
    }
    let config = "model='gpt-6-astra'\nmodel_provider='custom'\nmodel_catalog_json='cc-switch-model-catalog.json'\n[model_providers.custom]\nname='Other'\nbase_url='https://other.invalid'\nwire_api='responses'\n"
    let auth = "{\"auth_mode\":\"chatgpt\"}"
    try put(".codex-global-state.json", "{\"electron-persisted-atom-state\":{\"agent-mode-by-host-id\":{\"local\":\"full-access\"}}}")
    try put("config.toml", config)
    try put("auth.json", auth)
    try put("auth.json.yilai-disabled", "keep disabled auth")
    try put("auth.json.yilai-session-test", "keep session auth")
    try put("yilai-switcher-backup/manifest.json", "keep manifest")
    let service = PlatformService(root: root)
    try check(service.mode() == "其他 CCS / 第三方连接", "False Yilai identification")
    // Restoring auth cannot overwrite an account created by another process.
    var existingAuthRejected = false
    do { try service.restoreAuthWithoutReplacing(Data("replacement must not win".utf8)) }
    catch { existingAuthRejected = true }
    try check(try existingAuthRejected && text("auth.json") == auth, "Auth restore overwrote an existing account")
    try files.removeItem(at: root.appendingPathComponent("auth.json"))
    try service.restoreAuthWithoutReplacing(Data(auth.utf8))
    try check(try text("auth.json") == auth, "Auth restore did not recreate an absent file")
    let authPermissions = try files.attributesOfItem(atPath: root.appendingPathComponent("auth.json").path)[.posixPermissions] as? NSNumber
    try check(authPermissions?.intValue == 0o600, "Restored auth permissions are not private")
    let temporaryAuthFiles = try files.contentsOfDirectory(atPath: root.path).filter { $0.hasPrefix(".yilai-auth-restore-") }
    try check(temporaryAuthFiles.isEmpty, "Auth restoration left a temporary credential file")
    // Every entry point must refuse a second writer before touching config/auth.
    do {
        var failure: UnsafeMutablePointer<CChar>?
        let heldLock = root.path.withCString { yilai_operation_lock($0, &failure) }
        defer { if let failure { yilai_config_free(failure) } }
        guard let heldLock else {
            throw AppError(message: failure.map { String(cString: $0) } ?? "Cannot acquire synthetic operation lock")
        }
        defer { yilai_operation_unlock(heldLock) }
        for operation in Operation.allCases {
            var rejected = false
            do { _ = try service.run(operation, key: "sk-test-key", requireClosed: false) }
            catch { rejected = true }
            try check(rejected, "Concurrent operation was not rejected: \(operation)")
            try check(try text("config.toml") == config && text("auth.json") == auth, "Concurrent operation changed configuration or auth")
        }
    }
    // Exercise native POSIX spawning and the three-message handshake with a local
    // shell stub. This validates transport, not a real macOS Codex runtime.
    do {
        func jsonLine(_ value: [String: Any]) throws -> String {
            let bytes = try JSONSerialization.data(withJSONObject: value, options: [.sortedKeys])
            return String(decoding: bytes, as: UTF8.self)
        }
        func shellLiteral(_ value: String) -> String {
            "'" + value.replacingOccurrences(of: "'", with: "'\\''") + "'"
        }
        let initializedReply = try jsonLine(["id": 1, "result": ["userAgent": "probe's synthetic runtime"]])
        let configurationReply = try jsonLine([
            "id": 2,
            "result": [
                "config": [String: Any](),
                "origins": [String: Any](),
                "layers": [[
                    "name": ["type": "user", "file": root.appendingPathComponent("config.toml").path, "profile": NSNull()],
                    "config": [String: Any](),
                    "disabledReason": NSNull()
                ]]
            ]
        ])
        let probeStub = root.appendingPathComponent("runtime probe's stub")
        defer { try? files.removeItem(at: probeStub) }
        let script = [
            "#!/bin/sh",
            "IFS= read -r request || exit 10",
            "case \"$request\" in *initialize*) ;; *) exit 11 ;; esac",
            "printf '%s\\n' " + shellLiteral(initializedReply),
            "IFS= read -r request || exit 12",
            "case \"$request\" in *initialized*) ;; *) exit 13 ;; esac",
            "IFS= read -r request || exit 14",
            "case \"$request\" in *config/read*) ;; *) exit 15 ;; esac",
            "printf '%s\\n' " + shellLiteral(configurationReply),
            "exit 0"
        ].joined(separator: "\n") + "\n"
        try Data(script.utf8).write(to: probeStub)
        try files.setAttributes([.posixPermissions: 0o700], ofItemAtPath: probeStub.path)
        var lockError: UnsafeMutablePointer<CChar>?
        let probeLock = root.path.withCString { yilai_operation_lock($0, &lockError) }
        defer { if let lockError { yilai_config_free(lockError) } }
        guard let probeLock else {
            throw AppError(message: lockError.map { String(cString: $0) } ?? "Cannot lock synthetic probe home")
        }
        defer { yilai_operation_unlock(probeLock) }
        var sourceError: UnsafeMutablePointer<CChar>?
        let sourcePlan = root.path.withCString { home in
            probeStub.path.withCString { yilai_sources_prepare(home, $0, &sourceError) }
        }
        defer {
            if let sourceError { yilai_config_free(sourceError) }
            if let sourcePlan { yilai_sources_finish(sourcePlan) }
        }
        guard sourcePlan != nil else {
            throw AppError(message: sourceError.map { String(cString: $0) } ?? "POSIX runtime probe stub failed")
        }
        try check(try text("config.toml") == config && text("auth.json") == auth, "Read-only source probing changed config/auth")
    }

    // Final probing happens after the primary connection commit and cannot
    // roll it back, even when the supplied runtime is unusable.
    do {
        let warningRoot = root.appendingPathComponent("probe-warning", isDirectory: true)
        try files.createDirectory(at: warningRoot, withIntermediateDirectories: true)
        try Data(config.utf8).write(to: warningRoot.appendingPathComponent("config.toml"))
        try Data(auth.utf8).write(to: warningRoot.appendingPathComponent("auth.json"))
        let warningService = PlatformService(root: warningRoot)
        let warning = try warningService.run(
            .configure, key: "sk-test-key", requireClosed: false,
            runtimeOverride: root.appendingPathComponent("missing-codex-runtime").path)
        try check(warning.warning && warning.message.contains("API 已配置并保留"), "Probe failure was not shown as a committed warning")
        try check(warningService.mode() == "易来 API" && !files.fileExists(atPath: warningRoot.appendingPathComponent("auth.json").path), "Probe failure rolled back the primary connection")
    }

    try Data("old-catalog-sentinel".utf8).write(to: root.appendingPathComponent("yilai-model-catalog.json"))
    // An immutable auth file must fail the switch without changing config/history.
    let authPath = root.appendingPathComponent("auth.json").path
    guard chflags(authPath, UInt32(UF_IMMUTABLE)) == 0 else {
        throw AppError(message: "Cannot lock synthetic auth file")
    }
    defer { _ = chflags(authPath, 0) }
    var failed = false
    do { _ = try service.run(.configure, key: "sk-test-key", requireClosed: false) }
    catch { failed = true }
    _ = chflags(authPath, 0)
    try check(try failed && text("config.toml") == config && text("auth.json") == auth, "Locked auth switch rollback failed")
    try check(try text("yilai-model-catalog.json") == "old-catalog-sentinel", "Auth failure did not restore catalog")
    try checkUntouchedFiles()

    // Configuration succeeds even with opaque or malformed history/database files.
    // This also verifies that a failed operation released its exclusive lock.
    _ = try service.run(.configure, key: "sk-test-key", requireClosed: false)
    try check(service.mode() == "易来 API", "API switch did not select Yilai")
    try check(!files.fileExists(atPath: authPath), "API switch retained auth")
    let configured = try text("config.toml")
    try check(configured.contains("image_generation = true"), "API switch did not enable images")
    try check(configured.contains("gpt-6-astra") && configured.contains("yilai-model-catalog.json"), "API switch did not install the managed catalog")
    try check(configured.contains("approval_policy = \"never\"") && configured.contains("sandbox_mode = \"danger-full-access\""), "API switch did not preserve the desktop full-access mode")
    try check(try text("yilai-model-catalog.json") == String(cString: yilai_model_catalog()), "Managed catalog bytes differ")
    try checkUntouchedFiles()
    _ = try service.run(.configure, key: "sk-test-key", requireClosed: false)
    try check(try text("config.toml") == configured, "API configuration is not idempotent")
    try checkUntouchedFiles()
    try check(try text("auth.json.yilai-disabled") == "keep disabled auth" && text("auth.json.yilai-session-test") == "keep session auth" && text("yilai-switcher-backup/manifest.json") == "keep manifest", "Switch touched unrelated backups")

    // Official switching preserves auth; failures restore both bytes and absence.
    do {
        let officialRoot = root.appendingPathComponent("official-switch", isDirectory: true)
        try files.createDirectory(at: officialRoot, withIntermediateDirectories: true)
        let officialService = PlatformService(root: officialRoot)
        let configURL = officialRoot.appendingPathComponent("config.toml")
        let authURL = officialRoot.appendingPathComponent("auth.json")
        let originalConfig = Data(config.utf8)
        let officialAuth = Data("{\"auth_mode\":\"chatgpt\",\"tokens\":{\"access_token\":\"test-official-access\",\"refresh_token\":\"test-official-refresh\",\"account_id\":\"test-account\"}}".utf8)
        try originalConfig.write(to: configURL)
        try officialAuth.write(to: authURL)
        _ = try officialService.run(.official, requireClosed: false)
        try check(officialService.mode() == "OpenAI 官方", "Official switch did not select OpenAI")
        try check(try Data(contentsOf: authURL) == officialAuth, "Official switch changed auth")

        let invalidConfigs: [(Data, String)] = [
            (Data((config + "\0# must not be silently discarded\n").utf8), "空字符"),
            (Data([0xff, 0xfe, 0xfd]), "UTF-8")
        ]
        for (invalidConfig, expectedError) in invalidConfigs {
            try invalidConfig.write(to: configURL)
            var failureMessage = ""
            do { _ = try officialService.run(.official, requireClosed: false) }
            catch { failureMessage = error.localizedDescription }
            try check(failureMessage.contains(expectedError), "Official switch did not reject invalid configuration")
            try check(try Data(contentsOf: configURL) == invalidConfig, "Rejected official input changed configuration")
            try check(try Data(contentsOf: authURL) == officialAuth, "Rejected official input changed auth")
        }

        // Existing migration markers must be ignored without warnings or writes.
        let pendingURL = officialRoot.appendingPathComponent("yilai-history-backups/pending.json")
        try files.createDirectory(at: pendingURL.deletingLastPathComponent(), withIntermediateDirectories: true)
        let pendingData = Data("{\"generation\":\"official-test-invalid\"}".utf8)
        try pendingData.write(to: pendingURL)
        let priorConfigs: [Data?] = [originalConfig, nil]
        for priorConfig in priorConfigs {
            if let priorConfig { try priorConfig.write(to: configURL) }
            else if files.fileExists(atPath: configURL.path) { try files.removeItem(at: configURL) }
            let warning = try officialService.run(.official, requireClosed: false)
            try check(!warning.warning && officialService.mode() == "OpenAI 官方", "Official history warning rolled back the switch")
            try check(try Data(contentsOf: authURL) == officialAuth, "Official history warning changed auth")
            try check(try Data(contentsOf: pendingURL) == pendingData, "Official history warning changed its pending pointer")
        }
        let apiWarningRoot = root.appendingPathComponent("api-history-warning", isDirectory: true)
        try files.createDirectory(at: apiWarningRoot, withIntermediateDirectories: true)
        let apiWarningService = PlatformService(root: apiWarningRoot)
        try originalConfig.write(to: apiWarningRoot.appendingPathComponent("config.toml"))
        try officialAuth.write(to: apiWarningRoot.appendingPathComponent("auth.json"))
        let apiPending = apiWarningRoot.appendingPathComponent("yilai-history-backups/pending.json")
        try files.createDirectory(at: apiPending.deletingLastPathComponent(), withIntermediateDirectories: true)
        try pendingData.write(to: apiPending)
        let apiWarning = try apiWarningService.run(.configure, key: "sk-isolated-api-warning", requireClosed: false)
        try check(!apiWarning.warning && apiWarningService.mode() == "易来 API", "API history warning rolled back the primary configuration")
        try check(!files.fileExists(atPath: apiWarningRoot.appendingPathComponent("auth.json").path), "API history warning restored deleted auth")
        try check(try Data(contentsOf: apiPending) == pendingData, "API history warning changed its pending pointer")
        try files.removeItem(at: apiPending)
        let malformedSession = apiWarningRoot.appendingPathComponent("sessions/broken.jsonl")
        try files.createDirectory(at: malformedSession.deletingLastPathComponent(), withIntermediateDirectories: true)
        try Data("not-json\n".utf8).write(to: malformedSession)
        try officialAuth.write(to: apiWarningRoot.appendingPathComponent("auth.json"))
        let skippedWarning = try apiWarningService.run(.configure, key: "sk-isolated-api-warning", requireClosed: false)
        try check(!skippedWarning.warning && apiWarningService.mode() == "易来 API", "Skipped malformed history blocked API configuration")
        try check(!files.fileExists(atPath: apiWarningRoot.appendingPathComponent("auth.json").path), "Skipped malformed history restored deleted auth")
        try check(try String(contentsOf: malformedSession, encoding: .utf8) == "not-json\n", "Skipped malformed history file was modified")
    }

    // Reset is a reversible rename; it must preserve config bytes, auth and history.
    try put("auth.json", auth)
    let beforeReset = try text("config.toml")
    _ = try service.run(.cleanup, requireClosed: false)
    try check(!files.fileExists(atPath: root.appendingPathComponent("config.toml").path), "Reset retained active config")
    let renamed = try files.contentsOfDirectory(at: root, includingPropertiesForKeys: nil).filter { $0.lastPathComponent.hasPrefix("config.toml.disabled-") }
    try check(renamed.count == 1, "Reset did not create exactly one disabled config")
    try check(try String(contentsOf: renamed[0], encoding: .utf8) == beforeReset, "Reset changed saved config bytes")
    try check(try text("auth.json") == auth, "Reset changed auth")
    try checkUntouchedFiles()
    _ = try service.run(.cleanup, requireClosed: false)
    try check(try text("auth.json") == auth, "Empty reset changed auth")
    try checkUntouchedFiles()

    // Malformed input may appear in a parser error; the displayed reason must redact it.
    let malformed = "experimental_bearer_token = 'sk-test-key\n"
    try put("config.toml", malformed)
    var safeError = ""
    do { _ = try service.run(.configure, key: "sk-test-key", requireClosed: false) }
    catch { safeError = error.localizedDescription }
    try check(!safeError.isEmpty && !safeError.contains("sk-test-key"), "Displayed error leaked API key")
    try check(try text("config.toml") == malformed && text("auth.json") == auth, "Invalid config changed state")
    try checkUntouchedFiles()

    try check(!files.fileExists(atPath: root.appendingPathComponent("yilai-switcher-logs").path), "Error-only UI unexpectedly created process logs")
}
