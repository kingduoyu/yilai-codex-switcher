import Foundation
import CryptoKit
import Darwin
import ConfigRewrite
import OperationGuard

struct SoftwareRelease: Decodable {
    let available: Bool
    let version: String
    let notes: String
    let url: URL?
    let sha256: String?
    let size: Int?

    var bundleVersion: String { version.hasPrefix("v") ? String(version.dropFirst()) : version }
}

struct ModelChannel: Decodable {
    let schemaVersion: Int
    let revision: Int
    let url: URL
    let sha256: String

    private enum CodingKeys: String, CodingKey {
        case schemaVersion = "schema_version"
        case revision, url, sha256
    }
}

struct CatalogModel: Decodable, Identifiable {
    let slug: String
    let displayName: String
    var id: String { slug }

    private enum CodingKeys: String, CodingKey {
        case slug
        case displayName = "display_name"
    }
}

struct ValidatedCatalog {
    let data: Data
    let models: [CatalogModel]
    let ids: Set<String>

    func requireIncluding(_ previous: Set<String>) throws {
        guard previous.isSubset(of: ids) else {
            throw AppError(message: "模型目录会移除已有模型，已拒绝更新。")
        }
    }
}

enum UpdateProtocol {
    static let version = "3.4.0"
    static let bundleID = "com.yilai.codex-switcher"
    static let asset = "YilaiCodexSwitcher-macOS-universal.zip"
    static let releaseURL = URL(string: "https://api.github.com/repos/kingduoyu/yilai-codex-switcher/releases/latest")!
    static let channelURL = URL(string: "https://raw.githubusercontent.com/kingduoyu/yilai-codex-switcher/main/model-channel.json")!
    static let catalogLimit = 4 * 1024 * 1024
    static let softwareLimit = 100 * 1024 * 1024
    static let assetHosts: Set<String> = ["github.com", "release-assets.githubusercontent.com", "objects.githubusercontent.com", "github-releases.githubusercontent.com"]

    private static func normalized(_ data: Data, limit: Int,
        using transform: (UnsafePointer<CChar>, UnsafeMutablePointer<UnsafeMutablePointer<CChar>?>) -> UnsafeMutablePointer<CChar>?) throws -> Data {
        guard !data.isEmpty, data.count <= limit,
              let text = String(data: data, encoding: .utf8), !text.utf8.contains(0) else {
            throw AppError(message: "更新数据大小或 UTF-8 编码无效。")
        }
        var failure: UnsafeMutablePointer<CChar>?
        let output = text.withCString { transform($0, &failure) }
        defer {
            if let output { yilai_config_free(output) }
            if let failure { yilai_config_free(failure) }
        }
        guard let output else {
            throw AppError(message: failure.map { String(cString: $0) } ?? "更新协议校验失败。")
        }
        return Data(String(cString: output).utf8)
    }

    static func release(_ data: Data) throws -> SoftwareRelease {
        let result = try normalized(data, limit: 2 * 1024 * 1024) { input, failure in
            asset.withCString { yilai_update_release(input, $0, failure) }
        }
        let release = try JSONDecoder().decode(SoftwareRelease.self, from: result)
        if release.available {
            guard let url = release.url, let size = release.size, let digest = release.sha256,
                  size > 0, size <= softwareLimit, validDigest(digest),
                  url.absoluteString == "https://github.com/kingduoyu/yilai-codex-switcher/releases/download/\(release.version)/\(asset)" else {
                throw AppError(message: "软件更新附件无效。")
            }
        }
        return release
    }

    static func channel(_ data: Data) throws -> ModelChannel {
        let result = try normalized(data, limit: 256 * 1024) { yilai_update_channel($0, $1) }
        return try JSONDecoder().decode(ModelChannel.self, from: result)
    }

    static func catalog(_ data: Data) throws -> ValidatedCatalog {
        let result = try normalized(data, limit: catalogLimit) { yilai_validate_catalog($0, $1) }
        struct Catalog: Decodable { let models: [CatalogModel] }
        let models = try JSONDecoder().decode(Catalog.self, from: result).models
        let ids = Set(models.map(\.slug))
        guard !ids.isEmpty, ids.count == models.count else {
            throw AppError(message: "模型标识无效或重复。")
        }
        return ValidatedCatalog(data: data, models: models, ids: ids)
    }

    static func builtinCatalog() throws -> ValidatedCatalog {
        try catalog(Data(String(cString: yilai_model_catalog()).utf8))
    }

    static func digest(_ data: Data) -> String {
        SHA256.hash(data: data).map { String(format: "%02x", $0) }.joined()
    }

    private static func validDigest(_ value: String) -> Bool {
        value.utf8.count == 64 && value.utf8.allSatisfy { (48...57).contains($0) || (97...102).contains($0) }
    }

    static func verifyDigest(_ data: Data, expected: String) throws {
        guard validDigest(expected), digest(data) == expected else {
            throw AppError(message: "SHA-256 校验失败，未安装或写入更新。")
        }
    }

    static func allowed(_ url: URL, hosts: Set<String>) -> Bool {
        url.scheme == "https" && hosts.contains(url.host?.lowercased() ?? "") &&
            (url.port == nil || url.port == 443) && url.user == nil && url.password == nil
    }
}

