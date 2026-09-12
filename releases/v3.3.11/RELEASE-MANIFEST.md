# v3.3.11 Release Manifest

Status: published as the latest GitHub release after Windows/macOS build verification and downloaded-asset round-trip checks.

Application source: 1bafbf134a5736f72ce5f6e414a1e5e44a5718e8. The following README and release-record commits do not change application or build inputs.

Scope: preserve the Codex Desktop saved full-access mode when TOML has no explicit permission policy, while never replacing explicit approval or sandbox settings. Speed repeat history checks by reading only the metadata prefix of non-yilai sessions and querying only legacy yilai database rows. Files selected for migration still receive complete JSONL validation, backup, rollback and recovery handling. Publish one stable Windows EXE and one stable macOS DMG so `releases/latest/download` links remain valid without duplicate packages.

Version metadata: Windows 3.3.11.0; macOS 3.3.11/build23.

Windows: the versioned build, native executable `--self-test`, application/history driver self-tests, `Tests/windows-ui.py`, `Tests/unified-history.py` and the updated official-switch regression passed. The regression covers NUL rejection, inactive profile preservation, external modification protection and unrelated malformed non-yilai tails. A synthetic repeat check over 64 already-custom sparse rollouts with 16.0 GiB logical size completed in 0.220 and 0.142 seconds; this measures the prefix-only path, not physical sequential disk throughput. The ordinary migration regression completed in 0.173 seconds. Final normal and error screenshots were inspected; controls, status text, log action and reset action are complete without overlap.

macOS: https://github.com/kingduoyu/yilai-codex-switcher/actions/runs/34711173373 succeeded at workflow head 29318ce90b28531781e2aaf46b4915bc01ec7727. The workflow built arm64 and x86_64, ran the native app self-test, verified the DMG with `hdiutil` and rendered the UI screenshot. The downloaded ZIP independently passed full-entry read verification, reported 3.3.11/build23 and contained both Intel and Apple Silicon Mach-O slices. Downloaded DMG/ZIP hashes matched the CI log. The screenshot was inspected without clipping or overlap.

Limits: no production official OAuth send, paid image generation or physical Intel execution was tested. API configuration still deletes auth.json; official switching preserves the current file but does not restore deleted login. No automatic CCS database/current synchronization or full CCS account-management parity is claimed. Only legacy yilai history is migrated; other providers and message bodies remain unchanged. macOS is ad-hoc signed and not notarized. Tests used isolated configuration/history and did not change user live data.

| Asset | Bytes | SHA-256 |
| --- | ---: | --- |
| YilaiCodexSwitcher.exe | 5297664 | 3efc89362ecaddfd7e6819f40318e3bc87816e8c83654027728598ce8a3f2e5c |
| YilaiCodexSwitcher-macOS-universal.dmg | 6179992 | e133315a2a0f3b95f2c09b273df265af8e3279d3610d5ac78e4d43b9e66b8703 |

Publication: GitHub release database ID 387665453 was published at 2026-09-12T18:33:47Z as `v3.3.11` and selected as latest. The initial seven draft assets were downloaded into a separate directory and matched the local source files before publication. The public asset set was then simplified to exactly two stable program files; versioned duplicates, the optional ZIP and the checksum attachment were removed. The stable Windows EXE and macOS DMG `releases/latest/download` URLs returned HTTP 200 with 5297664 and 6179992 bytes respectively. The annotated tag remains at 0b6720b77174cab91bfa483f9cdffbaab9b98bf0; later publication-record commits do not move it. Final stable copies are retained in the parent `publish/win-x64` and `publish/mac-universal` directories without removing older release versions.
