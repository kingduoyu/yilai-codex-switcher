# v3.3.13 Release Manifest

Status: published as the latest GitHub release after Windows/macOS verification and public download round-trip checks.

Scope: commit API connection, managed model catalog and authentication handling before optional checks. Read the final effective Codex configuration afterward and report missing connection, model or image-generation functionality together with identifiable override sources. Final probing, CCS directory mismatches and history synchronization warnings do not roll back a usable API connection. Process JSON logs and the log-view UI are removed; displayed reasons remain sanitized.

Version metadata: Windows 3.3.13.0; macOS 3.3.13/build25.

Local Windows verification on 2026-09-13: native build, executable self-test, GUI failure/warning regression, source-layer integration and real Codex CLI 0.153.4 runtime integration passed. Tests used isolated configuration/history directories and a local mock API.

Windows: GitHub Actions run 34759915407 succeeded at application source commit cfcce236c7d585dd7ac650f2553c35305669fb71. The native executable self-test and GUI failure/warning regression passed. The downloaded CI executable also passed its self-test.

macOS: GitHub Actions run 34759916795 succeeded at the same application source commit. The workflow built and self-tested the universal app, verified the DMG with hdiutil, rendered the UI screenshot and uploaded the validation ZIP. The downloaded ZIP reports 3.3.13/build25; its Mach-O fat header contains x86_64 and arm64 slices. The screenshot was inspected without clipping or overlap.

Limits: core write failures may still roll back the current operation when connection, catalog or auth handling would otherwise leave a partial state. Effective configuration probing is read-only and does not automatically edit project/profile/launch/admin sources or the CCS database. No production official OAuth send, paid image generation or physical Intel execution was tested. macOS is ad-hoc signed and not notarized.

| Release asset | Bytes | SHA-256 |
| --- | ---: | --- |
| YilaiCodexSwitcher.exe | 5355008 | ad6617048106233d2ecdea11e127a7bba1c3f8201f4a09340f199634cb826933 |
| YilaiCodexSwitcher-macOS-universal.dmg | 6175308 | 234d7251aa4b6843ba716e0ca260a8d139fb46d9c1748a54b100e28c18538af6 |

Validation-only macOS ZIP: 5422710 bytes, SHA-256 42a1dcadd1a3ca88ad5a1dffca22eeece99df41ed1259678ca942fa1a3851293. It will not be attached to the public release.

Publication: GitHub release database ID 387910354 was published at 2026-09-13T13:33:14Z as v3.3.13 and selected as latest. The public release contains exactly YilaiCodexSwitcher.exe and YilaiCodexSwitcher-macOS-universal.dmg; no validation ZIP, versioned alias or SHA256SUMS.txt is attached. Both releases/latest/download links returned HTTP 200, and the downloaded files matched the candidate sizes and SHA-256 values above. The annotated tag points to 3fa0c0a0d185a1f19e7bbc6df7b3851d1345baa8. Final stable and versioned copies are retained in the parent publish/win-x64 and publish/mac-universal directories.
