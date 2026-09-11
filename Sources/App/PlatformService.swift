import Foundation
import Darwin
import ConfigRewrite
import HistorySync
import Diagnostics
import OperationGuard
import ConfigSources

enum Operation: String, CaseIterable { case images, configure, sync, undo, cleanup }
struct AppError: LocalizedError {
    let message: String
    var errorDescription: String? { message }
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
        case "recover_history": lastMainStage = "恢复未完成的历史同步"
        case "checking_apps": lastMainStage = "检查后台程序"
        case "prepare_sources": lastMainStage = "识别配置来源"
        case "apply_sources": lastMainStage = "处理连接覆盖配置"
        case "verify_sources": lastMainStage = "核验实际生效连接"
        case "prepare_config": lastMainStage = "准备连接配置"
        case "write_config": lastMainStage = "写入配置"
        case "delete_auth": lastMainStage = "删除旧登录文件"
        case "sync_history": lastMainStage = "同步本地历史"
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
    private(set) var historyWarning = false

    init(root: URL? = nil) {
        let env = ProcessInfo.processInfo.environment["CODEX_HOME"].flatMap {
            $0.isEmpty ? nil : URL(fileURLWithPath: $0, isDirectory: true)
        }
        self.root = root ?? env ?? FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent(".codex", isDirectory: true)
    }

    private var config: URL { root.appendingPathComponent("config.toml") }
    private var auth: URL { root.appendingPathComponent("auth.json") }
    var logsDirectory: URL { root.appendingPathComponent("yilai-switcher-logs", isDirectory: true) }

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

    private func recoverHistory() throws {
        var failure: UnsafeMutablePointer<CChar>?
        let output = root.path.withCString { yilai_recover_history($0, &failure) }
        defer {
            if let output { yilai_config_free(output) }
            if let failure { yilai_config_free(failure) }
        }
        guard output != nil else {
            throw AppError(message: failure.map { String(cString: $0) } ?? "恢复未完成的历史同步失败")
        }
    }

    private func history(undo: Bool = false) throws -> String {
        var failure: UnsafeMutablePointer<CChar>?
        let output = root.path.withCString { yilai_sync_history($0, undo ? 1 : 0, &failure) }
        defer {
            if let output { yilai_config_free(output) }
            if let failure { yilai_config_free(failure) }
        }
        guard let output else {
            throw AppError(message: failure.map { String(cString: $0) } ?? "历史同步失败")
        }
        // The core has already committed when returning success. Display formatting
        // must not throw and trigger a platform rollback after that commit.
        let report = (try? JSONSerialization.jsonObject(with: Data(String(cString: output).utf8))) as? [String: Any]
        let title = undo ? "已撤销上次同步" : "已同步本地历史"
        return "\(title)：\(report?["files"] as? Int ?? 0) 个会话文件，\(report?["rows"] as? Int ?? 0) 条索引。"
    }

