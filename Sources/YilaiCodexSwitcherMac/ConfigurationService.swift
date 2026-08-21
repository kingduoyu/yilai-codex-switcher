import Foundation

enum CodexMode {
  case notConfigured
  case officialOrOther
  case yilai

  var label: String {
    switch self {
    case .notConfigured: return "尚未配置"
    case .officialOrOther: return "官方或原有配置"
    case .yilai: return "易来 API"
    }
  }
}

struct CodexPaths {
  let codex: URL
  let config: URL
  let auth: URL
  let disabledAuth: URL
  let backup: URL
  let manifest: URL
  let backupConfig: URL

  init(codex: URL) {
    self.codex = codex
    config = codex.appendingPathComponent("config.toml")
    auth = codex.appendingPathComponent("auth.json")
    disabledAuth = codex.appendingPathComponent("auth.json.yilai-disabled")
    backup = codex.appendingPathComponent("yilai-switcher-backup", isDirectory: true)
    manifest = backup.appendingPathComponent("manifest.json")
    backupConfig = backup.appendingPathComponent("config.toml")
  }
}

private struct BackupManifest: Codable {
  let configExisted: Bool
  let authExisted: Bool
  let createdAtUtc: String

  enum CodingKeys: String, CodingKey {
    case configExisted = "ConfigExisted"
    case authExisted = "AuthExisted"
    case createdAtUtc = "CreatedAtUtc"
  }
}

enum SwitcherError: LocalizedError {
  case message(String)

  var errorDescription: String? {
    if case .message(let text) = self { return text }
    return nil
  }
}

final class CodexConfigurationService {
  private let files = FileManager.default
  private let marker = "# Managed by Yilai Codex Switcher"
  private let paths: CodexPaths

  init(
    codexDirectory: URL = FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent(
      ".codex", isDirectory: true)
  ) {
    paths = CodexPaths(codex: codexDirectory)
  }

  func mode() throws -> CodexMode {
    guard exists(paths.config) else { return .notConfigured }
    for line in splitLines(try read(paths.config)) {
      if isTable(line) { break }
      if isSetting(line, key: "model_provider") {
        let value =
          line.split(separator: "=", maxSplits: 1).dropFirst().first.map(String.init)?
          .trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
        return value == "\"yilai\"" || value == "'yilai'" ? .yilai : .officialOrOther
      }
    }
    return .officialOrOther
  }

  func switchToYilai(key rawKey: String) throws {
    let key = rawKey.trimmingCharacters(in: .whitespacesAndNewlines)
    guard !key.isEmpty else { throw SwitcherError.message("API Key 不能为空") }
    guard key.unicodeScalars.allSatisfy({ $0.value >= 32 && $0.value != 127 }) else {
      throw SwitcherError.message("API Key 包含无效字符")
    }

    try files.createDirectory(at: paths.codex, withIntermediateDirectories: true)
    let manifest = try ensureBackup()
    var authMoved = false
    do {
      let existing = exists(paths.config) ? try read(paths.config) : ""
      try writeAtomic(buildYilaiConfig(existing: existing, key: key), to: paths.config)
      if exists(paths.auth) {
        guard !exists(paths.disabledAuth) else {
          throw SwitcherError.message("auth.json.yilai-disabled 已存在，为避免覆盖登录信息，未执行切换")
        }
        try files.moveItem(at: paths.auth, to: paths.disabledAuth)
        authMoved = true
      } else if manifest.authExisted && !exists(paths.disabledAuth) {
        throw SwitcherError.message("原有 auth.json 及其停用文件均不存在，未执行切换")
      }
    } catch {
      if authMoved && !exists(paths.auth) && exists(paths.disabledAuth) {
        try? files.moveItem(at: paths.disabledAuth, to: paths.auth)
      }
      try? restoreConfig(manifest)
      throw error
    }
  }