// Each request owns a serial delegate queue and bounds bytes while receiving
// them, including responses without Content-Length and HTTPS redirects.
private final class BoundedRequest: NSObject, URLSessionDataDelegate, @unchecked Sendable {
    private let hosts: Set<String>
    private let limit: Int
    private let expectedSize: Int?
    private let state = NSLock()
    private var cancelled = false
    private var session: URLSession?
    private var task: URLSessionDataTask?
    private var continuation: CheckedContinuation<Data, Error>?
    private var bytes = Data()
    private var failure: Error?
    private var redirects = 0

    init(hosts: Set<String>, limit: Int, expectedSize: Int?) {
        self.hosts = hosts
        self.limit = limit
        self.expectedSize = expectedSize
    }

    static func get(_ url: URL, hosts: Set<String>, limit: Int, expectedSize: Int? = nil) async throws -> Data {
        guard UpdateProtocol.allowed(url, hosts: hosts) else {
            throw AppError(message: "更新地址不是允许的 GitHub HTTPS 地址。")
        }
        let request = BoundedRequest(hosts: hosts, limit: limit, expectedSize: expectedSize)
        return try await withTaskCancellationHandler(operation: {
            try await withCheckedThrowingContinuation { request.start(url, continuation: $0) }
        }, onCancel: { request.cancel() })
    }

    private func start(_ url: URL, continuation: CheckedContinuation<Data, Error>) {
        state.lock()
        guard !cancelled else {
            state.unlock()
            continuation.resume(throwing: CancellationError())
            return
        }
        self.continuation = continuation
        let configuration = URLSessionConfiguration.ephemeral
        configuration.timeoutIntervalForRequest = 30
        configuration.timeoutIntervalForResource = expectedSize == nil ? 90 : 300
        configuration.httpShouldSetCookies = false
        configuration.httpCookieStorage = nil
        configuration.urlCredentialStorage = nil
        configuration.urlCache = nil
        let queue = OperationQueue()
        queue.maxConcurrentOperationCount = 1
        let session = URLSession(configuration: configuration, delegate: self, delegateQueue: queue)
        self.session = session
        var request = URLRequest(url: url, cachePolicy: .reloadIgnoringLocalCacheData)
        request.setValue("YilaiCodexSwitcher/\(UpdateProtocol.version)", forHTTPHeaderField: "User-Agent")
        request.setValue("identity", forHTTPHeaderField: "Accept-Encoding")
        let task = session.dataTask(with: request)
        self.task = task
        state.unlock()
        task.resume()
    }

    private func cancel() {
        state.lock()
        cancelled = true
        let task = task
        state.unlock()
        task?.cancel()
    }

    func urlSession(_ session: URLSession, task: URLSessionTask, willPerformHTTPRedirection response: HTTPURLResponse,
                    newRequest request: URLRequest, completionHandler: @escaping (URLRequest?) -> Void) {
        redirects += 1
        guard redirects <= 5, let url = request.url, UpdateProtocol.allowed(url, hosts: hosts) else {
            failure = AppError(message: "更新下载重定向到不允许的地址，已停止。")
            completionHandler(nil)
            task.cancel()
            return
        }
        completionHandler(request)
    }

    func urlSession(_ session: URLSession, dataTask: URLSessionDataTask, didReceive response: URLResponse,
                    completionHandler: @escaping (URLSession.ResponseDisposition) -> Void) {
        guard let response = response as? HTTPURLResponse, response.statusCode == 200,
              let url = response.url, UpdateProtocol.allowed(url, hosts: hosts) else {
            failure = AppError(message: "更新服务未返回有效内容（HTTP \((response as? HTTPURLResponse)?.statusCode ?? 0)）。")
            completionHandler(.cancel)
            return
        }
        let length = response.expectedContentLength
        guard length <= Int64(limit), expectedSize == nil || length < 0 || length == Int64(expectedSize!) else {
            failure = AppError(message: "更新下载大小与发布记录不符或超过限制。")
            completionHandler(.cancel)
            return
        }
        completionHandler(.allow)
    }

    func urlSession(_ session: URLSession, dataTask: URLSessionDataTask, didReceive data: Data) {
        guard failure == nil else { return }
        let maximum = min(limit, expectedSize ?? limit)
        guard data.count <= maximum - bytes.count else {
            failure = AppError(message: "更新下载超过允许的大小，已停止。")
            dataTask.cancel()
            return
        }
        bytes.append(data)
    }

    func urlSession(_ session: URLSession, task: URLSessionTask, didCompleteWithError error: Error?) {
        state.lock()
        let completion = continuation
        continuation = nil
        self.task = nil
        self.session = nil
        let wasCancelled = cancelled
        state.unlock()
        defer { session.invalidateAndCancel() }
        if let failure { completion?.resume(throwing: failure) }
        else if wasCancelled { completion?.resume(throwing: CancellationError()) }
        else if let error { completion?.resume(throwing: error) }
        else if bytes.isEmpty || (expectedSize != nil && bytes.count != expectedSize) {
            completion?.resume(throwing: AppError(message: "更新下载不完整，未做修改。"))
        } else { completion?.resume(returning: bytes) }
    }
}

final class UpdateService {
    func checkRelease() async throws -> SoftwareRelease {
        let data = try await BoundedRequest.get(UpdateProtocol.releaseURL, hosts: ["api.github.com"], limit: 2 * 1024 * 1024)
        return try UpdateProtocol.release(data)
    }

