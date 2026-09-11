// swift-tools-version: 5.9
import PackageDescription
let package = Package(
    name: "YilaiCodexSwitcherMac",
    platforms: [.macOS(.v13)],
    products: [.executable(name: "YilaiCodexSwitcherMac", targets: ["YilaiCodexSwitcherMac"])],
    targets: [
        .target(name: "ConfigRewrite", path: "Sources/ConfigRewrite", publicHeadersPath: "include"),
        .target(name: "HistorySync", dependencies: ["ConfigRewrite"], path: "Sources/HistorySync", publicHeadersPath: "include"),
        .executableTarget(name: "YilaiCodexSwitcherMac", dependencies: ["ConfigRewrite", "HistorySync"], path: "Sources/App")
    ], cxxLanguageStandard: .cxx17
)