  func switchToOfficial() throws {
    let manifest = try readManifest()
    if manifest.authExisted {
      guard exists(paths.disabledAuth) else {
        throw SwitcherError.message("找不到 auth.json.yilai-disabled，无法恢复原有登录")
      }
      guard !exists(paths.auth) else {
        throw SwitcherError.message("检测到新的 auth.json。为避免覆盖登录信息，请完全退出 Codex 后再重试")
      }
    }
    try restoreConfig(manifest)
    if manifest.authExisted { try files.moveItem(at: paths.disabledAuth, to: paths.auth) }
    try files.removeItem(at: paths.backup)
  }

  private func ensureBackup() throws -> BackupManifest {
    if exists(paths.manifest) { return try readManifest() }
    try files.createDirectory(at: paths.backup, withIntermediateDirectories: true)
    let manifest = BackupManifest(
      configExisted: exists(paths.config),
      authExisted: exists(paths.auth),
      createdAtUtc: ISO8601DateFormatter().string(from: Date())
    )
    if manifest.configExisted { try writeAtomic(try read(paths.config), to: paths.backupConfig) }
    let encoder = JSONEncoder()
    try writeAtomic(encoder.encode(manifest), to: paths.manifest)
    return manifest
  }

  private func readManifest() throws -> BackupManifest {
    guard exists(paths.manifest) else {
      throw SwitcherError.message("没有找到可恢复的原有配置。请先使用“切换到易来 API”")
    }
    do {
      return try JSONDecoder().decode(BackupManifest.self, from: Data(contentsOf: paths.manifest))
    } catch { throw SwitcherError.message("备份信息无效") }
  }

  private func restoreConfig(_ manifest: BackupManifest) throws {
    if manifest.configExisted {
      guard exists(paths.backupConfig) else { throw SwitcherError.message("config.toml 备份文件缺失") }
      try writeAtomic(try read(paths.backupConfig), to: paths.config)
    } else if exists(paths.config) {
      try files.removeItem(at: paths.config)
    }
  }

  private func buildYilaiConfig(existing: String, key: String) -> String {
    let newline = existing.contains("\r\n") ? "\r\n" : "\n"
    var lines = splitLines(existing)
    removeManagedProvider(from: &lines)
    setTopLevel("model_provider", value: "\"yilai\"", in: &lines)
    setTopLevel("model", value: "\"gpt-5.6-sol\"", in: &lines)
    while lines.last?.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty == true {
      lines.removeLast()
    }
    if !lines.isEmpty { lines.append("") }
    lines += [
      marker,
      "[model_providers.yilai]",
      "name = \"易来 API\"",
      "base_url = \"https://api.yilai-ai.com\"",
      "wire_api = \"responses\"",
      "requires_openai_auth = false",
      "http_headers = { \"x-openai-actor-authorization\" = \"local-image-extension\" }",
      "experimental_bearer_token = \"\(escapeToml(key))\"",
    ]
    return lines.joined(separator: newline) + newline
  }

  private func removeManagedProvider(from lines: inout [String]) {
    while let start = lines.firstIndex(where: {
      $0.trimmingCharacters(in: .whitespacesAndNewlines) == "[model_providers.yilai]"
    }) {
      var first = start
      var end = start + 1
      while end < lines.count && !isTable(lines[end]) { end += 1 }
      if first > 0 && lines[first - 1].trimmingCharacters(in: .whitespacesAndNewlines) == marker {
        first -= 1
      }
      lines.removeSubrange(first..<end)
    }
  }

  private func setTopLevel(_ key: String, value: String, in lines: inout [String]) {
    let table = lines.firstIndex(where: isTable) ?? lines.endIndex
    if let index = lines[..<table].firstIndex(where: { isSetting($0, key: key) }) {
      lines[index] = "\(key) = \(value)"
    } else {
      lines.insert("\(key) = \(value)", at: table)
    }
  }

  private func splitLines(_ value: String) -> [String] {
    value.replacingOccurrences(of: "\r\n", with: "\n")
      .replacingOccurrences(of: "\r", with: "\n")
      .split(separator: "\n", omittingEmptySubsequences: false)
      .map(String.init)
      .dropLast(value.hasSuffix("\n") || value.hasSuffix("\r") ? 1 : 0)
      .map { $0 }
  }