    func run(_ operation: Operation, key: String = "", requireClosed: Bool = true, runtimeOverride: String = "") throws -> String {
        historyWarning = false
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
            log.finish(success: true, message: historyWarning ? result : "Operation completed")
            return result
        } catch {
            var message = log.failureMessage(operationErrorDescription(error))
            log.event("failure", message)
            if !log.finish(success: false, message: message) {
                message += " 诊断日志未能完整保存，请复制当前错误信息。"
            }
            throw AppError(message: message)
        }
    }

    private func runOperation(_ operation: Operation, key: String, requireClosed: Bool, runtimeOverride: String, log: DiagnosticLog) throws -> String {
        log.event("checking_apps", requireClosed ? "Checking Codex and CC-Switch processes" : "Synthetic-home test: process check skipped")
        if requireClosed { try closed() }
        var historyIssue: String?
        if operation == .configure {
            log.event("recover_history", "Checking and recovering any interrupted history transaction")
            do {
                try recoverHistory()
                log.event("recover_history", "Pending history recovery completed")
            } catch {
                historyIssue = log.sanitized(operationErrorDescription(error))
                log.event("history_warning", "History recovery incomplete; preserving the pending transaction and continuing API setup: \(historyIssue!)")
            }
        }
        if operation == .sync || operation == .undo {
            log.event("sync_history", operation == .undo ? "Restoring local history classification" : "Synchronizing local history")
            return try history(undo: operation == .undo)
        }
        if operation == .cleanup {
            log.event("prepare_config", "Checking config before reset")
            guard let before = try snapshot(config) else { return "尚无配置，无需重置。" }
            let disabled = root.appendingPathComponent("config.toml.disabled-\(UUID().uuidString)")
            guard try snapshot(config) == before else {
                throw AppError(message: "配置已被其他程序改动，请关闭后重试。")
            }
            log.event("rename_config", "Renaming config to a unique disabled file")
            try files.moveItem(at: config, to: disabled)
            return "配置已重置，旧文件已保留为 \(disabled.lastPathComponent)。请重新选择连接。"
        }

        log.event("prepare_config", "Validating configuration update")
        if operation == .configure && key.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
            throw AppError(message: "请先填写易来 API Key，再点击“切换到易来 API”。")
        }
        var sources: OpaquePointer?
        defer { if let sources { yilai_sources_finish(sources) } }
        let switching = operation == .configure
        if switching && (requireClosed || !runtimeOverride.isEmpty) {
            log.event("prepare_sources", "Reading effective configuration layers using the installed runtime")
            var sourceError: UnsafeMutablePointer<CChar>?
            defer { if let sourceError { yilai_config_free(sourceError) } }
            sources = root.path.withCString { home in
                runtimeOverride.withCString { yilai_sources_prepare(home, $0, &sourceError) }
            }
            guard let sources else {
                throw AppError(message: sourceError.map { String(cString: $0) } ?? "配置来源识别失败，未继续切换。")
            }
            if let summary = yilai_sources_summary(sources) {
                defer { yilai_config_free(summary) }
                log.event("sources_plan", String(cString: summary))
            }
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
        let action: Int32
        switch operation {
        case .images: action = Int32(YILAI_ENHANCE)
        case .configure: action = Int32(YILAI_CONFIGURE)
        default: throw AppError(message: "不支持的配置操作。")
        }
        var failure: UnsafeMutablePointer<CChar>?
        let output = before.withCString { text in
            token.withCString { yilai_apply_config(text, $0, action, &failure) }
        }
        defer {
            if let output { yilai_config_free(output) }
            if let failure { yilai_config_free(failure) }
        }
        guard let output else {
            throw AppError(message: failure.map { String(cString: $0) } ?? "配置更新失败")
        }
        let after = Data(String(cString: output).utf8)
        let beforeAuth = switching ? try snapshot(auth) : nil
        guard try snapshot(config) == beforeConfig else {
            throw AppError(message: "配置已被其他程序改动，请关闭后重试。")
        }
        var configChanged = false
        var authRemoved = false
        var sourcesAttempted = false
        do {
            if let sources {
                log.event("apply_sources", "Removing only effective connection overrides from the prepared sources")
                var sourceError: UnsafeMutablePointer<CChar>?
                defer { if let sourceError { yilai_config_free(sourceError) } }
                sourcesAttempted = true
                guard yilai_sources_apply(sources, &sourceError) != 0 else {
                    throw AppError(message: sourceError.map { String(cString: $0) } ?? "连接覆盖配置处理失败。")
                }
            }
            log.event("write_config", "Writing configuration atomically")
            try write(after, config)
            configChanged = true
            if switching {
                log.event("delete_auth", "Removing only auth.json when present")
                guard try snapshot(auth) == beforeAuth else {
                    throw AppError(message: "登录文件已被其他程序改动，请关闭后重试。")
                }
                if beforeAuth != nil {
                    try files.removeItem(at: auth)
                    authRemoved = true
                }
                if let sources {
                    log.event("verify_sources", "Verifying the effective provider and authentication before history sync")
                    var sourceError: UnsafeMutablePointer<CChar>?
                    defer { if let sourceError { yilai_config_free(sourceError) } }
                    let verified = token.withCString {
                        yilai_sources_verify(sources, $0, &sourceError)
                    }
                    guard verified != 0 else {
                        throw AppError(message: sourceError.map { String(cString: $0) } ?? "新连接未实际生效，已停止切换。")
                    }
                }
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
            if sourcesAttempted, let sources {
                log.event("rollback_sources", "Restoring configuration sources changed by this operation")
                var sourceError: UnsafeMutablePointer<CChar>?
                defer { if let sourceError { yilai_config_free(sourceError) } }
                if yilai_sources_rollback(sources, &sourceError) == 0 {
                    failures.append("配置来源")
                    log.event("rollback_sources", "Restore failed: \(sourceError.map { String(cString: $0) } ?? "未知错误")")
                } else {
                    log.event("rollback_sources", "Restored")
                }
            }
            guard failures.isEmpty else {
                throw AppError(message: "切换失败，\(failures.joined(separator: "、"))未能还原：\(operationErrorDescription(error))")
            }
            throw error
        }
        // API configuration is committed after authentication/source verification.
        // History is independent: its failure must never revert a working connection.
        if operation == .configure {
            if historyIssue == nil {
                log.event("sync_history", "Synchronizing local history")
                do { _ = try history() }
                catch {
                    historyIssue = log.sanitized(operationErrorDescription(error))
                    log.event("history_warning", "API configuration remains active: \(historyIssue!)")
                }
            }
            if let historyIssue {
                historyWarning = true
                return "API 已配置，历史同步未完成：\(historyIssue)。生图已启用，可重新打开 Codex；请查看日志排查历史问题。"
            }
            return "已切换到易来 API，生图和本地历史已就绪。请重新打开 Codex。"
        }
        return operation == .images ? "生图已启用。请重新打开 Codex。" : "操作完成。"
    }
}