    func updateCatalog(using service: PlatformService) async throws -> OperationOutcome {
        let channelData = try await BoundedRequest.get(UpdateProtocol.channelURL, hosts: ["raw.githubusercontent.com"], limit: 256 * 1024)
        let channel = try UpdateProtocol.channel(channelData)
        let data = try await BoundedRequest.get(channel.url, hosts: ["raw.githubusercontent.com"], limit: UpdateProtocol.catalogLimit)
        return try await withCheckedThrowingContinuation { continuation in
            DispatchQueue.global(qos: .userInitiated).async {
                continuation.resume(with: Result { try service.updateCatalog(data, sha256: channel.sha256) })
            }
        }
    }

    func prepareInstallation(_ release: SoftwareRelease) async throws -> PreparedInstallation {
        let destination = try UpdateInstaller.installationURL()
        guard release.available, let url = release.url, let size = release.size, let sha256 = release.sha256 else {
            throw AppError(message: "没有可安装的软件更新。")
        }
        let data = try await BoundedRequest.get(url, hosts: UpdateProtocol.assetHosts, limit: UpdateProtocol.softwareLimit, expectedSize: size)
        return try await withCheckedThrowingContinuation { continuation in
            DispatchQueue.global(qos: .userInitiated).async {
                continuation.resume(with: Result {
                    try UpdateProtocol.verifyDigest(data, expected: sha256)
                    return try UpdateInstaller.prepare(data, release: release, destination: destination)
                })
            }
        }
    }
}

struct PreparedInstallation {
    fileprivate let stage: URL
    fileprivate let destination: URL
    fileprivate let candidate: URL
    fileprivate let backup: URL
    fileprivate let version: String
    fileprivate let previousVersion: String

    func launch() async throws {
        try await withCheckedThrowingContinuation { (continuation: CheckedContinuation<Void, Error>) in
            DispatchQueue.global(qos: .userInitiated).async {
                continuation.resume(with: Result { try UpdateInstaller.launch(self) })
            }
        }
    }
}

private struct InstallationResult: Codable {
    let success: Bool
    let pending: Bool
    let version: String
    let message: String
}

private struct HelperStatus: Codable {
    let ready: Bool
    let helperPID: Int32
    let parentPID: Int32
    let message: String
}

enum UpdateInstaller {
    private static let files = FileManager.default
    private static let moveMessage = "请先将应用移到可写的 Applications（应用程序）目录，再安装软件更新。"

    static func installationURL() throws -> URL {
        let url = Bundle.main.bundleURL.standardizedFileURL
        guard url.pathExtension == "app", Bundle.main.bundleIdentifier == UpdateProtocol.bundleID else {
            throw AppError(message: moveMessage)
        }
        try writableInstallation(url)
        return url
    }

    private static func writableInstallation(_ url: URL) throws {
        let parent = url.deletingLastPathComponent()
        guard let values = try? url.resourceValues(forKeys: [.isDirectoryKey, .isSymbolicLinkKey, .volumeIsReadOnlyKey]),
              values.isDirectory == true, values.isSymbolicLink != true, values.volumeIsReadOnly == false,
              url.resolvingSymlinksInPath() == url,
              files.isWritableFile(atPath: url.path), files.isWritableFile(atPath: parent.path) else {
            throw AppError(message: moveMessage)
        }
    }

    private static func resultURL() throws -> URL {
        let support = files.homeDirectoryForCurrentUser.appendingPathComponent("Library/Application Support", isDirectory: true)
            .appendingPathComponent(UpdateProtocol.bundleID, isDirectory: true)
        if (try? files.destinationOfSymbolicLink(atPath: support.path)) != nil {
            throw AppError(message: "软件更新结果目录不能是符号链接。")
        }
        try files.createDirectory(at: support, withIntermediateDirectories: true, attributes: [.posixPermissions: 0o700])
        let info = try files.attributesOfItem(atPath: support.path)
        guard info[.type] as? FileAttributeType == .typeDirectory,
              (info[.ownerAccountID] as? NSNumber)?.uint32Value == getuid() else {
            throw AppError(message: "软件更新结果目录不属于当前用户。")
        }
        try files.setAttributes([.posixPermissions: 0o700], ofItemAtPath: support.path)
        return support.appendingPathComponent("update-result.json")
    }

    private static func writeResult(_ result: InstallationResult, to url: URL) throws {
        try regularFile(url, mustExist: false)
        try JSONEncoder().encode(result).write(to: url, options: .atomic)
        try files.setAttributes([.posixPermissions: 0o600], ofItemAtPath: url.path)
    }

    static func consumeResult() -> OperationOutcome? {
        guard let url = try? resultURL(),
              (try? regularFile(url)) != nil,
              let data = try? Data(contentsOf: url), data.count <= 64 * 1024,
              let result = try? JSONDecoder().decode(InstallationResult.self, from: data) else { return nil }
        try? files.removeItem(at: url)
        let installedVersion = Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? UpdateProtocol.version
        if result.pending || (result.success && result.version != installedVersion) {
            return OperationOutcome(message: "上次软件更新未完成。\(result.message)", warning: true)
        }
        return OperationOutcome(message: result.message, warning: !result.success)
    }