  private func isTable(_ line: String) -> Bool {
    let value = line.trimmingCharacters(in: .whitespacesAndNewlines)
    return value.hasPrefix("[") && value.hasSuffix("]")
  }

  private func isSetting(_ line: String, key: String) -> Bool {
    let value = line.trimmingCharacters(in: .whitespacesAndNewlines)
    guard value.hasPrefix(key) else { return false }
    return value.dropFirst(key.count).trimmingCharacters(in: .whitespacesAndNewlines).hasPrefix("=")
  }

  private func escapeToml(_ value: String) -> String {
    value.replacingOccurrences(of: "\\", with: "\\\\").replacingOccurrences(of: "\"", with: "\\\"")
  }

  private func exists(_ url: URL) -> Bool { files.fileExists(atPath: url.path) }

  private func read(_ url: URL) throws -> String {
    guard let value = String(data: try Data(contentsOf: url), encoding: .utf8) else {
      throw SwitcherError.message("无法读取 UTF-8 文件：\(url.path)")
    }
    return value
  }

  private func writeAtomic(_ text: String, to url: URL) throws {
    guard let data = text.data(using: .utf8) else { throw SwitcherError.message("无法编码配置文件") }
    try writeAtomic(data, to: url)
  }

  private func writeAtomic(_ data: Data, to url: URL) throws {
    try files.createDirectory(
      at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
    let temporary = url.deletingLastPathComponent().appendingPathComponent(
      ".\(url.lastPathComponent).\(UUID().uuidString).tmp")
    try data.write(to: temporary)
    do {
      if exists(url) {
        _ = try files.replaceItemAt(url, withItemAt: temporary)
      } else {
        try files.moveItem(at: temporary, to: url)
      }
    } catch {
      try? files.removeItem(at: temporary)
      throw error
    }
  }
}

func runSelfTest() throws {
  let root = FileManager.default.temporaryDirectory.appendingPathComponent(
    "YilaiCodexSwitcher-swift-\(UUID().uuidString)", isDirectory: true)
  defer { try? FileManager.default.removeItem(at: root) }
  try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
  let originalConfig =
    "model_provider = \"custom\"\r\nmodel = \"gpt-old\"\r\n\r\n[model_providers.custom]\r\nname = \"Original\"\r\n\r\n[plugins.\"browser@openai-bundled\"]\r\nenabled = true\r\n"
  let originalAuth = "{\"auth_mode\":\"chatgpt\"}"
  try originalConfig.data(using: .utf8)!.write(to: root.appendingPathComponent("config.toml"))
  try originalAuth.data(using: .utf8)!.write(to: root.appendingPathComponent("auth.json"))

  let service = CodexConfigurationService(codexDirectory: root)
  try service.switchToYilai(key: "sk-test-key")
  guard try service.mode() == .yilai else { throw SwitcherError.message("自测失败：模式识别") }
  let switched = try String(contentsOf: root.appendingPathComponent("config.toml"), encoding: .utf8)
  guard switched.contains("requires_openai_auth = false"),
    switched.contains("local-image-extension"), switched.contains("sk-test-key"),
    switched.contains("[plugins.\"browser@openai-bundled\"]")
  else {
    throw SwitcherError.message("自测失败：易来配置内容")
  }
  try service.switchToYilai(key: "sk-new-key")
  try service.switchToOfficial()
  guard
    try String(contentsOf: root.appendingPathComponent("config.toml"), encoding: .utf8)
      == originalConfig,
    try String(contentsOf: root.appendingPathComponent("auth.json"), encoding: .utf8)
      == originalAuth
  else {
    throw SwitcherError.message("自测失败：原样恢复")
  }

  let newRoot = root.appendingPathComponent("new-user", isDirectory: true)
  let newService = CodexConfigurationService(codexDirectory: newRoot)
  try newService.switchToYilai(key: "sk-new-user")
  try newService.switchToOfficial()
  guard !FileManager.default.fileExists(atPath: newRoot.appendingPathComponent("config.toml").path)
  else {
    throw SwitcherError.message("自测失败：新用户恢复")
  }
}
