# v3.3.0 Release Manifest

Date: 2026-09-11 (Asia/Shanghai).

Release: https://github.com/kingduoyu/yilai-codex-switcher/releases/tag/v3.3.0

Application source commit (both platforms): 3d4726b2f57bb2240ee9049d7229a38f0f072608. The release tag adds only release documentation and asset checksums after this commit.

## Scope

Native rewrite with separate configuration rules, history synchronization engine, and Windows/macOS adapters. Default image enhancement preserves CCS models, catalog and authentication. Connection configuration, local history synchronization/undo and credential cleanup are explicit separate actions. CCS source/settings and real user configuration/history were not modified during development or testing.

## Build and verification

- Windows x64: LLVM-MinGW UCRT 20260616, static native C++ application. Self-test passed, including configuration preservation, profile routing, history sync/undo/fault rollback, Unicode paths and native Recycle Bin failure recovery. Final GUI capture passed; imported libraries are Windows system/UCRT libraries.
- macOS 13+, arm64 + x86_64: Actions run 34584033825 succeeded at the application source commit above. Both architectures compiled and were joined with lipo. Native configuration/history and Foundation trash/immutable-file rollback self-tests passed on the arm64 runner. DMG verification and final GUI capture passed. Ad-hoc signed, not notarized; no claim of an Intel hardware run.
- Workflow: https://github.com/kingduoyu/yilai-codex-switcher/actions/runs/34584033825
- Codex app-server 0.153.4 integration: isolated CODEX_HOME/CODEX_SQLITE_HOME under a Unicode path; local mocked Responses service only. Verified preservation of existing gpt-6-astra catalog/auth, visible model/list, native image_gen.imagegen declaration and image header, real runtime thread creation, legacy provider normalization, list/read/resume, continued messages retained by undo, and official-provider shape/history readability across two synthetic credential changes. The shared core did not change after this test. Reproduction: Tests/run-runtime.ps1.
- History regression coverage includes archived official records, external SQLite locations, missing/NULL provider values, interrupted synchronization recovery, malformed history refusal and selective undo retaining new messages/threads/titles.

## Assets

| Asset | Bytes | SHA-256 |
| --- | ---: | --- |
| YilaiCodexSwitcher.exe | 4805632 | b78462662b887d2244a46cf96ec90bb1b084bfc25b8ddddd3993771c140e62ee |
| YilaiCodexSwitcher-macOS-universal.dmg | 5711206 | bb5a8a7004843e0be2b081ba23b44c1257c2d5b104c8afcd609a1a63b6561144 |
| YilaiCodexSwitcher-macOS-universal.zip | 5005392 | 181d10d407a719a23524eee3111bd72f1ce6fe950609de0e1e0b492bd034c59b |

## Boundaries

- Default enhancement normalizes TOML formatting; comments are not retained. Models, catalogs and login credentials remain.
- CCS controls later official/third-party switches. Enable CCS’s own unified-history option to keep the custom provider bucket across those switches. This application does not change that CCS setting.
- History covers records already in the same CODEX_HOME, including archives; it does not download another account’s cloud records.
- Undo requires access to the original external SQLite home when one was used. New messages are retained; independently changed connection configuration is not overwritten.
- Production paid image generation and real ChatGPT account login were not exercised. Native tool exposure/header and local simulated provider/account changes were tested.