func selfTest() throws {
    for test in [yilai_config_self_test, yilai_history_self_test, yilai_diagnostic_self_test] {
        var error: UnsafeMutablePointer<CChar>?
        let ok = test(&error)
        let detail = error.map { String(cString: $0) } ?? "Core test failed"
        if let error { yilai_config_free(error) }
        guard ok != 0 else { throw AppError(message: detail) }
    }
    let files = FileManager.default
    let root = files.temporaryDirectory.appendingPathComponent("YilaiCodexSwitcher-swift-\(UUID().uuidString)", isDirectory: true)
    try files.createDirectory(at: root, withIntermediateDirectories: true)
    let oldSQLiteHome = ProcessInfo.processInfo.environment["CODEX_SQLITE_HOME"]
    setenv("CODEX_SQLITE_HOME", root.path, 1)
    defer {
        if let oldSQLiteHome { setenv("CODEX_SQLITE_HOME", oldSQLiteHome, 1) }
        else { unsetenv("CODEX_SQLITE_HOME") }
        try? files.removeItem(at: root)
    }
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
    func record(_ id: String, _ provider: String) -> String {
        "{\"type\":\"session_meta\",\"payload\":{\"id\":\"\(id)\",\"model_provider\":\"\(provider)\"}}\n"
    }
    func provider(_ name: String) throws -> String {
        let bytes = Data(try text(name).split(separator: "\n")[0].utf8)
        let row = try JSONSerialization.jsonObject(with: bytes) as? [String: Any]
        return (row?["payload"] as? [String: Any])?["model_provider"] as? String ?? ""
    }
    let config = "model='gpt-6-astra'\nmodel_provider='custom'\nmodel_catalog_json='cc-switch-model-catalog.json'\n[model_providers.custom]\nname='Other'\nbase_url='https://other.invalid'\nwire_api='responses'\n"
    let auth = "{\"auth_mode\":\"chatgpt\"}"
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
    // The next operation also verifies release of the held lock.
    _ = try service.run(.images, requireClosed: false)
    try check(try text("auth.json") == auth, "Enhancement cleared auth")
    let enhanced = try text("config.toml")
    _ = try service.run(.images, requireClosed: false)
    try check(try text("config.toml") == enhanced, "Enhancement not idempotent")

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
        try check(try text("config.toml") == enhanced && text("auth.json") == auth, "Read-only source probing changed config/auth")
    }

    // A supplied test runtime enables source probing even with process checks skipped.
    // Preparation failure must preserve credentials/config and release the operation.
    var missingRuntimeError = ""
    do {
        _ = try service.run(.configure, key: "sk-test-key", requireClosed: false,
                            runtimeOverride: root.appendingPathComponent("missing-codex-runtime").path)
    } catch { missingRuntimeError = error.localizedDescription }
    try check(missingRuntimeError.contains("识别配置来源"), "Explicit test runtime did not enable source verification")
    try check(try text("config.toml") == enhanced && text("auth.json") == auth, "Source preparation failure changed config/auth")

    // An immutable auth file must fail the switch without changing config/history.
    let session = record("active", "openai")
    try put("sessions/active.jsonl", session)
    let authPath = root.appendingPathComponent("auth.json").path
    guard chflags(authPath, UInt32(UF_IMMUTABLE)) == 0 else {
        throw AppError(message: "Cannot lock synthetic auth file")
    }
    defer { _ = chflags(authPath, 0) }
    var failed = false
    do { _ = try service.run(.configure, key: "sk-test-key", requireClosed: false) }
    catch { failed = true }
    _ = chflags(authPath, 0)
    try check(try failed && text("config.toml") == enhanced && text("auth.json") == auth, "Locked auth switch rollback failed")
    try check(try text("sessions/active.jsonl") == session, "Locked auth changed history")

    // History validation fails after API setup commits; the connection stays active.
    try put("sessions/broken.jsonl", "invalid json\n")
    let historyWarning = try service.run(.configure, key: "sk-test-key", requireClosed: false)
    try check(service.historyWarning && historyWarning.contains("API 已配置，历史同步未完成"), "History failure was not reported as a warning")
    try check(service.mode() == "易来 API" && !files.fileExists(atPath: authPath), "History failure reverted the API connection")
    try check(try text("config.toml").contains("image_generation = true"), "History warning lost image support")
    try check(try text("sessions/active.jsonl") == session, "Sync validation failure altered history")
    try files.removeItem(at: root.appendingPathComponent("sessions/broken.jsonl"))

    // A broken pending-history journal cannot block API setup or be overwritten.
    try put("config.toml", enhanced)
    try put("auth.json", auth)
    try put("yilai-history-backups/pending.json", "invalid pending journal\n")
    let recoveryWarning = try service.run(.configure, key: "sk-test-key", requireClosed: false)
    try check(service.historyWarning && recoveryWarning.contains("API 已配置，历史同步未完成"), "History recovery failure blocked API setup")
    try check(service.mode() == "易来 API" && !files.fileExists(atPath: authPath), "History recovery failure reverted the API connection")
    try check(try text("yilai-history-backups/pending.json") == "invalid pending journal\n" && text("sessions/active.jsonl") == session, "Failed recovery was overwritten by fresh history sync")
    try files.removeItem(at: root.appendingPathComponent("yilai-history-backups/pending.json"))
    try put("archived_sessions/local.jsonl", record("archived", "openai"))
    _ = try service.run(.configure, key: "sk-test-key", requireClosed: false)
    try check(!service.historyWarning, "Completed history sync retained an old warning")
    try check(service.mode() == "易来 API", "API switch did not select Yilai")
    try check(!files.fileExists(atPath: authPath), "API switch retained auth")
    try check(try provider("sessions/active.jsonl") == "custom", "API switch did not sync history")
    try check(try text("config.toml").contains("image_generation = true"), "API switch did not enable images")
    try check(try provider("archived_sessions/local.jsonl") == "custom", "API switch did not sync archived history")
    try check(try text("auth.json.yilai-disabled") == "keep disabled auth" && text("auth.json.yilai-session-test") == "keep session auth" && text("yilai-switcher-backup/manifest.json") == "keep manifest", "Switch touched unrelated backups")

    // Reset is a reversible rename; it must preserve config bytes, auth and history.
    try put("auth.json", auth)
    let beforeReset = try text("config.toml")
    let beforeHistory = try text("sessions/active.jsonl")
    // Even a recoverable pending transaction belongs to a main switch, not reset.
    let pendingHistory = try text("yilai-history-backups/latest.json")
    try put("yilai-history-backups/pending.json", pendingHistory)
    _ = try service.run(.cleanup, requireClosed: false)
    try check(!files.fileExists(atPath: root.appendingPathComponent("config.toml").path), "Reset retained active config")
    let renamed = try files.contentsOfDirectory(at: root, includingPropertiesForKeys: nil).filter { $0.lastPathComponent.hasPrefix("config.toml.disabled-") }
    try check(renamed.count == 1, "Reset did not create exactly one disabled config")
    try check(try String(contentsOf: renamed[0], encoding: .utf8) == beforeReset, "Reset changed saved config bytes")
    try check(try text("auth.json") == auth && text("sessions/active.jsonl") == beforeHistory, "Reset changed auth/history")
    _ = try service.run(.cleanup, requireClosed: false)
    try check(try text("auth.json") == auth, "Empty reset changed auth")
    try check(try text("yilai-history-backups/pending.json") == pendingHistory, "Reset changed a pending history marker")
    try files.removeItem(at: root.appendingPathComponent("yilai-history-backups/pending.json"))

    // Malformed input may appear in a parser error: both UI and logs must redact it.
    let malformed = "experimental_bearer_token = 'sk-test-key\n"
    try put("config.toml", malformed)
    var safeError = ""
    do { _ = try service.run(.configure, key: "sk-test-key", requireClosed: false) }
    catch { safeError = error.localizedDescription }
    try check(!safeError.isEmpty && !safeError.contains("sk-test-key"), "Displayed error leaked API key")
    try check(try text("config.toml") == malformed && text("auth.json") == auth, "Invalid config changed state")

    // Logging is best-effort even if its directory name is occupied by a file.
    let noLogRoot = root.appendingPathComponent("no-log", isDirectory: true)
    try files.createDirectory(at: noLogRoot, withIntermediateDirectories: true)
    try Data("occupied".utf8).write(to: noLogRoot.appendingPathComponent("yilai-switcher-logs"))
    let noLogService = PlatformService(root: noLogRoot)
    _ = try noLogService.run(.cleanup, requireClosed: false)
    var noLogError = ""
    do { _ = try noLogService.run(.configure, requireClosed: false) }
    catch { noLogError = error.localizedDescription }
    try check(noLogError.contains("诊断日志未能完整保存"), "Unavailable diagnostics were not reported on failure")

    let logFiles = try files.contentsOfDirectory(at: service.logsDirectory, includingPropertiesForKeys: [.isRegularFileKey])
        .filter { (try? $0.resourceValues(forKeys: [.isRegularFileKey]).isRegularFile) == true }
    try check(!logFiles.isEmpty, "Operation diagnostics were not created")
    let logText = try logFiles.map { try String(contentsOf: $0, encoding: .utf8) }.joined(separator: "\n")
    try check(logText.contains("rollback_config") && logText.contains("failure") && logText.contains("history_warning"), "Failure/rollback diagnostics missing")
    try check(!logText.contains("sk-test-key"), "Diagnostics leaked the API key")
}
