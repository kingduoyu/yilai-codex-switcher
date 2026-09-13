# v3.3.13 Release Manifest

Status: release candidate pending GitHub Actions and public download verification.

Scope: commit API connection, managed model catalog and authentication handling before optional checks. Read the final effective Codex configuration afterward and report missing connection, model or image-generation functionality together with identifiable override sources. Final probing, CCS directory mismatches and history synchronization warnings do not roll back a usable API connection. Process JSON logs and the log-view UI are removed; displayed reasons remain sanitized.

Version metadata: Windows 3.3.13.0; macOS 3.3.13/build25.

Local Windows verification on 2026-09-13: native build, executable self-test, GUI failure/warning regression, source-layer integration and real Codex CLI 0.153.4 runtime integration passed. Tests used isolated configuration/history directories and a local mock API.

Limits: core write failures may still roll back the current operation when connection, catalog or auth handling would otherwise leave a partial state. Effective configuration probing is read-only and does not automatically edit project/profile/launch/admin sources or the CCS database. No production official OAuth send, paid image generation or physical Intel execution was tested. macOS is ad-hoc signed and not notarized.

| Release asset | Bytes | SHA-256 |
| --- | ---: | --- |
| YilaiCodexSwitcher.exe | pending | pending |
| YilaiCodexSwitcher-macOS-universal.dmg | pending | pending |

Validation-only macOS ZIP: pending. It will not be attached to the public release.
