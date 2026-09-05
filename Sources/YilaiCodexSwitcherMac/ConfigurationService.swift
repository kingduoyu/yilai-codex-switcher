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
  let modelCatalog: URL

  init(codex: URL) {
    self.codex = codex
    config = codex.appendingPathComponent("config.toml")
    auth = codex.appendingPathComponent("auth.json")
    disabledAuth = codex.appendingPathComponent("auth.json.yilai-disabled")
    backup = codex.appendingPathComponent("yilai-switcher-backup", isDirectory: true)
    manifest = backup.appendingPathComponent("manifest.json")
    backupConfig = backup.appendingPathComponent("config.toml")
    modelCatalog = codex.appendingPathComponent("yilai-model-catalog.json")
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

  var configurationPath: String { paths.config.path }

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
    let previousConfig = exists(paths.config) ? try read(paths.config) : nil
    let previousCatalog = exists(paths.modelCatalog) ? try read(paths.modelCatalog) : nil
    var authMoves: [(from: URL, to: URL)] = []
    var catalogWritten = false
    var configWritten = false
    do {
      try writeAtomic(YilaiModelCatalog.json, to: paths.modelCatalog)
      catalogWritten = true
      try writeAtomic(buildYilaiConfig(existing: previousConfig ?? "", key: key), to: paths.config)
      configWritten = true

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
      guard !exists(paths.auth) else {
        throw SwitcherError.message("写入后校验失败：auth.json 仍在生效，请完全退出 Codex 后重试")
      }
      if manifest.authExisted && !exists(paths.disabledAuth) {
        throw SwitcherError.message("写入后校验失败：原有官方登录未安全停用")
      }
    } catch {
      for move in authMoves.reversed() where !exists(move.from) && exists(move.to) {
        try? files.moveItem(at: move.to, to: move.from)
      }
      if configWritten {
        if let previousConfig { try? writeAtomic(previousConfig, to: paths.config) }
        else { try? files.removeItem(at: paths.config) }
      }
      if catalogWritten {
        if let previousCatalog { try? writeAtomic(previousCatalog, to: paths.modelCatalog) }
        else { try? files.removeItem(at: paths.modelCatalog) }
      }
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
    if manifest?.configExisted == true, exists(paths.backupConfig) {
      baseConfig = try read(paths.backupConfig)
    }

    do {
      try writeAtomic(buildOfficialConfig(existing: baseConfig), to: paths.config)
      if manifest?.authExisted == true {
        try files.moveItem(at: paths.disabledAuth, to: paths.auth)
      }
    } catch {
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
    removeTopLevel("model_catalog_json", from: &lines)
    setTopLevel("model_catalog_json", value: "\"\(escapeToml(paths.modelCatalog.path))\"", in: &lines)
    // macOS users may keep ChatGPT OAuth in Keychain. Force file-only auth lookup while
    // the auth.json file is disabled so a cached Free account cannot hide image generation.
    setTopLevel("cli_auth_credentials_store", value: "\"file\"", in: &lines)
    setTableSetting("features", key: "image_generation", value: "true", in: &lines)
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
    removeTopLevel("model_catalog_json", from: &lines)
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
    guard settingValue("model_catalog_json", in: topLevel) == "\"\(escapeToml(paths.modelCatalog.path))\"",
      try read(paths.modelCatalog) == YilaiModelCatalog.json
    else { throw SwitcherError.message("写入后校验失败：固定模型目录未生效") }
    guard settingValue("cli_auth_credentials_store", in: topLevel) == "\"file\"" else {
      throw SwitcherError.message("写入后校验失败：未切换到纯 API 登录存储")
    }
    guard
      let featuresStart = lines.firstIndex(where: {
        $0.trimmingCharacters(in: .whitespacesAndNewlines) == "[features]"
      })
    else { throw SwitcherError.message("写入后校验失败：生图功能开关不存在") }
    let featuresSearch = (featuresStart + 1)..<lines.endIndex
    let featuresEnd = lines[featuresSearch].firstIndex(where: isTable) ?? lines.endIndex
    let features = Array(lines[(featuresStart + 1)..<featuresEnd])
    guard settingValue("image_generation", in: features) == "true" else {
      throw SwitcherError.message("写入后校验失败：生图功能未启用")
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

  private func setTableSetting(
    _ tableName: String, key: String, value: String, in lines: inout [String]
  ) {
    let header = "[\(tableName)]"
    guard
      let tableStart = lines.firstIndex(where: {
        $0.trimmingCharacters(in: .whitespacesAndNewlines) == header
      })
    else {
      while lines.last?.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty == true {
        lines.removeLast()
      }
      if !lines.isEmpty { lines.append("") }
      lines += [header, "\(key) = \(value)"]
      return
    }

    let tableSearch = (tableStart + 1)..<lines.endIndex
    let tableEnd = lines[tableSearch].firstIndex(where: isTable) ?? lines.endIndex
    if let index = lines[(tableStart + 1)..<tableEnd].firstIndex(where: {
      isSetting($0, key: key)
    }) {
      lines[index] = "\(key) = \(value)"
    } else {
      lines.insert("\(key) = \(value)", at: tableEnd)
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
  let catalog = try JSONSerialization.jsonObject(with: Data(YilaiModelCatalog.json.utf8)) as? [String: Any]
  guard let models = catalog?["models"] as? [[String: Any]],
    models.compactMap({ $0["slug"] as? String }) == ["gpt-5.6-sol", "gpt-5.6-terra", "gpt-6-astra"],
    models.allSatisfy({ ($0["visibility"] as? String) == "list" && ($0["supported_in_api"] as? Bool) == true })
  else { throw SwitcherError.message("自测失败：内嵌模型目录必须恰好包含三个可见模型") }
  let root = FileManager.default.temporaryDirectory.appendingPathComponent(
    "YilaiCodexSwitcher-swift-\(UUID().uuidString)", isDirectory: true)
  defer { try? FileManager.default.removeItem(at: root) }
  try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
  let originalConfig =
    "model_provider = \"custom\"\r\nmodel = \"gpt-old\"\r\ncli_auth_credentials_store = \"keyring\"\r\n\r\n[features]\r\nimage_generation = false\r\n\r\n[model_providers.custom]\r\nname = \"Original\"\r\n\r\n[plugins.\"browser@openai-bundled\"]\r\nenabled = true\r\n"
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
    switched.contains("model_catalog_json = "),
    try String(contentsOf: root.appendingPathComponent("yilai-model-catalog.json"), encoding: .utf8) == YilaiModelCatalog.json,
    switched.contains("cli_auth_credentials_store = \"file\""),
    switched.contains("image_generation = true"),
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
    official.contains("cli_auth_credentials_store = \"keyring\""),
    official.contains("image_generation = false"),
    !official.contains("model_provider"),
    !official.contains("model_catalog_json"),
    !official.contains("[model_providers."),
    !official.contains("sk-new-key"),
    official.contains("[plugins.\"browser@openai-bundled\"]"),
    try String(contentsOf: root.appendingPathComponent("auth.json"), encoding: .utf8)
      == originalAuth
  else { throw SwitcherError.message("自测失败：官方配置和原凭据存储未恢复") }

  let newRoot = root.appendingPathComponent("new-user", isDirectory: true)
  let newService = CodexConfigurationService(codexDirectory: newRoot)
  try newService.switchToYilai(key: "sk-new-user")
  try newService.switchToOfficial()
  let newOfficial = try String(
    contentsOf: newRoot.appendingPathComponent("config.toml"), encoding: .utf8)
  guard try newService.mode() == .official,
    newOfficial.contains("model = \"gpt-5.6-terra\""),
    newOfficial.contains("cli_auth_credentials_store = \"file\""),
    !FileManager.default.fileExists(atPath: newRoot.appendingPathComponent("auth.json").path)
  else { throw SwitcherError.message("自测失败：新用户切换官方") }

  let keyringOnlyRoot = root.appendingPathComponent("keyring-only", isDirectory: true)
  try FileManager.default.createDirectory(at: keyringOnlyRoot, withIntermediateDirectories: true)
  let keyringOnlyConfig =
    "model = \"gpt-old\"\ncli_auth_credentials_store = \"keyring\"\n\n[plugins.\"browser@openai-bundled\"]\nenabled = true\n"
  try keyringOnlyConfig.data(using: .utf8)!.write(
    to: keyringOnlyRoot.appendingPathComponent("config.toml"))
  let keyringOnlyService = CodexConfigurationService(codexDirectory: keyringOnlyRoot)
  try keyringOnlyService.switchToYilai(key: "sk-keyring-only")
  let keyringSwitched = try String(
    contentsOf: keyringOnlyRoot.appendingPathComponent("config.toml"), encoding: .utf8)
  guard keyringSwitched.contains("cli_auth_credentials_store = \"file\""),
    !FileManager.default.fileExists(atPath: keyringOnlyRoot.appendingPathComponent("auth.json").path)
  else { throw SwitcherError.message("自测失败：Keychain 混合登录未隔离") }
  try keyringOnlyService.switchToOfficial()
  let keyringOfficial = try String(
    contentsOf: keyringOnlyRoot.appendingPathComponent("config.toml"), encoding: .utf8)
  guard keyringOfficial.contains("cli_auth_credentials_store = \"keyring\""),
    keyringOfficial.contains("model = \"gpt-5.6-terra\""),
    keyringOfficial.contains("[plugins.\"browser@openai-bundled\"]")
  else { throw SwitcherError.message("自测失败：Keychain 官方登录设置未恢复") }

  let directRoot = root.appendingPathComponent("direct-official", isDirectory: true)
  try FileManager.default.createDirectory(at: directRoot, withIntermediateDirectories: true)
  try originalConfig.data(using: .utf8)!.write(to: directRoot.appendingPathComponent("config.toml"))
  let directService = CodexConfigurationService(codexDirectory: directRoot)
  try directService.switchToOfficial()
  guard try directService.mode() == .official else {
    throw SwitcherError.message("自测失败：无备份时不能直接切换官方")
  }

  let upgradeRoot = root.appendingPathComponent("old-user", isDirectory: true)
  try FileManager.default.createDirectory(at: upgradeRoot, withIntermediateDirectories: true)
  try originalConfig.data(using: .utf8)!.write(to: upgradeRoot.appendingPathComponent("config.toml"))
  try originalAuth.data(using: .utf8)!.write(to: upgradeRoot.appendingPathComponent("auth.json"))
  let upgradeService = CodexConfigurationService(codexDirectory: upgradeRoot)
  try upgradeService.switchToYilai(key: "sk-old-key")
  let manifestURL = upgradeRoot.appendingPathComponent("yilai-switcher-backup/manifest.json")
  let originalManifest = try Data(contentsOf: manifestURL)
  let oldConfig = try String(contentsOf: upgradeRoot.appendingPathComponent("config.toml"), encoding: .utf8)
    .components(separatedBy: .newlines).filter { !$0.hasPrefix("model_catalog_json") }.joined(separator: "\n")
  try ("model_catalog_json = \"old-catalog.json\"\nmodel_catalog_json = \"stale-catalog.json\"\n" + oldConfig)
    .data(using: .utf8)!.write(to: upgradeRoot.appendingPathComponent("config.toml"))
  try "old catalog containing luna".data(using: .utf8)!.write(to: upgradeRoot.appendingPathComponent("yilai-model-catalog.json"))
  try "old cache containing luna".data(using: .utf8)!.write(to: upgradeRoot.appendingPathComponent("models_cache.json"))
  try upgradeService.switchToYilai(key: "sk-upgraded-key")
  let upgraded = try String(contentsOf: upgradeRoot.appendingPathComponent("config.toml"), encoding: .utf8)
  guard upgraded.components(separatedBy: "model_catalog_json").count == 2,
    !upgraded.contains("old-catalog.json"), upgraded.contains("sk-upgraded-key"),
    try String(contentsOf: upgradeRoot.appendingPathComponent("yilai-model-catalog.json"), encoding: .utf8) == YilaiModelCatalog.json,
    try Data(contentsOf: manifestURL) == originalManifest,
    try String(contentsOf: upgradeRoot.appendingPathComponent("auth.json.yilai-disabled"), encoding: .utf8) == originalAuth,
    try String(contentsOf: upgradeRoot.appendingPathComponent("models_cache.json"), encoding: .utf8) == "old cache containing luna"
  else { throw SwitcherError.message("自测失败：旧用户目录覆盖与原备份保护") }
  try upgradeService.switchToOfficial()
  let upgradedOfficial = try String(contentsOf: upgradeRoot.appendingPathComponent("config.toml"), encoding: .utf8)
  guard !upgradedOfficial.contains("model_catalog_json"),
    try String(contentsOf: upgradeRoot.appendingPathComponent("auth.json"), encoding: .utf8) == originalAuth
  else { throw SwitcherError.message("自测失败：旧用户升级后切回官方") }

  let rollbackRoot = root.appendingPathComponent("catalog-rollback", isDirectory: true)
  try FileManager.default.createDirectory(at: rollbackRoot, withIntermediateDirectories: true)
  try originalAuth.data(using: .utf8)!.write(to: rollbackRoot.appendingPathComponent("auth.json"))
  let rollbackService = CodexConfigurationService(codexDirectory: rollbackRoot)
  try rollbackService.switchToYilai(key: "sk-before-failure")
  let rollbackConfigURL = rollbackRoot.appendingPathComponent("config.toml")
  let rollbackCatalogURL = rollbackRoot.appendingPathComponent("yilai-model-catalog.json")
  let beforeFailure = try Data(contentsOf: rollbackConfigURL)
  try "previous catalog".data(using: .utf8)!.write(to: rollbackCatalogURL)
  try FileManager.default.removeItem(at: rollbackRoot.appendingPathComponent("auth.json.yilai-disabled"))
  var failed = false
  do { try rollbackService.switchToYilai(key: "sk-after-failure") }
  catch { failed = true }
  guard failed, try Data(contentsOf: rollbackConfigURL) == beforeFailure,
    try String(contentsOf: rollbackCatalogURL, encoding: .utf8) == "previous catalog"
  else { throw SwitcherError.message("自测失败：配置与模型目录失败回滚") }
}
