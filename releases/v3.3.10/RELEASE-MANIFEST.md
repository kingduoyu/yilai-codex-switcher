# v3.3.10 Release Manifest

Status: published and verified as the latest stable release on 2026-09-12.

Application source: 9aaa1ce807733a4da41993add03261965ea19575. Later release-record commits do not change application or build inputs.

Release tag: v3.3.10, pointing to c9353d2dfef62b1f0114181c05ebf078433c99d2. main and the release branch were updated by a non-force atomic push before publication. The tag is retained unchanged after publication-record updates.

Scope: reject NUL input before official rewrites; revalidate config snapshots before writes and rollback; preserve inactive root/profile connections referenced through custom or legacy yilai. Official custom/yilai aliases remain free of third-party endpoint, bearer-token and header settings. Existing API authentication policy, history migration scope, UI actions and model catalog remain unchanged.

Version metadata: Windows application manifest 3.3.10.0; macOS 3.3.10/build22.

Windows: Windows/build.ps1, native app --self-test, history-self-test and Tests/windows-ui.py passed. The UI regression verified recovery of controls and log access after two consecutive failures without config changes. The isolated official-switch regression passed all six checks, including inactive profile preservation, NUL rejection and external-change protection. Tests/unified-history.py passed all seven checks for migration, message preservation, idempotence and both routes. All 24 recorded build-input fingerprints remained unchanged during verification.

macOS: https://github.com/kingduoyu/yilai-codex-switcher/actions/runs/34676352210 succeeded on the application source above. The workflow built arm64 and x86_64, ran native app self-tests on the Apple Silicon runner, verified the DMG with hdiutil and rendered the UI screenshot. The downloaded ZIP independently passed CRC verification, reported 3.3.10/build22, retained executable permissions and contained both Intel and Apple Silicon Mach-O slices. Downloaded DMG/ZIP hashes matched the CI log.

Windows normal/error screenshots and the macOS screenshot were inspected: complete layout, switching/reset controls present, no standalone history button and no real credentials. The Windows error view retained the log action.

Limits: no production official OAuth send, paid image generation or physical Intel execution was tested. API configuration still deletes auth.json; official switching preserves the current file but does not restore deleted login. No automatic CCS database/current synchronization or full CCS account-management parity is claimed. Only legacy yilai history is migrated; other providers and message bodies remain unchanged. macOS is ad-hoc signed and not notarized. Tests used isolated configuration/history and did not change user live data.

| Asset | Bytes | SHA-256 |
| --- | ---: | --- |
| YilaiCodexSwitcher-v3.3.10.exe | 5265408 | 9026111177bc26802e201d5c80f2049f59fe01d618dab08dccf92bb695dcc28c |
| YilaiCodexSwitcher-v3.3.10-macOS-universal.dmg | 6175867 | 28e8dd250e56d57c85e3470ce72d5cdee40672fa9309e56f792ea2fba1a7defe |
| YilaiCodexSwitcher-v3.3.10-macOS-universal.zip | 5423252 | 7a3930a2a54847d7b50730cbb892bff4de392b49ceaaf481ebc0fbc4de19e5e0 |
| SHA256SUMS.txt | 323 | 21ad23eb8375fda7385417ab21ba2e6b1a2eff8bef31511755a498b85ff9d340 |

Publication confirmed: 2026-09-12T06:00:20Z; release id 387471362; latest stable tag v3.3.10. URL: https://github.com/kingduoyu/yilai-codex-switcher/releases/tag/v3.3.10. All four asset names, sizes and GitHub SHA-256 digests matched local files; all four draft assets were also downloaded and hash-verified before publication. The published release was rechecked as non-draft/non-prerelease with the same four asset digests and exact target commit.

Delivery: the versioned EXE, DMG and ZIP were copied to the parent publish/win-x64 and publish/mac-universal directories and to user-facing outputs, with SHA-256 verification. SHA256SUMS-v3.3.10.txt was copied with matching bytes to both platform directories and outputs. Versioned Windows normal/error and macOS proof screenshots were retained in their platform publish directories and hash-verified. Older packages and generic filenames were not overwritten.
