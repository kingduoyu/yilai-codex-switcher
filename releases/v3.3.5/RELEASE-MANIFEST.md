# v3.3.5 Release Manifest

Date: 2026-09-11 (Asia/Shanghai).
Publication confirmed: 2026-09-11T14:13:15Z (UTC). GitHub latest is v3.3.5, neither draft nor prerelease. All four remote assets matched local hashes, sizes and uploaded state before publication; published application assets rechecked. Local publish binaries, checksum and previews hash-verified.
Release tag commit: 93b3e2ee0c0ee23631e36d49b38f5195e80ebd1b.

Application source commit (both platforms): 722bec556717f82ecb283fabfc55dbdb1b05f962. Subsequent changes before the release tag are test-only correction 39a3f18 and release documentation/checksums, with no application changes.

## Scope

- Remove the official-switch action, configuration rewrite operation, verification branch and both platform buttons. CCS is responsible for official switching. The app has one primary API setup button; reset remains rename-only and never reactivates disabled config files.
- Keep verified API configuration independent of automatic history migration. History recovery/sync failures preserve working API config and auth deletion, report a warning and expose logs. Configuration/auth/source verification failures still roll back the incomplete configuration operation.
- Accept legitimate fork rollouts: the first session_meta is canonical; later inherited metadata is validated and retained byte-for-byte, including its different parent ID/provider. Keep complete JSONL validation and conservative handling of truly invalid history, now with a file path and no parser content snippets.
- Preserve CCS's custom provider history bucket and its separate settings. CCS's unified-history toggle/migration-completion state is not stored in config.toml and is not modified by this application; completed migration is not a permanent background synchronization service.

## Verification

- Windows final build and native --self-test passed, including fork migration/undo/failure recovery, API history warnings and auth failure rollback. Real GUI test confirms the removed official button is absent and two actual failures leave enabled API/log controls. Screenshot dist/windows-v335.png inspected.
- Independent history-core self-test passed: child first metadata and parent later metadata with different IDs/providers survive synchronization, idempotence, injected failures and undo; appended messages survive; invalid later records are still rejected without leaking content.
- Final real Codex 0.153.4 runtime integration passed: dist/runtime-test-46QBOO/result.json. Native image declaration/header, mock bearer authentication without official login, catalog retention, history list/read/resume, and nonblocking history warning/privacy verified. An earlier obsolete test sequence failed because removing official switching also removed its post-undo resync; the test was corrected to reapply API and the final run passed.
- Auto-discovery source integration passed: dist/source-test-rh8_t543/result.json. Fresh API setup, trusted nested sources, untrusted-source preservation and read failure checks pass. History failure after source verification preserves API, commits source edits, retains malformed history unchanged and leaves no pending source marker.
- macOS workflow https://github.com/kingduoyu/yilai-codex-switcher/actions/runs/34608301116 succeeded at the application SHA above. arm64 and x86_64 compiled; native self-test passed, including independent history warnings and malformed recovery marker behavior. DMG verification and screenshot passed. Downloaded ZIP independently checked for version 3.3.5/build17 and both Mach-O architectures.

## Boundaries

- Full JSONL validation is retained; no unmeasured performance improvement is claimed. A history warning means some history remains unsynchronized, not that API configuration failed.
- Tests use synthetic homes and localhost services. No real paid image generation or production ChatGPT login; no real user configuration/history was modified for this release's tests.
- Real runtime integration ran on Windows. macOS has native self-tests and POSIX protocol stub coverage; no Intel hardware execution claimed. Ad-hoc signed, not notarized.
- Source inspection retains the previous scope: default user config and active local workspaces; not arbitrary CLI --profile/-c, future workspaces or system/organization policy removal.

## Assets

| Asset | Bytes | SHA-256 |
| --- | ---: | --- |
| YilaiCodexSwitcher.exe | 5159424 | ddbb6c3a8912d1b375fe96f1258836e8e16bf5f94cfd2399701956a0a8ff5d62 |
| YilaiCodexSwitcher-macOS-universal.dmg | 6129258 | c727c834c76d764d3423f9b54af35758236b06046793049b8f3c29435433c4a9 |
| YilaiCodexSwitcher-macOS-universal.zip | 5390098 | cdf5701dd682638c77c6c1a27bce8d8afb29e3680a4d4096b130e95e3ba079f1 |