    private static func regularFile(_ url: URL, mustExist: Bool = true) throws {
        if (try? files.destinationOfSymbolicLink(atPath: url.path)) != nil {
            throw AppError(message: "更新文件不能是符号链接：\(url.path)")
        }
        guard files.fileExists(atPath: url.path) else {
            if mustExist { throw AppError(message: "更新文件不存在：\(url.lastPathComponent)") }
            return
        }
        let info = try url.resourceValues(forKeys: [.isRegularFileKey, .isSymbolicLinkKey])
        guard info.isRegularFile == true, info.isSymbolicLink != true else {
            throw AppError(message: "更新文件不是普通文件：\(url.lastPathComponent)")
        }
    }

    // File-backed output avoids pipe deadlocks; time and output limits also
    // apply to codesign, archive inspection, self-tests and LaunchServices.
    private static func tool(_ executable: String, _ arguments: [String], in stage: URL,
                             timeout: TimeInterval, environment: [String: String]? = nil) throws -> Data {
        let output = stage.appendingPathComponent("tool-\(UUID().uuidString).log")
        guard files.createFile(atPath: output.path, contents: nil, attributes: [.posixPermissions: 0o600]) else {
            throw AppError(message: "无法创建软件更新校验临时文件。")
        }
        let handle = try FileHandle(forWritingTo: output)
        defer { try? handle.close(); try? files.removeItem(at: output) }
        let process = Process()
        process.executableURL = URL(fileURLWithPath: executable)
        process.arguments = arguments
        process.currentDirectoryURL = stage
        process.environment = environment ?? ["PATH": "/usr/bin:/bin:/usr/sbin:/sbin", "LC_ALL": "C"]
        process.standardInput = FileHandle.nullDevice
        process.standardOutput = handle
        process.standardError = handle
        try process.run()
        let deadline = ProcessInfo.processInfo.systemUptime + timeout
        var limitReached = false
        while process.isRunning {
            let count = (try? files.attributesOfItem(atPath: output.path)[.size] as? NSNumber)?.intValue ?? 0
            if ProcessInfo.processInfo.systemUptime >= deadline || count > 1024 * 1024 {
                limitReached = true
                process.terminate()
                let grace = ProcessInfo.processInfo.systemUptime + 1
                while process.isRunning && ProcessInfo.processInfo.systemUptime < grace { Thread.sleep(forTimeInterval: 0.05) }
                if process.isRunning { _ = Darwin.kill(process.processIdentifier, SIGKILL) }
                break
            }
            Thread.sleep(forTimeInterval: 0.05)
        }
        let stopped = ProcessInfo.processInfo.systemUptime + 2
        while process.isRunning && ProcessInfo.processInfo.systemUptime < stopped { Thread.sleep(forTimeInterval: 0.05) }
        guard !process.isRunning else { throw AppError(message: "软件更新校验进程无法结束。") }
        process.waitUntilExit()
        guard !limitReached else { throw AppError(message: "软件更新校验超时或输出超过限制：\(URL(fileURLWithPath: executable).lastPathComponent)") }
        let data = try Data(contentsOf: output)
        guard data.count <= 1024 * 1024, process.terminationStatus == 0 else {
            throw AppError(message: "软件更新校验失败：\(URL(fileURLWithPath: executable).lastPathComponent)（退出码 \(process.terminationStatus)）。")
        }
        return data
    }

    private static func bundleInfo(_ app: URL) throws -> [String: Any] {
        let plist = app.appendingPathComponent("Contents/Info.plist")
        try regularFile(plist)
        let data = try Data(contentsOf: plist)
        guard data.count <= 64 * 1024,
              let info = try PropertyListSerialization.propertyList(from: data, format: nil) as? [String: Any],
              info["CFBundleIdentifier"] as? String == UpdateProtocol.bundleID,
              info["CFBundlePackageType"] as? String == "APPL",
              info["CFBundleExecutable"] as? String == "YilaiCodexSwitcherMac" else {
            throw AppError(message: "更新应用的标识或可执行文件无效。")
        }
        return info
    }

    private static func validateBundle(_ app: URL, version: String, in stage: URL, selfTest: Bool) throws {
        guard app.pathExtension == "app", app.resolvingSymlinksInPath() == app,
              try bundleInfo(app)["CFBundleShortVersionString"] as? String == version else {
            throw AppError(message: "更新应用的版本与发布记录不一致。")
        }
        var enumerationError: Error?
        guard let iterator = files.enumerator(at: app, includingPropertiesForKeys: [.isSymbolicLinkKey, .isRegularFileKey, .fileSizeKey],
                                              options: [], errorHandler: { _, error in enumerationError = error; return false }) else {
            throw AppError(message: "无法检查更新应用文件。")
        }
        var count = 0
        var total = 0
        for case let file as URL in iterator {
            let info = try file.resourceValues(forKeys: [.isSymbolicLinkKey, .isRegularFileKey, .isDirectoryKey, .fileSizeKey])
            count += 1
            guard info.isSymbolicLink != true, info.isRegularFile == true || info.isDirectory == true,
                  !file.path.unicodeScalars.contains(where: { CharacterSet.controlCharacters.contains($0) }) else {
                throw AppError(message: "更新应用包含不安全的文件。")
            }
            let size = info.isRegularFile == true ? info.fileSize ?? 0 : 0
            guard size >= 0, count <= 10_000, size <= 400 * 1024 * 1024 - total else {
                throw AppError(message: "解压后的更新应用超过允许的大小。")
            }
            total += size
        }
        if let enumerationError { throw enumerationError }
        let executable = app.appendingPathComponent("Contents/MacOS/YilaiCodexSwitcherMac")
        try regularFile(executable)
        guard files.isExecutableFile(atPath: executable.path) else { throw AppError(message: "更新应用不可执行。") }
        _ = try tool("/usr/bin/codesign", ["--verify", "--deep", "--strict", app.path], in: stage, timeout: 20)
        _ = try tool("/usr/bin/lipo", [executable.path, "-verify_arch", "x86_64", "arm64"], in: stage, timeout: 10)
        if selfTest {
            let sandbox = stage.appendingPathComponent("self-test", isDirectory: true)
            let temporary = sandbox.appendingPathComponent("tmp", isDirectory: true)
            try files.createDirectory(at: temporary, withIntermediateDirectories: true, attributes: [.posixPermissions: 0o700])
            let environment = ["PATH": "/usr/bin:/bin:/usr/sbin:/sbin", "LC_ALL": "C", "HOME": sandbox.path,
                               "CFFIXED_USER_HOME": sandbox.path, "CODEX_HOME": sandbox.appendingPathComponent(".codex").path,
                               "TMPDIR": temporary.path + "/"]
            _ = try tool(executable.path, ["--self-test"], in: stage, timeout: 60, environment: environment)
        }
    }

