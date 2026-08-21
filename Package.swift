// swift-tools-version: 5.9
import PackageDescription

let package = Package(
  name: "YilaiCodexSwitcherMac",
  platforms: [.macOS(.v13)],
  products: [
    .executable(name: "YilaiCodexSwitcherMac", targets: ["YilaiCodexSwitcherMac"])
  ],
  targets: [
    .executableTarget(
      name: "YilaiCodexSwitcherMac",
      path: "Sources/YilaiCodexSwitcherMac"
    )
  ]
)
