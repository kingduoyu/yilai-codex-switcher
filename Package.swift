// swift-tools-version: 5.9
import PackageDescription
let package = Package(
    name: "YilaiCodexSwitcherMac",
    platforms: [.macOS(.v13)],
    products: [.executable(name: "YilaiCodexSwitcherMac", targets: ["YilaiCodexSwitcherMac"])],
    targets: [
        .target(name: "ConfigRewrite", path: "Sources/ConfigRewrite", publicHeadersPath: "include"),
        .target(name: "HistoryRepair", dependencies: ["ConfigRewrite"], path: "Sources/HistoryRepair", publicHeadersPath: "include"),
        .target(name: "ConfigSources", dependencies: ["ConfigRewrite"], path: "Sources/ConfigSources", publicHeadersPath: "include"),
        .target(name: "OperationGuard", path: "Sources/OperationGuard", publicHeadersPath: "include"),
        .target(name: "Diagnostics", path: "Sources/Diagnostics", publicHeadersPath: "include"),
        .executableTarget(name: "YilaiCodexSwitcherMac", dependencies: ["ConfigRewrite", "Diagnostics", "OperationGuard", "ConfigSources", "HistoryRepair"], path: "Sources/App")
    ], cxxLanguageStandard: .cxx17
)