    static func prepare(_ data: Data, release: SoftwareRelease, destination: URL) throws -> PreparedInstallation {
        try writableInstallation(destination)
        let previous = try bundleInfo(destination)["CFBundleShortVersionString"] as? String ?? UpdateProtocol.version
        let parent = destination.deletingLastPathComponent()
        let stage = parent.appendingPathComponent(".yilai-update-\(UUID().uuidString)", isDirectory: true)
        try files.createDirectory(at: stage, withIntermediateDirectories: false, attributes: [.posixPermissions: 0o700])
        var prepared = false
        defer { if !prepared { try? files.removeItem(at: stage) } }
        let archive = stage.appendingPathComponent("update.zip")
        try data.write(to: archive, options: .atomic)
        let listing = try tool("/usr/bin/unzip", ["-Z1", archive.path], in: stage, timeout: 10)
        guard let names = String(data: listing, encoding: .utf8) else { throw AppError(message: "更新压缩包文件名编码无效。") }
        var appNames = Set<String>()
        let entries = names.split(separator: "\n", omittingEmptySubsequences: true)
        guard !entries.isEmpty, entries.count <= 10_000 else { throw AppError(message: "更新压缩包文件数量无效。") }
        for entry in entries {
            let parts = entry.split(separator: "/", omittingEmptySubsequences: true)
            guard !entry.hasPrefix("/"), !entry.contains("\\"), !entry.unicodeScalars.contains(where: { CharacterSet.controlCharacters.contains($0) }),
                  !parts.contains(".."), !parts.contains("."), let first = parts.first,
                  first == "__MACOSX" || first.hasSuffix(".app") else {
                throw AppError(message: "更新压缩包包含不安全的路径。")
            }
            if first.hasSuffix(".app") { appNames.insert(String(first)) }
        }
        guard appNames.count == 1, let appName = appNames.first else { throw AppError(message: "更新压缩包必须只包含一个应用。") }
        let attributes = try tool("/usr/bin/unzip", ["-Z", "-l", archive.path], in: stage, timeout: 10)
        guard let detail = String(data: attributes, encoding: .utf8),
              !detail.split(separator: "\n").contains(where: { $0.hasPrefix("l") }) else {
            throw AppError(message: "更新压缩包包含符号链接，已拒绝安装。")
        }
        let unpacked = stage.appendingPathComponent("unpacked", isDirectory: true)
        try files.createDirectory(at: unpacked, withIntermediateDirectories: false, attributes: [.posixPermissions: 0o700])
        _ = try tool("/usr/bin/ditto", ["-x", "-k", archive.path, unpacked.path], in: stage, timeout: 60)
        let candidate = unpacked.appendingPathComponent(appName, isDirectory: true)
        try validateBundle(candidate, version: release.bundleVersion, in: stage, selfTest: true)
        let helper = stage.appendingPathComponent("update-helper")
        let originalExecutable = destination.appendingPathComponent("Contents/MacOS/YilaiCodexSwitcherMac")
        try regularFile(originalExecutable)
        try files.copyItem(at: originalExecutable, to: helper)
        try files.setAttributes([.posixPermissions: 0o700], ofItemAtPath: helper.path)
        let backup = parent.appendingPathComponent("\(destination.deletingPathExtension().lastPathComponent).backup-\(UUID().uuidString).app", isDirectory: true)
        prepared = true
        return PreparedInstallation(stage: stage, destination: destination, candidate: candidate, backup: backup,
                                    version: release.bundleVersion, previousVersion: previous)
    }

