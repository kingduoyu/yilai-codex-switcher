// swift-tools-version: 5.9
import PackageDescription

let package = Package(
  name: "YilaiCodexSwitcherMac",
  platforms: [.macOS(.v13)],
  products: [
    .executable(name: "YilaiCodexSwitcherMac", targets: ["YilaiCodexSwitcherMac"])
  ],
  targets: [
    .target(
      name: "ConfigRewrite",
      path: "Sources/ConfigRewrite",
      publicHeadersPath: "include"
    ),
    .executableTarget(
      name: "YilaiCodexSwitcherMac",
      dependencies: ["ConfigRewrite"],
      path: "Sources/YilaiCodexSwitcherMac"
    )
  ],
  cxxLanguageStandard: .cxx17
)
