import Foundation
import Darwin
import ConfigRewrite

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



enum SwitcherError: LocalizedError {
  case message(String)

  var errorDescription: String? {
    if case .message(let text) = self { return text }
    return nil
  }
}

@discardableResult
private func moveToTrash(_ url: URL) throws -> URL? {
  var result: NSURL?
  try FileManager.default.trashItem(at: url, resultingItemURL: &result)
  return result as URL?
}

final class CodexConfigurationService {
  private let files = FileManager.default
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
    let value = try read(paths.config).withCString { yilai_config_mode($0) }
    switch value {
    case 0: return .official
    case 1: return .yilai
    case 2: return .other
    default: throw SwitcherError.message("config.toml 格式无效，请修复后重试。")
    }
  }

  func switchToYilai(key rawKey: String) throws {
    let key = rawKey.trimmingCharacters(in: .whitespacesAndNewlines)
    guard !key.isEmpty else { throw SwitcherError.message("API Key 不能为空") }
    guard key.unicodeScalars.allSatisfy({ $0.value >= 32 && $0.value != 127 }) else {
      throw SwitcherError.message("API Key 包含无效字符")
    }

    try files.createDirectory(at: paths.codex, withIntermediateDirectories: true)
    let existing = exists(paths.config) ? try read(paths.config) : ""
    let config = try buildConfiguration(existing: existing, key: key, official: false)
    try applyConfiguration(config, expectedKey: key)
  }

  func switchToOfficial() throws {
    try files.createDirectory(at: paths.codex, withIntermediateDirectories: true)
    let existing = exists(paths.config) ? try read(paths.config) : ""
    let config = try buildConfiguration(existing: existing, key: "", official: true)
    try applyConfiguration(config, expectedKey: nil)
  }

  private func applyConfiguration(_ config: String, expectedKey: String?) throws {
    struct Snapshot {
      let url: URL
      let data: Data?
    }
    // Keep rollback data only in memory; never create persistent login backups.
    var targets = [paths.config, paths.modelCatalog, paths.auth, paths.disabledAuth,
                   paths.manifest, paths.backupConfig]
    let archives = try files.contentsOfDirectory(at: paths.codex, includingPropertiesForKeys: nil)
      .filter { $0.lastPathComponent.hasPrefix("auth.json.yilai-session-") }
    targets.append(contentsOf: archives)
    let snapshots = try targets.map { url -> Snapshot in
      if exists(url) {
        let values = try url.resourceValues(forKeys: [.isRegularFileKey, .isSymbolicLinkKey])
        guard values.isRegularFile == true, values.isSymbolicLink != true else {
          throw SwitcherError.message("预期为普通文件：\(url.path)")
        }
        return Snapshot(url: url, data: try Data(contentsOf: url))
      }
      return Snapshot(url: url, data: nil)
    }
    var changed: [Int] = []
    do {
      try writeAtomic(config, to: paths.config)
      changed.append(0)
      if expectedKey != nil {
        try writeAtomic(YilaiModelCatalog.json, to: paths.modelCatalog)
        changed.append(1)
      }
      for index in 2..<targets.count where exists(targets[index]) {
        try moveToTrash(targets[index])
        changed.append(index)
      }
      if let expectedKey { try verifyYilaiConfiguration(expectedKey: expectedKey) }
      guard !exists(paths.auth), !exists(paths.disabledAuth) else {
        throw SwitcherError.message("认证文件被重新创建，请完全退出 Codex 和 CC-Switch 后重试")
      }
    } catch {
      let originalError = error
      var rollbackFailed = false
      for index in changed.reversed() {
        let snapshot = snapshots[index]
        do {
          if let data = snapshot.data { try writeAtomic(data, to: snapshot.url) }
          else if exists(snapshot.url) { try files.removeItem(at: snapshot.url) }
        } catch { rollbackFailed = true }
      }
      if rollbackFailed {
        throw SwitcherError.message("切换失败且回滚不完整，请检查文件权限后重试")
      }
      throw originalError
    }
    // Do not recursively delete unknown files in the legacy backup directory.
    if let contents = try? files.contentsOfDirectory(atPath: paths.backup.path), contents.isEmpty {
      try? files.removeItem(at: paths.backup)
    }
  }

  private func buildConfiguration(existing: String, key: String, official: Bool) throws -> String {
    guard !existing.utf8.contains(0) else {
      throw SwitcherError.message("config.toml 包含无效空字符，未修改文件。")
    }
    var error: UnsafeMutablePointer<CChar>?
    let output = existing.withCString { existing in
      key.withCString { key in
        paths.modelCatalog.path.withCString { catalog in
          yilai_rewrite_config(existing, key, catalog, official ? 1 : 0, &error)
        }
      }
    }
    defer {
      if let output { yilai_config_free(output) }
      if let error { yilai_config_free(error) }
    }
    guard let output else {
      let detail = error.map { String(cString: $0) } ?? "Invalid configuration."
      throw SwitcherError.message("无法生成配置：\(detail)")
    }
    return String(cString: output)
  }

  private func verifyYilaiConfiguration(expectedKey: String) throws {
    let contents = try read(paths.config)
    let valid = contents.withCString { contents in
      expectedKey.withCString { key in
        paths.modelCatalog.path.withCString { catalog in
          yilai_verify_config(contents, key, catalog) != 0
        }
      }
    }
    guard !contents.utf8.contains(0), valid, try read(paths.modelCatalog) == YilaiModelCatalog.json else {
      throw SwitcherError.message("写入后校验失败：易来配置或模型目录不完整。")
    }
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
  var coreError: UnsafeMutablePointer<CChar>?
  let corePassed = yilai_config_self_test(&coreError) != 0
  let detail = coreError.map { String(cString: $0) } ?? "Structured configuration self-test failed."
  if let coreError { yilai_config_free(coreError) }
  guard corePassed else { throw SwitcherError.message(detail) }
  let catalog = try JSONSerialization.jsonObject(with: Data(YilaiModelCatalog.json.utf8)) as? [String: Any]
  guard let models = catalog?["models"] as? [[String: Any]],
    models.compactMap({ $0["slug"] as? String }) == ["gpt-5.6-sol", "gpt-5.6-terra", "gpt-6-astra"],
    models.allSatisfy({ ($0["visibility"] as? String) == "list" && ($0["supported_in_api"] as? Bool) == true })
  else { throw SwitcherError.message("自测失败：内嵌模型目录必须恰好包含三个可见模型") }
  let root = FileManager.default.temporaryDirectory.appendingPathComponent(
    "YilaiCodexSwitcher-swift-\(UUID().uuidString)", isDirectory: true)
  defer { try? FileManager.default.removeItem(at: root) }
  try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
  // Verify retained bytes and collision handling through the production trash helper.
  let trashProbe = root.appendingPathComponent("trash-proof-\(UUID().uuidString).json")
  var recycled: [URL] = []
  for value in ["first synthetic credential", "second synthetic credential"] {
    let bytes = Data(value.utf8)
    try bytes.write(to: trashProbe)
    guard let destination = try moveToTrash(trashProbe),
      !FileManager.default.fileExists(atPath: trashProbe.path),
      try Data(contentsOf: destination) == bytes,
      !recycled.contains(destination)
    else { throw SwitcherError.message("自测失败：废纸篓必须保留文件内容及同名副本") }
    recycled.append(destination)
  }
  guard try Data(contentsOf: recycled[0]) == Data("first synthetic credential".utf8)
  else { throw SwitcherError.message("自测失败：废纸篓旧副本被覆盖") }
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
  guard archivedAuths.isEmpty, !FileManager.default.fileExists(
    atPath: root.appendingPathComponent("auth.json").path)
  else {
    throw SwitcherError.message("自测失败：重复切换不应备份登录")
  }
  try service.switchToOfficial()
  let official = try String(contentsOf: root.appendingPathComponent("config.toml"), encoding: .utf8)
  guard try service.mode() == .official,
    official.contains("model = \"gpt-5.6-terra\""),
    official.contains("cli_auth_credentials_store = \"file\""),
    official.contains("image_generation = true"),
    !official.contains("model_provider"),
    !official.contains("model_catalog_json"),
    !official.contains("[model_providers."),
    !official.contains("sk-new-key"),
    official.contains("[plugins.\"browser@openai-bundled\"]"),
    !FileManager.default.fileExists(atPath: root.appendingPathComponent("auth.json").path),
    !FileManager.default.fileExists(atPath: root.appendingPathComponent("auth.json.yilai-disabled").path),
    !FileManager.default.fileExists(atPath: root.appendingPathComponent("yilai-switcher-backup").path)
  else { throw SwitcherError.message("自测失败：官方配置应保留当前设置并重新登录") }

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
  guard keyringOfficial.contains("cli_auth_credentials_store = \"file\""),
    keyringOfficial.contains("model = \"gpt-5.6-terra\""),
    keyringOfficial.contains("[plugins.\"browser@openai-bundled\"]")
  else { throw SwitcherError.message("自测失败：官方不应恢复旧 Keychain 登录设置") }

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
  try FileManager.default.createDirectory(at: manifestURL.deletingLastPathComponent(), withIntermediateDirectories: true)
  try Data("invalid old manifest".utf8).write(to: manifestURL)
  try Data(originalConfig.utf8).write(to: upgradeRoot.appendingPathComponent("yilai-switcher-backup/config.toml"))
  try Data(originalAuth.utf8).write(to: upgradeRoot.appendingPathComponent("auth.json.yilai-disabled"))
  let oldConfig = try String(contentsOf: upgradeRoot.appendingPathComponent("config.toml"), encoding: .utf8)
    .components(separatedBy: .newlines).filter { !$0.hasPrefix("model_catalog_json") }.joined(separator: "\n")
  try ("model_catalog_json = \"old-catalog.json\"\n" + oldConfig)
    .data(using: .utf8)!.write(to: upgradeRoot.appendingPathComponent("config.toml"))
  try "old catalog containing luna".data(using: .utf8)!.write(to: upgradeRoot.appendingPathComponent("yilai-model-catalog.json"))
  try "old cache containing luna".data(using: .utf8)!.write(to: upgradeRoot.appendingPathComponent("models_cache.json"))
  try upgradeService.switchToYilai(key: "sk-upgraded-key")
  let upgraded = try String(contentsOf: upgradeRoot.appendingPathComponent("config.toml"), encoding: .utf8)
  guard upgraded.components(separatedBy: "model_catalog_json").count == 2,
    !upgraded.contains("old-catalog.json"), upgraded.contains("sk-upgraded-key"),
    try String(contentsOf: upgradeRoot.appendingPathComponent("yilai-model-catalog.json"), encoding: .utf8) == YilaiModelCatalog.json,
    !FileManager.default.fileExists(atPath: manifestURL.path),
    !FileManager.default.fileExists(atPath: upgradeRoot.appendingPathComponent("auth.json.yilai-disabled").path),
    try String(contentsOf: upgradeRoot.appendingPathComponent("models_cache.json"), encoding: .utf8) == "old cache containing luna"
  else { throw SwitcherError.message("自测失败：旧用户目录覆盖与旧备份清理") }
  try upgradeService.switchToOfficial()
  let upgradedOfficial = try String(contentsOf: upgradeRoot.appendingPathComponent("config.toml"), encoding: .utf8)
  guard !upgradedOfficial.contains("model_catalog_json"),
    !FileManager.default.fileExists(atPath: upgradeRoot.appendingPathComponent("auth.json").path)
  else { throw SwitcherError.message("自测失败：旧用户升级后切回官方") }

  for officialMode in [false, true] {
    let conflictRoot = root.appendingPathComponent(officialMode ? "conflict-official" : "conflict-yilai")
    let conflictPaths = CodexPaths(codex: conflictRoot)
    try FileManager.default.createDirectory(at: conflictPaths.backup, withIntermediateDirectories: true)
    try Data(originalConfig.utf8).write(to: conflictPaths.config)
    try Data(originalAuth.utf8).write(to: conflictPaths.auth)
    try Data("old auth".utf8).write(to: conflictPaths.disabledAuth)
    try Data("invalid manifest".utf8).write(to: conflictPaths.manifest)
    try Data("obsolete settings".utf8).write(to: conflictPaths.backupConfig)
    let archive = conflictRoot.appendingPathComponent("auth.json.yilai-session-test")
    try Data("old archived auth".utf8).write(to: archive)
    let unrelated = conflictPaths.backup.appendingPathComponent("unrelated.txt")
    try Data("keep".utf8).write(to: unrelated)
    let conflictService = CodexConfigurationService(codexDirectory: conflictRoot)
    if officialMode { try conflictService.switchToOfficial() }
    else { try conflictService.switchToYilai(key: "sk-conflict") }
    guard try conflictService.mode() == (officialMode ? .official : .yilai),
      !FileManager.default.fileExists(atPath: conflictPaths.auth.path),
      !FileManager.default.fileExists(atPath: conflictPaths.disabledAuth.path),
      !FileManager.default.fileExists(atPath: conflictPaths.manifest.path),
      !FileManager.default.fileExists(atPath: conflictPaths.backupConfig.path),
      !FileManager.default.fileExists(atPath: archive.path),
      try Data(contentsOf: unrelated) == Data("keep".utf8),
      try String(contentsOf: conflictPaths.config, encoding: .utf8).contains("[plugins.")
    else { throw SwitcherError.message("自测失败：旧备份冲突应清理且保留当前通用设置") }
  }

  let rollbackRoot = root.appendingPathComponent("catalog-rollback", isDirectory: true)
  try FileManager.default.createDirectory(at: rollbackRoot, withIntermediateDirectories: true)
  let rollbackPaths = CodexPaths(codex: rollbackRoot)
  try Data(originalConfig.utf8).write(to: rollbackPaths.config)
  try Data("previous catalog".utf8).write(to: rollbackPaths.modelCatalog)
  try Data(originalAuth.utf8).write(to: rollbackPaths.auth)
  try Data("locked old auth".utf8).write(to: rollbackPaths.disabledAuth)
  // Deny deletion after the active auth was removed, exercising partial cleanup rollback.
  guard chflags(rollbackPaths.disabledAuth.path, UInt32(UF_IMMUTABLE)) == 0 else {
    throw SwitcherError.message("自测失败：无法锁定测试文件")
  }
  defer { _ = chflags(rollbackPaths.disabledAuth.path, 0) }
  let rollbackService = CodexConfigurationService(codexDirectory: rollbackRoot)
  for officialMode in [false, true] {
    var failed = false
    do {
      if officialMode { try rollbackService.switchToOfficial() }
      else { try rollbackService.switchToYilai(key: "sk-after-failure") }
    } catch { failed = true }
    guard failed, try Data(contentsOf: rollbackPaths.config) == Data(originalConfig.utf8),
      try Data(contentsOf: rollbackPaths.modelCatalog) == Data("previous catalog".utf8),
      try Data(contentsOf: rollbackPaths.auth) == Data(originalAuth.utf8),
      try Data(contentsOf: rollbackPaths.disabledAuth) == Data("locked old auth".utf8)
    else { throw SwitcherError.message("自测失败：清理失败应回滚配置、目录与登录") }
  }
}