    fileprivate static func launch(_ installation: PreparedInstallation) throws {
        let result = try resultURL()
        try writeResult(InstallationResult(success: false, pending: true, version: installation.version,
            message: "旧应用备份位置：\(installation.backup.path)。暂存目录：\(installation.stage.path)。"), to: result)
        let process = Process()
        process.executableURL = installation.stage.appendingPathComponent("update-helper")
        process.arguments = ["--update-helper", String(getpid()), installation.destination.path, installation.candidate.path,
                             installation.backup.path, result.path, installation.version, installation.previousVersion]
        process.currentDirectoryURL = installation.stage
        process.standardInput = FileHandle.nullDevice
        process.standardOutput = FileHandle.nullDevice
        process.standardError = FileHandle.nullDevice
        do {
            try process.run()
            let ready = installation.stage.appendingPathComponent("helper-status.json")
            let deadline = ProcessInfo.processInfo.systemUptime + 40
            while ProcessInfo.processInfo.systemUptime < deadline {
                if (try? regularFile(ready)) != nil {
                    let data = try Data(contentsOf: ready)
                    guard data.count <= 64 * 1024 else { throw AppError(message: "软件更新助手就绪信息无效。") }
                    let status = try JSONDecoder().decode(HelperStatus.self, from: data)
                    guard status.helperPID == process.processIdentifier, status.parentPID == getpid() else {
                        throw AppError(message: "软件更新助手身份不匹配。")
                    }
                    guard status.ready else { throw AppError(message: status.message) }
                    guard process.isRunning else { throw AppError(message: "软件更新助手已退出，当前应用不会退出。") }
                    return
                }
                if !process.isRunning { break }
                Thread.sleep(forTimeInterval: 0.05)
            }
            throw AppError(message: "软件更新助手未就绪，当前应用不会退出。")
        }
        catch {
            if process.isRunning {
                process.terminate()
                let deadline = ProcessInfo.processInfo.systemUptime + 1
                while process.isRunning && ProcessInfo.processInfo.systemUptime < deadline { Thread.sleep(forTimeInterval: 0.05) }
                if process.isRunning { _ = Darwin.kill(process.processIdentifier, SIGKILL) }
            }
            try? writeResult(InstallationResult(success: false, pending: false, version: installation.version,
                message: "软件更新助手未就绪：\(error.localizedDescription)。当前应用保持原样。暂存目录：\(installation.stage.path)。"), to: result)
            throw error
        }
    }

    private static func helperStatus(ready: Bool, parentPID: Int32, message: String, in stage: URL) throws {
        let url = stage.appendingPathComponent("helper-status.json")
        try regularFile(url, mustExist: false)
        let status = HelperStatus(ready: ready, helperPID: getpid(), parentPID: parentPID, message: message)
        try JSONEncoder().encode(status).write(to: url, options: .atomic)
        try files.setAttributes([.posixPermissions: 0o600], ofItemAtPath: url.path)
    }

    static func runHelper(_ arguments: [String]) throws {
        guard arguments.count == 7, let pid = Int32(arguments[0]), pid > 1, pid != getpid() else {
            throw AppError(message: "软件更新助手参数无效。")
        }
        let destination = URL(fileURLWithPath: arguments[1], isDirectory: true).standardizedFileURL
        let candidate = URL(fileURLWithPath: arguments[2], isDirectory: true).standardizedFileURL
        let backup = URL(fileURLWithPath: arguments[3], isDirectory: true).standardizedFileURL
        let result = URL(fileURLWithPath: arguments[4]).standardizedFileURL
        let stage = URL(fileURLWithPath: CommandLine.arguments[0]).standardizedFileURL.deletingLastPathComponent()
        let version = arguments[5]
        let previousVersion = arguments[6]
        guard destination.pathExtension == "app", stage.lastPathComponent.hasPrefix(".yilai-update-"),
              stage.deletingLastPathComponent() == destination.deletingLastPathComponent(),
              candidate.deletingLastPathComponent() == stage.appendingPathComponent("unpacked", isDirectory: true),
              backup.deletingLastPathComponent() == destination.deletingLastPathComponent(),
              backup.pathExtension == "app", backup.lastPathComponent.hasPrefix(destination.deletingPathExtension().lastPathComponent + ".backup-"),
              result == (try resultURL()), stage.resolvingSymlinksInPath() == stage else {
            throw AppError(message: "软件更新助手路径无效。")
        }
        let stageInfo = try files.attributesOfItem(atPath: stage.path)
        guard (stageInfo[.posixPermissions] as? NSNumber)?.intValue == 0o700,
              (stageInfo[.ownerAccountID] as? NSNumber)?.uint32Value == getuid() else {
            throw AppError(message: "软件更新暂存目录权限无效。")
        }
        var exited = false
        var backedUp = false
        var installed = false
        do {
            try writableInstallation(destination)
            guard try bundleInfo(destination)["CFBundleShortVersionString"] as? String == previousVersion,
                  !files.fileExists(atPath: backup.path) else {
                throw AppError(message: "当前应用或备份位置已被其他程序改动，未替换应用。")
            }
            try validateBundle(candidate, version: version, in: stage, selfTest: false)
            try helperStatus(ready: true, parentPID: pid, message: "Ready", in: stage)
            let deadline = ProcessInfo.processInfo.systemUptime + 60
            while Darwin.kill(pid, 0) == 0 || errno == EPERM {
                guard ProcessInfo.processInfo.systemUptime < deadline else {
                    throw AppError(message: "等待旧应用退出超时，未替换应用。")
                }
                Thread.sleep(forTimeInterval: 0.2)
            }
            guard errno == ESRCH else { throw AppError(message: "无法确认旧应用已退出，未替换应用。") }
            exited = true
            try writableInstallation(destination)
            guard try bundleInfo(destination)["CFBundleShortVersionString"] as? String == previousVersion,
                  !files.fileExists(atPath: backup.path) else {
                throw AppError(message: "当前应用或备份位置已被其他程序改动，未替换应用。")
            }
            try files.moveItem(at: destination, to: backup)
            backedUp = true
            try files.moveItem(at: candidate, to: destination)
            installed = true
            try writeResult(InstallationResult(success: true, pending: false, version: version,
                message: "软件已更新至 v\(version)。旧应用备份：\(backup.path)。"), to: result)
            _ = try tool("/usr/bin/open", ["-n", destination.path], in: stage, timeout: 15)
            try? files.removeItem(at: stage)
        } catch {
            var message = "软件更新失败：\(error.localizedDescription)"
            var restored = !backedUp
            if backedUp {
                do {
                    if installed {
                        try files.moveItem(at: destination, to: stage.appendingPathComponent("failed-install.app", isDirectory: true))
                    }
                    guard !files.fileExists(atPath: destination.path) else {
                        throw AppError(message: "原安装位置已出现其他文件，未覆盖。")
                    }
                    let restore = stage.appendingPathComponent("restore.app", isDirectory: true)
                    try files.copyItem(at: backup, to: restore)
                    try validateBundle(restore, version: previousVersion, in: stage, selfTest: false)
                    try files.moveItem(at: restore, to: destination)
                    restored = true
                    message += " 旧应用已恢复。"
                } catch { message += " 旧应用恢复失败：\(error.localizedDescription)。" }
                message += " 保留备份：\(backup.path)。"
            } else { message += " 当前应用保持原样。" }
            message += " 暂存目录：\(stage.path)。"
            try? helperStatus(ready: false, parentPID: pid, message: message, in: stage)
            try? writeResult(InstallationResult(success: false, pending: false, version: version, message: message), to: result)
            if exited && restored {
                do { _ = try tool("/usr/bin/open", ["-n", destination.path], in: stage, timeout: 15) }
                catch {
                    message += " 无法重新打开应用：\(error.localizedDescription)。"
                    try? writeResult(InstallationResult(success: false, pending: false, version: version, message: message), to: result)
                }
            }
            throw AppError(message: message)
        }
    }
}

