# v3.3.14 Release Manifest

Status: platform-verified candidate; publication pending.

Scope: initialize disposable probe databases against an empty history before reading real configuration. Reuse one runtime session for all configuration contexts and share the existing 20-second deadline across bootstrap and verification. Isolate the startup working directory and identify the timeout phase. API rollback semantics are unchanged.

Version metadata: Windows 3.3.14.0; macOS 3.3.14/build26.

Application source: 1aa91ff06b155ccd7214c5a4bb559863f4b49d19, merged into main by PR #3 at e5af818. The merge tree matches the CI source tree exactly.

Windows: GitHub Actions run 34853808194 passed native build, executable self-test and GUI regression. macOS: GitHub Actions run 34853813215 passed universal build, native self-test, DMG verification and screenshot generation. The downloaded app reports 3.3.14/build26; the Mach-O header contains x86_64 and arm64 slices. The macOS screenshot was inspected without clipping or overlap.

| Release asset | Bytes | SHA-256 |
| --- | ---: | --- |
| YilaiCodexSwitcher.exe | 5363200 | 88cdbe3363c69bd29e0deb7d82da40306c242c69f38092aff358543faf64b5d4 |
| YilaiCodexSwitcher-macOS-universal.dmg | 6174404 | 28d8b93e58fe4b40f4a7d52f431f99c642936ed505d61dc048ab7145893d229d |

Local validation before version update: Windows build, real Codex CLI 0.153.4 runtime integration, source-layer integration and GUI regression passed. On the same local configuration, one measured probe took 17.32 seconds before the bootstrap change and 0.47 seconds after it. These measurements are not a customer-wide performance guarantee.

Limits: no paid image generation, production official OAuth send or physical Intel execution was tested. Temporary bootstrap storage is discarded; user history databases are not used for probing. macOS is ad-hoc signed and not notarized.
