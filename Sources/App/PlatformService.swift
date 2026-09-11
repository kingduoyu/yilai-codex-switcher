import Foundation
import Darwin
import ConfigRewrite
import HistorySync
import Diagnostics

enum Operation: String, CaseIterable { case images, configure, official, sync, undo, cleanup }
struct AppError: LocalizedError {
    let message: String
    var errorDescription: String? { message }
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
        case "checking_apps": lastMainStage = "检查后台程序"
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

    func finish(success: Bool, message: String) {
        message.withCString { yilai_diagnostic_end(context, success ? 1 : 0, $0) }
        context = nil
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

    func run(_ operation: Operation, key: String = "", requireClosed: Bool = true) throws -> String {
        let log = DiagnosticLog(root: root, operation: operation, key: key.trimmingCharacters(in: .whitespacesAndNewlines))
        do {
            let result = try runOperation(operation, key: key, requireClosed: requireClosed, log: log)
            log.finish(success: true, message: "Operation completed")
            return result
        } catch {
            let message = log.failureMessage(error.localizedDescription)
            log.event("failure", message)
            log.finish(success: false, message: message)
            throw AppError(message: message)
        }
    }

    private func runOperation(_ operation: Operation, key: String, requireClosed: Bool, log: DiagnosticLog) throws -> String {
        log.event("checking_apps", requireClosed ? "Checking Codex and CC-Switch processes" : "Synthetic-home test: process check skipped")
        if requireClosed { try closed() }
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
        case .official: action = Int32(YILAI_OFFICIAL)
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
        let switching = operation == .configure || operation == .official
        let beforeAuth = switching ? try snapshot(auth) : nil
        guard try snapshot(config) == beforeConfig else {
            throw AppError(message: "配置已被其他程序改动，请关闭后重试。")
        }
        var configChanged = false
        var authRemoved = false
        do {
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
                log.event("sync_history", "Synchronizing local history")
                _ = try history()
            }
        } catch {
            log.event("switch_failed", error.localizedDescription)
            var failures: [String] = []
            if configChanged {
                log.event("rollback_config", "Restoring prior configuration")
                do {
                    try restore(beforeConfig, config)
                    log.event("rollback_config", "Restored")
                } catch {
                    failures.append("连接配置")
                    log.event("rollback_config", "Restore failed: \(error.localizedDescription)")
                }
            }
            if authRemoved {
                log.event("rollback_auth", "Restoring prior auth.json")
                do {
                    try restore(beforeAuth, auth)
                    log.event("rollback_auth", "Restored")
                } catch {
                    failures.append("登录文件")
                    log.event("rollback_auth", "Restore failed: \(error.localizedDescription)")
                }
            }
            guard failures.isEmpty else {
                throw AppError(message: "切换失败，\(failures.joined(separator: "、"))未能还原：\(error.localizedDescription)")
            }
            throw error
        }
        switch operation {
        case .images: return "生图已启用。请重新打开 Codex。"
        case .configure: return "已切换到易来 API，生图和本地历史已就绪。请重新打开 Codex。"
        case .official: return "已切回官方设置，本地历史已同步。请重新打开 Codex 并登录官方账号。"
        default: return "操作完成。"
        }
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
    _ = try service.run(.images, requireClosed: false)
    try check(try text("auth.json") == auth, "Enhancement cleared auth")
    let enhanced = try text("config.toml")
    _ = try service.run(.images, requireClosed: false)
    try check(try text("config.toml") == enhanced, "Enhancement not idempotent")

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

    // A sync validation failure occurs after auth deletion: both bytes must return.
    try put("sessions/broken.jsonl", "invalid json\n")
    for operation in [Operation.configure, .official] {
        failed = false
        do { _ = try service.run(operation, key: "sk-test-key", requireClosed: false) }
        catch { failed = true }
        try check(try failed && text("config.toml") == enhanced && text("auth.json") == auth, "Sync failure did not restore config/auth")
        try check(try text("sessions/active.jsonl") == session, "Sync failure altered history")
    }
    try files.removeItem(at: root.appendingPathComponent("sessions/broken.jsonl"))
    _ = try service.run(.configure, key: "sk-test-key", requireClosed: false)
    try check(service.mode() == "易来 API", "API switch did not select Yilai")
    try check(!files.fileExists(atPath: authPath), "API switch retained auth")
    try check(try provider("sessions/active.jsonl") == "custom", "API switch did not sync history")
    try check(try text("config.toml").contains("image_generation = true"), "API switch did not enable images")

    try put("auth.json", auth)
    try put("archived_sessions/official.jsonl", record("archived", "openai"))
    _ = try service.run(.official, requireClosed: false)
    try check(service.mode() == "OpenAI 官方", "Official switch did not select official")
    try check(!files.fileExists(atPath: authPath), "Official switch retained auth")
    try check(try provider("archived_sessions/official.jsonl") == "custom", "Official switch did not sync archived history")
    try check(try text("auth.json.yilai-disabled") == "keep disabled auth" && text("auth.json.yilai-session-test") == "keep session auth" && text("yilai-switcher-backup/manifest.json") == "keep manifest", "Switch touched unrelated backups")

    // Reset is a reversible rename; it must preserve config bytes, auth and history.
    try put("auth.json", auth)
    let beforeReset = try text("config.toml")
    let beforeHistory = try text("sessions/active.jsonl")
    _ = try service.run(.cleanup, requireClosed: false)
    try check(!files.fileExists(atPath: root.appendingPathComponent("config.toml").path), "Reset retained active config")
    let renamed = try files.contentsOfDirectory(at: root, includingPropertiesForKeys: nil).filter { $0.lastPathComponent.hasPrefix("config.toml.disabled-") }
    try check(renamed.count == 1, "Reset did not create exactly one disabled config")
    try check(try String(contentsOf: renamed[0], encoding: .utf8) == beforeReset, "Reset changed saved config bytes")
    try check(try text("auth.json") == auth && text("sessions/active.jsonl") == beforeHistory, "Reset changed auth/history")
    _ = try service.run(.cleanup, requireClosed: false)
    try check(try text("auth.json") == auth, "Empty reset changed auth")

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
    _ = try PlatformService(root: noLogRoot).run(.cleanup, requireClosed: false)

    let logFiles = try files.contentsOfDirectory(at: service.logsDirectory, includingPropertiesForKeys: [.isRegularFileKey])
        .filter { (try? $0.resourceValues(forKeys: [.isRegularFileKey]).isRegularFile) == true }
    try check(!logFiles.isEmpty, "Operation diagnostics were not created")
    let logText = try logFiles.map { try String(contentsOf: $0, encoding: .utf8) }.joined(separator: "\n")
    try check(logText.contains("rollback_auth") && logText.contains("failure"), "Failure/rollback diagnostics missing")
    try check(!logText.contains("sk-test-key"), "Diagnostics leaked the API key")
}