func updateSelfTest() throws {
    func check(_ condition: Bool, _ message: String) throws {
        if !condition { throw AppError(message: message) }
    }
    func rejected(_ message: String, _ operation: () throws -> Void) throws {
        var refused = false
        do { try operation() } catch { refused = true }
        try check(refused, message)
    }
    func json(_ object: Any) throws -> Data {
        try JSONSerialization.data(withJSONObject: object, options: [.prettyPrinted, .sortedKeys])
    }
    let tag = "v9999.0.0"
    let assetURL = "https://github.com/kingduoyu/yilai-codex-switcher/releases/download/\(tag)/\(UpdateProtocol.asset)"
    let digest = String(repeating: "a", count: 64)
    let asset: [String: Any] = ["name": UpdateProtocol.asset, "digest": "sha256:" + digest, "size": 17, "browser_download_url": assetURL]
    let release: [String: Any] = ["draft": false, "prerelease": false, "tag_name": tag, "body": "Synthetic release notes", "assets": [asset]]
    let validRelease = try UpdateProtocol.release(json(release))
    try check(validRelease.available && validRelease.url?.absoluteString == assetURL && validRelease.size == 17 &&
              validRelease.sha256 == digest && validRelease.bundleVersion == "9999.0.0", "Release protocol decoding failed")
    for mutation in [
        ["browser_download_url": "http://github.com/kingduoyu/yilai-codex-switcher/releases/download/\(tag)/\(UpdateProtocol.asset)"],
        ["browser_download_url": "https://github.com/other/repository/releases/download/\(tag)/\(UpdateProtocol.asset)"],
        ["digest": "sha256:invalid"], ["size": UpdateProtocol.softwareLimit + 1], ["size": 0]
    ] as [[String: Any]] {
        var changedAsset = asset
        for (key, value) in mutation { changedAsset[key] = value }
        var changed = release
        changed["assets"] = [changedAsset]
        try rejected("Unsafe release asset accepted") { _ = try UpdateProtocol.release(json(changed)) }
    }
    for field in ["draft", "prerelease"] {
        var changed = release
        changed[field] = true
        try rejected("Non-stable release accepted") { _ = try UpdateProtocol.release(json(changed)) }
    }
    var current = release
    current["tag_name"] = "v" + UpdateProtocol.version
    current["assets"] = []
    try check(try !UpdateProtocol.release(json(current)).available, "Current release offered for installation")

    let immutableURL = "https://raw.githubusercontent.com/kingduoyu/yilai-codex-switcher/\(String(repeating: "b", count: 40))/model-catalog.json"
    let channel: [String: Any] = ["schema_version": 1, "revision": 1, "url": immutableURL, "sha256": digest]
    let validChannel = try UpdateProtocol.channel(json(channel))
    try check(validChannel.revision == 1 && validChannel.schemaVersion == 1 && validChannel.url.absoluteString == immutableURL,
              "Model channel decoding failed")
    for mutation in [
        ["url": UpdateProtocol.channelURL.absoluteString], ["url": immutableURL.replacingOccurrences(of: "https:", with: "http:")],
        ["url": immutableURL.replacingOccurrences(of: "kingduoyu/", with: "other/")],
        ["schema_version": 2], ["revision": 0], ["sha256": "invalid"]
    ] as [[String: Any]] {
        var changed = channel
        for (key, value) in mutation { changed[key] = value }
        try rejected("Unsafe model channel accepted") { _ = try UpdateProtocol.channel(json(changed)) }
    }
    try check(UpdateProtocol.allowed(URL(string: "https://release-assets.githubusercontent.com/file?token=synthetic")!, hosts: UpdateProtocol.assetHosts),
              "Official HTTPS asset host rejected")
    for url in ["http://github.com/file", "https://github.com.evil.invalid/file", "https://user@github.com/file", "https://github.com:444/file"] {
        try check(!UpdateProtocol.allowed(URL(string: url)!, hosts: UpdateProtocol.assetHosts), "Unsafe redirect URL accepted")
    }

    let builtin = try UpdateProtocol.builtinCatalog()
    var expanded = try JSONSerialization.jsonObject(with: builtin.data) as! [String: Any]
    var models = expanded["models"] as! [[String: Any]]
    var added = models[0]
    let newID = "gpt-swift-update-test"
    added["slug"] = newID
    added["display_name"] = "Swift update fixture"
    models.append(added)
    expanded["models"] = models
    let expandedData = try json(expanded)
    let validated = try UpdateProtocol.catalog(expandedData)
    try validated.requireIncluding(builtin.ids)
    try check(validated.models.contains { $0.slug == newID }, "Validated model list lost the added model")
    var duplicated = expanded
    duplicated["models"] = models + [added]
    try rejected("Duplicate model id accepted") { _ = try UpdateProtocol.catalog(json(duplicated)) }
    try rejected("NUL catalog input accepted") { _ = try UpdateProtocol.catalog(builtin.data + Data([0])) }
    try rejected("Corrupt catalog accepted") { _ = try UpdateProtocol.catalog(Data("old-catalog-sentinel".utf8)) }

    let files = FileManager.default
    let root = files.temporaryDirectory.appendingPathComponent("YilaiCodexSwitcher-update-test-\(UUID().uuidString)", isDirectory: true)
    try files.createDirectory(at: root, withIntermediateDirectories: false, attributes: [.posixPermissions: 0o700])
    defer { try? files.removeItem(at: root) }
    let service = PlatformService(root: root)
    let configURL = root.appendingPathComponent("config.toml")
    let authURL = root.appendingPathComponent("auth.json")
    let historyURL = root.appendingPathComponent("history.jsonl")
    let catalogURL = root.appendingPathComponent("yilai-model-catalog.json")
    let originalConfig = Data("model='gpt-swift-update-test'\nmodel_provider='openai'\n".utf8)
    let originalAuth = Data("{\"auth_mode\":\"chatgpt\"}".utf8)
    let originalHistory = Data("opaque synthetic history\n".utf8)
    try originalConfig.write(to: configURL)
    try originalAuth.write(to: authURL)
    try originalHistory.write(to: historyURL)
    try check(Set(service.modelCatalog().map(\.slug)) == builtin.ids, "Absent catalog did not fall back to builtin")
    _ = try service.updateCatalog(expandedData, sha256: UpdateProtocol.digest(expandedData), requireClosed: false)
    try check(try Data(contentsOf: configURL) == originalConfig && Data(contentsOf: authURL) == originalAuth &&
              Data(contentsOf: historyURL) == originalHistory && service.mode() == "OpenAI 官方", "Independent model update touched configuration, auth or history")
    try check(service.modelCatalog().contains { $0.slug == newID }, "Installed model list did not refresh")
    try rejected("Digest mismatch updated the catalog") {
        _ = try service.updateCatalog(builtin.data, sha256: digest, requireClosed: false)
    }
    try rejected("Catalog update removed an installed model") {
        _ = try service.updateCatalog(builtin.data, sha256: UpdateProtocol.digest(builtin.data), requireClosed: false)
    }
    do {
        var failure: UnsafeMutablePointer<CChar>?
        let lock = root.path.withCString { yilai_operation_lock($0, &failure) }
        defer { if let failure { yilai_config_free(failure) } }
        guard let lock else { throw AppError(message: "Cannot acquire catalog fixture lock") }
        defer { yilai_operation_unlock(lock) }
        try rejected("Model update bypassed the operation lock") {
            _ = try service.updateCatalog(expandedData, sha256: UpdateProtocol.digest(expandedData), requireClosed: false)
        }
    }
    try check(try Data(contentsOf: catalogURL) == expandedData, "Rejected update changed the catalog")
    try files.removeItem(at: authURL)
    _ = try service.run(.configure, key: "sk-swift-update-fixture", requireClosed: false)
    let configured = try String(contentsOf: configURL, encoding: .utf8)
    try check(configured.contains("gpt-swift-update-test"), "Configuration did not preserve a model from the updated catalog")
    try check(try Data(contentsOf: catalogURL) == expandedData && Data(contentsOf: historyURL) == originalHistory,
              "Configuration replaced the updated catalog or changed history")
    try Data("old-catalog-sentinel".utf8).write(to: catalogURL)
    try check(Set(service.modelCatalog().map(\.slug)) == builtin.ids, "Corrupt catalog list did not fall back to builtin")
    _ = try service.run(.configure, key: "sk-swift-update-fixture", requireClosed: false)
    try check(try Data(contentsOf: catalogURL) == builtin.data, "Configuration did not repair the corrupt catalog with builtin data")
}
