import Foundation
import Darwin
import ConfigRewrite
import HistorySync

enum Operation: String, CaseIterable { case images, configure, sync, undo, cleanup }
struct AppError: LocalizedError { let message: String; var errorDescription: String? { message } }
final class PlatformService {
    let root: URL
    private let files = FileManager.default
    init(root: URL? = nil) {
        let env = ProcessInfo.processInfo.environment["CODEX_HOME"].flatMap { $0.isEmpty ? nil : URL(fileURLWithPath: $0, isDirectory: true) }
        self.root = root ?? env ?? FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent(".codex", isDirectory: true)
    }
    private var config: URL { root.appendingPathComponent("config.toml") }
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
        let process = Process(); process.executableURL = URL(fileURLWithPath: "/usr/bin/pgrep")
        process.arguments = ["-x", "(Codex|codex|cc-switch|CC Switch)"]
        process.standardOutput = Pipe(); process.standardError = Pipe()
        try process.run(); process.waitUntilExit()
        guard process.terminationStatus == 1 else { throw AppError(message: "请完全退出 Codex 和 CC-Switch 后再操作；关闭窗口后也请检查后台进程。") }
    }
    private func regular(_ url: URL) throws {
        guard files.fileExists(atPath: url.path) else { return }
        let info = try url.resourceValues(forKeys: [.isRegularFileKey, .isSymbolicLinkKey])
        guard info.isRegularFile == true, info.isSymbolicLink != true else { throw AppError(message: "预期为普通文件：\(url.path)") }
    }
    private func read(_ url: URL) throws -> String { try regular(url); return try String(contentsOf: url, encoding: .utf8) }
    private func write(_ data: Data, _ url: URL) throws {
        try regular(url); try files.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
        try data.write(to: url, options: .atomic)
    }
    func run(_ operation: Operation, key: String = "", requireClosed: Bool = true) throws -> String {
        if requireClosed { try closed() }
        if operation == .sync || operation == .undo {
            var failure: UnsafeMutablePointer<CChar>?
            let output = root.path.withCString { yilai_sync_history($0, operation == .undo ? 1 : 0, &failure) }
            defer { if let output { yilai_config_free(output) }; if let failure { yilai_config_free(failure) } }
            guard let output else { throw AppError(message: failure.map { String(cString: $0) } ?? "历史同步失败") }
            let report = try JSONSerialization.jsonObject(with: Data(String(cString: output).utf8)) as? [String: Any] ?? [:]
            let title = operation == .undo ? "已撤销上次同步" : "已同步本地历史"
            let summary = "\(title)：\(report["files"] as? Int ?? 0) 个会话文件，\(report["rows"] as? Int ?? 0) 条索引。"
            let backup = report["backup"] as? String ?? ""
            return summary + (backup.isEmpty ? "" : "\n备份：\(backup)")
        }
        let before = files.fileExists(atPath: config.path) ? try read(config) : ""
        let token = key.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !before.utf8.contains(0), !token.utf8.contains(0) else { throw AppError(message: "配置或 Key 包含无效空字符") }
        let action = operation == .images ? Int32(YILAI_ENHANCE) : operation == .configure ? Int32(YILAI_CONFIGURE) : Int32(YILAI_CLEANUP)
        var failure: UnsafeMutablePointer<CChar>?
        let output = before.withCString { text in token.withCString { yilai_apply_config(text, $0, action, &failure) } }
        defer { if let output { yilai_config_free(output) }; if let failure { yilai_config_free(failure) } }
        guard let output else { throw AppError(message: failure.map { String(cString: $0) } ?? "配置更新失败") }
        let after = Data(String(cString: output).utf8)
        guard (files.fileExists(atPath: config.path) ? try read(config) : "") == before else { throw AppError(message: "配置已被其他程序改动，请关闭后重试") }
        if operation != .cleanup { try write(after, config) }
        else {
            var targets = [config, root.appendingPathComponent("auth.json"), root.appendingPathComponent("auth.json.yilai-disabled"), root.appendingPathComponent("yilai-switcher-backup/manifest.json"), root.appendingPathComponent("yilai-switcher-backup/config.toml")]
            targets += try files.contentsOfDirectory(at: root, includingPropertiesForKeys: nil).filter { $0.lastPathComponent.hasPrefix("auth.json.yilai-session-") }
            let snapshots: [Data?] = try targets.map { url in try regular(url); return files.fileExists(atPath: url.path) ? try Data(contentsOf: url) : nil }
            var changed: [Int] = []
            do {
                try write(after, config); changed.append(0)
                for i in 1..<targets.count where snapshots[i] != nil {
                    try files.trashItem(at: targets[i], resultingItemURL: nil)
                    guard !files.fileExists(atPath: targets[i].path) else { throw AppError(message: "文件未移入废纸篓") }
                    changed.append(i)
                }
                guard !files.fileExists(atPath: targets[1].path), !files.fileExists(atPath: targets[2].path) else { throw AppError(message: "登录文件被重新创建，请关闭 Codex 后重试") }
            } catch {
                var restored = true
                for i in changed.reversed() {
                    do { if let data = snapshots[i] { try write(data, targets[i]) } else { try files.removeItem(at: targets[i]) } }
                    catch { restored = false }
                }
                guard restored else { throw AppError(message: "清理回滚不完整，请保留废纸篓备份") }
                throw error
            }
        }
        switch operation {
        case .images: return "生图已启用；模型、目录和登录保持不变。请重开 Codex。"
        case .configure: return "易来连接已配置；保留现有模型和登录。请重开 Codex。"
        default: return "旧登录已移入废纸篓；已解除本工具旧模型目录引用。"
        }
    }
}
func selfTest() throws {
    for test in [yilai_config_self_test, yilai_history_self_test] {
        var error: UnsafeMutablePointer<CChar>?
        let ok = test(&error); let detail = error.map { String(cString: $0) } ?? "Core test failed"
        if let error { yilai_config_free(error) }
        guard ok != 0 else { throw AppError(message: detail) }
    }
    let files = FileManager.default
    let root = files.temporaryDirectory.appendingPathComponent("YilaiCodexSwitcher-swift-\(UUID().uuidString)", isDirectory: true)
    try files.createDirectory(at: root, withIntermediateDirectories: true)
    defer { try? files.removeItem(at: root) }
    func put(_ name: String, _ value: String) throws { let url = root.appendingPathComponent(name); try files.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true); try Data(value.utf8).write(to: url) }
    func text(_ name: String) throws -> String { try String(contentsOf: root.appendingPathComponent(name), encoding: .utf8) }
    func check(_ value: Bool, _ message: String) throws { if !value { throw AppError(message: message) } }
    let config = "model='gpt-6-astra'\nmodel_provider='custom'\nmodel_catalog_json='cc-switch-model-catalog.json'\n[model_providers.custom]\nname='Other'\nbase_url='https://other.invalid'\nwire_api='responses'\n"
    let auth = "{\"auth_mode\":\"chatgpt\"}"
    try put("config.toml", config); try put("auth.json", auth); try put("auth.json.yilai-disabled", auth)
    try put("yilai-switcher-backup/manifest.json", "old manifest"); try put("yilai-switcher-backup/config.toml", "old config"); try put("auth.json.yilai-session-test", auth)
    let service = PlatformService(root: root)
    try check(service.mode() == "其他 CCS / 第三方连接", "False Yilai identification")
    _ = try service.run(.images, requireClosed: false)
    try check(try text("auth.json") == auth && text("auth.json.yilai-session-test") == auth, "Default cleared auth")
    let once = try text("config.toml"); _ = try service.run(.images, requireClosed: false)
    try check(try text("config.toml") == once, "Enhancement not idempotent")
    _ = try service.run(.configure, key: "sk-test-key", requireClosed: false)
    let before = try text("config.toml")
    let locked = root.appendingPathComponent("auth.json.yilai-disabled").path
    guard chflags(locked, UInt32(UF_IMMUTABLE)) == 0 else { throw AppError(message: "Cannot lock test file") }
    defer { _ = chflags(locked, 0) }
    var failed = false
    do { _ = try service.run(.cleanup, requireClosed: false) } catch { failed = true }
    _ = chflags(locked, 0)
    try check(try failed && text("config.toml") == before && text("auth.json") == auth, "Cleanup rollback failed")
    _ = try service.run(.cleanup, requireClosed: false)
    try check(!files.fileExists(atPath: root.appendingPathComponent("auth.json").path) && !files.fileExists(atPath: root.appendingPathComponent("auth.json.yilai-session-test").path), "Explicit cleanup incomplete")
    try put("config.toml", "model='gpt-6-astra'\n"); try put("auth.json", auth)
    failed = false
    do { _ = try service.run(.cleanup, requireClosed: false) } catch { failed = true }
    try check(try failed && text("auth.json") == auth, "Official auth was not protected")
    // Verify platform trash retains bytes and handles duplicate original names.
    var destinations: [URL] = []
    for value in ["first synthetic credential", "second synthetic credential"] {
        try put("trash-test.json", value); var result: NSURL?
        try files.trashItem(at: root.appendingPathComponent("trash-test.json"), resultingItemURL: &result)
        guard let result = result as URL? else { throw AppError(message: "Missing trash path") }
        try check(try String(contentsOf: result, encoding: .utf8) == value, "Trash bytes changed"); destinations.append(result)
    }
    try check(destinations[0] != destinations[1], "Trash collision overwrote previous file")
}
