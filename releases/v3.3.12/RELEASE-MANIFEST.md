# v3.3.12 Release Manifest

Status: Windows and macOS candidates verified; publication pending.

Application source: merge commit c6026d3e6e59fe856842fd36a1cc8afc873c250c. CI built the identical application tree at e6e11e54505b13578c56062c0aa498f80ece9d7c.

Scope: make API and official connection configuration the primary transaction. History synchronization runs afterward and cannot roll back a successful configuration, model catalog update or authentication change. Duplicate session IDs are processed per file. Malformed, unreadable, changed, unbackable or unwritable JSONL files are recorded and skipped while other eligible sessions continue. Restore remains strict, and successful migrations retain backup and recovery handling.

Version metadata: Windows 3.3.12.0; macOS 3.3.12/build24.

Windows: GitHub Actions run 34733787919 succeeded at e6e11e54505b13578c56062c0aa498f80ece9d7c. The native executable self-test and `Tests/windows-ui.py` passed. Local verification also passed the application/history driver self-tests, `Tests/unified-history.py`, `Tests/source-integration.py`, and real Codex CLI runtime integration with the managed sol, terra and astra catalog.

macOS: GitHub Actions run 34733787983 succeeded at the same source commit. The workflow built the universal app, ran its native self-test, verified the DMG with `hdiutil`, and rendered the UI screenshot. The downloaded ZIP reported 3.3.12/build24 and its Mach-O executable contains both x86_64 and arm64 slices. The screenshot was inspected without clipping or overlap.

Limits: history synchronization is best effort only for individual JSONL files during forward migration. Restore and global transaction prerequisites remain strict. No production official OAuth send, paid image generation or physical Intel execution was tested. API configuration still deletes auth.json; official switching preserves the current file but does not restore a previously deleted login. macOS is ad-hoc signed and not notarized. Tests used isolated configuration and history directories.

| Release asset | Bytes | SHA-256 |
| --- | ---: | --- |
| YilaiCodexSwitcher.exe | 5376000 | eb45b79be50c1ac9bc8cb3964b5d911703ef56ae9a7012fc3bb9e9cdc21af1df |
| YilaiCodexSwitcher-macOS-universal.dmg | 6194949 | 775d8beb358b0f0ce859387698a64e52b4dc0e858d6872b20b8c1c1070f6e6e0 |

Validation-only macOS ZIP: 5447532 bytes, SHA-256 a7b41cc391b2e553000f0baf9349327517a1c1470b572a619c8a6b719c25057e. It is not a public release attachment.

Publication plan: publish exactly the two stable program filenames above. Do not attach the validation ZIP, versioned aliases or `SHA256SUMS.txt`.
