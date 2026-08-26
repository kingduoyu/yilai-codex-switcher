import Foundation

enum CodexMode {
  case notConfigured
  case official
  case other
  case yilai

  var label: String {
    switch self {
    case .notConfigured: return "尚未配置"
    case .official: return "OpenAI 官方"
    case .other: return "其他第三方配置"
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
        if value == "\"yilai\"" || value == "'yilai'" { return .yilai }
        if value == "\"openai\"" || value == "'openai'" { return .official }
        return .other
      }
    }
    return .official
  }

  func switchToYilai(key rawKey: String) throws {
    let key = rawKey.trimmingCharacters(in: .whitespacesAndNewlines)
    guard !key.isEmpty else { throw SwitcherError.message("API Key 不能为空") }
    guard key.unicodeScalars.allSatisfy({ $0.value >= 32 && $0.value != 127 }) else {
      throw SwitcherError.message("API Key 包含无效字符")
    }

    try files.createDirectory(at: paths.codex, withIntermediateDirectories: true)
    let backupAlreadyExisted = exists(paths.manifest)
    let manifest = try ensureBackup()
    var authMoves: [(from: URL, to: URL)] = []
    do {
      let existing = exists(paths.config) ? try read(paths.config) : ""
      try writeAtomic(buildYilaiConfig(existing: existing, key: key), to: paths.config)

      // A disabled auth without our manifest is residue from an interrupted older run.
      // Preserve it, then make the current auth the authoritative restore point.
      if !backupAlreadyExisted && exists(paths.disabledAuth) {
        let archive = nextAuthArchiveURL()
        try files.moveItem(at: paths.disabledAuth, to: archive)
        authMoves.append((paths.disabledAuth, archive))
      }

      if exists(paths.auth) {
        if exists(paths.disabledAuth) {
          let archive = nextAuthArchiveURL()
          try files.moveItem(at: paths.auth, to: archive)
          authMoves.append((paths.auth, archive))
        } else {
          try files.moveItem(at: paths.auth, to: paths.disabledAuth)
          authMoves.append((paths.auth, paths.disabledAuth))
        }
      } else if manifest.authExisted && !exists(paths.disabledAuth) {
        throw SwitcherError.message("原有 auth.json 及其停用文件均不存在，未执行切换")
      }

      try verifyYilaiConfiguration(expectedKey: key)
    } catch {
      for move in authMoves.reversed() where !exists(move.from) && exists(move.to) {
        try? files.moveItem(at: move.to, to: move.from)
      }
      try? restoreConfig(manifest)
      throw error
    }
  }

  func switchToOfficial() throws {
    try files.createDirectory(at: paths.codex, withIntermediateDirectories: true)
    let manifest = exists(paths.manifest) ? try readManifest() : nil
    var archivedActiveAuth: URL?
    if manifest?.authExisted == true {
      guard exists(paths.disabledAuth) else {
        throw SwitcherError.message("找不到 auth.json.yilai-disabled，无法恢复原有登录")
      }
      if exists(paths.auth) {
        let archive = nextAuthArchiveURL()
        try files.moveItem(at: paths.auth, to: archive)
        archivedActiveAuth = archive
      }
    }

    let previousConfig = exists(paths.config) ? try read(paths.config) : nil
    var baseConfig = previousConfig ?? ""
    if previousConfig == nil, manifest?.configExisted == true, exists(paths.backupConfig) {
      baseConfig = try read(paths.backupConfig)
    }

    var authRestored = false
    do {
      try writeAtomic(buildOfficialConfig(existing: baseConfig), to: paths.config)
      if manifest?.authExisted == true {
        try files.moveItem(at: paths.disabledAuth, to: paths.auth)
        authRestored = true
      }
    } catch {
      if authRestored, exists(paths.auth), !exists(paths.disabledAuth) {
        try? files.moveItem(at: paths.auth, to: paths.disabledAuth)
      }
      if let archive = archivedActiveAuth, !exists(paths.auth), exists(archive) {
        try? files.moveItem(at: archive, to: paths.auth)
      }
      if let previousConfig {
        try? writeAtomic(previousConfig, to: paths.config)
      } else if exists(paths.config) {
        try? files.removeItem(at: paths.config)
      }
      throw error
    }

    if exists(paths.backup) { try? files.removeItem(at: paths.backup) }
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

  private func buildOfficialConfig(existing: String) -> String {
    let newline = existing.contains("\r\n") ? "\r\n" : "\n"
    var lines = splitLines(existing)
    removeAllModelProviders(from: &lines)
    removeTopLevel("model_provider", from: &lines)
    setTopLevel("model", value: "\"gpt-5.6-terra\"", in: &lines)
    while lines.last?.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty == true {
      lines.removeLast()
    }
    return lines.joined(separator: newline) + newline
  }

  private func verifyYilaiConfiguration(expectedKey: String) throws {
    let contents = try read(paths.config)
    let lines = splitLines(contents)
    let firstTable = lines.firstIndex(where: isTable) ?? lines.endIndex
    let topLevel = Array(lines[..<firstTable])

    guard settingValue("model_provider", in: topLevel) == "\"yilai\"" else {
      throw SwitcherError.message("写入后校验失败：model_provider 未设置为 yilai")
    }
    guard settingValue("model", in: topLevel) == "\"gpt-5.6-sol\"" else {
      throw SwitcherError.message("写入后校验失败：未找到 gpt-5.6-sol 模型")
    }

    guard
      let providerStart = lines.firstIndex(where: {
        $0.trimmingCharacters(in: .whitespacesAndNewlines) == "[model_providers.yilai]"
      })
    else {
      throw SwitcherError.message("写入后校验失败：易来 provider 配置不存在")
    }
    let providerEnd = lines[(providerStart + 1)...].firstIndex(where: isTable) ?? lines.endIndex
    let provider = Array(lines[(providerStart + 1)..<providerEnd])
    guard settingValue("base_url", in: provider) == "\"https://api.yilai-ai.com\"",
      settingValue("wire_api", in: provider) == "\"responses\"",
      settingValue("requires_openai_auth", in: provider) == "false",
      settingValue("experimental_bearer_token", in: provider) == "\"\(escapeToml(expectedKey))\"",
      settingValue("http_headers", in: provider)?.contains("local-image-extension") == true
    else {
      throw SwitcherError.message("写入后校验失败：易来地址、Key 或生图请求头不完整")
    }
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

  private func removeAllModelProviders(from lines: inout [String]) {
    while let start = lines.firstIndex(where: isModelProviderTable) {
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

  private func removeTopLevel(_ key: String, from lines: inout [String]) {
    let table = lines.firstIndex(where: isTable) ?? lines.endIndex
    for index in lines[..<table].indices.reversed() where isSetting(lines[index], key: key) {
      lines.remove(at: index)
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

  private func isModelProviderTable(_ line: String) -> Bool {
    let value = line.trimmingCharacters(in: .whitespacesAndNewlines)
    return value.hasPrefix("[model_providers.") && value.hasSuffix("]")
  }

  private func isSetting(_ line: String, key: String) -> Bool {
    let value = line.trimmingCharacters(in: .whitespacesAndNewlines)
    guard value.hasPrefix(key) else { return false }
    return value.dropFirst(key.count).trimmingCharacters(in: .whitespacesAndNewlines).hasPrefix("=")
  }

  private func settingValue(_ key: String, in lines: [String]) -> String? {
    guard let line = lines.first(where: { isSetting($0, key: key) }) else { return nil }
    return line.split(separator: "=", maxSplits: 1).dropFirst().first.map(String.init)?
      .trimmingCharacters(in: .whitespacesAndNewlines)
  }

  private func nextAuthArchiveURL() -> URL {
    paths.codex.appendingPathComponent(
      "auth.json.yilai-session-\(ISO8601DateFormatter().string(from: Date()).replacingOccurrences(of: ":", with: "-"))-\(UUID().uuidString.prefix(8))"
    )
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
  guard try service.mode() == .other else {
    throw SwitcherError.message("自测失败：第三方模式识别")
  }
  try service.switchToYilai(key: "sk-test-key")
  guard try service.mode() == .yilai else { throw SwitcherError.message("自测失败：模式识别") }
  let switched = try String(contentsOf: root.appendingPathComponent("config.toml"), encoding: .utf8)
  guard switched.contains("model_provider = \"yilai\""),
    switched.contains("model = \"gpt-5.6-sol\""),
    switched.contains("requires_openai_auth = false"),
    switched.contains("local-image-extension"), switched.contains("sk-test-key"),
    switched.contains("[plugins.\"browser@openai-bundled\"]")
  else {
    throw SwitcherError.message("自测失败：易来配置内容")
  }
  try "{\"auth_mode\":\"transient\"}".data(using: .utf8)!.write(
    to: root.appendingPathComponent("auth.json"))
  try service.switchToYilai(key: "sk-new-key")
  let archivedAuths = try FileManager.default.contentsOfDirectory(
    at: root, includingPropertiesForKeys: nil
  ).filter { $0.lastPathComponent.hasPrefix("auth.json.yilai-session-") }
  guard archivedAuths.count == 1, !FileManager.default.fileExists(
    atPath: root.appendingPathComponent("auth.json").path)
  else {
    throw SwitcherError.message("自测失败：重复切换登录保护")
  }
  try service.switchToOfficial()
  let official = try String(contentsOf: root.appendingPathComponent("config.toml"), encoding: .utf8)
  guard try service.mode() == .official,
    official.contains("model = \"gpt-5.6-terra\""),
    !official.contains("model_provider"),
    !official.contains("[model_providers."),
    !official.contains("sk-new-key"),
    official.contains("[plugins.\"browser@openai-bundled\"]"),
    try String(contentsOf: root.appendingPathComponent("auth.json"), encoding: .utf8)
      == originalAuth
  else { throw SwitcherError.message("自测失败：官方配置生成") }

  let newRoot = root.appendingPathComponent("new-user", isDirectory: true)
  let newService = CodexConfigurationService(codexDirectory: newRoot)
  try newService.switchToYilai(key: "sk-new-user")
  try newService.switchToOfficial()
  let newOfficial = try String(
    contentsOf: newRoot.appendingPathComponent("config.toml"), encoding: .utf8)
  guard try newService.mode() == .official,
    newOfficial.contains("model = \"gpt-5.6-terra\""),
    !FileManager.default.fileExists(atPath: newRoot.appendingPathComponent("auth.json").path)
  else { throw SwitcherError.message("自测失败：新用户切换官方") }

  let directRoot = root.appendingPathComponent("direct-official", isDirectory: true)
  try FileManager.default.createDirectory(at: directRoot, withIntermediateDirectories: true)
  try originalConfig.data(using: .utf8)!.write(to: directRoot.appendingPathComponent("config.toml"))
  let directService = CodexConfigurationService(codexDirectory: directRoot)
  try directService.switchToOfficial()
  guard try directService.mode() == .official else {
    throw SwitcherError.message("自测失败：无备份时不能直接切换官方")
  }
}
