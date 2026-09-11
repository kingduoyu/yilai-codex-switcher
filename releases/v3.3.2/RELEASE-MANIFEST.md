# v3.3.2 Release Manifest

Date: 2026-09-11 (Asia/Shanghai).
Publication confirmed: 09/11/2026 11:43:08 (UTC). GitHub latest is v3.3.2, neither draft nor prerelease. All four remote assets matched local digest, byte length and uploaded state before publication. Local publish/ delivery copies and UI previews were also hash-verified.
Release tag commit: 0a07cdbe3f9f01646a6020797a6bab4e55c76d46.

Application source commit (both platforms): 689ce288b37d7841f77292adb709fad3cea2d730. Subsequent release-tag changes are release documentation/checksums only.

## Scope

- Restore the Windows log button after real failed operations.
- Recover pending history synchronization before either main switch; finalize completed transactions and safely restore incomplete ones without reverting the current connection or losing appended messages/titles. Reset still only renames config and leaves history/pending markers alone.
- Avoid new unreferenced provider archives on repeated switching; retain old routes only when other profiles need them. Existing archives are not indiscriminately deleted.
- Lock a whole configurator operation per CODEX_HOME, including rollback. System-released file locks prevent two current configurators from modifying the same home concurrently.
- Include native system error codes and detect observed external config changes during rollback. Authentication restoration commits only if auth.json remains absent, without replacing another process's new file.
- Report unavailable/incomplete diagnostics on failure; logging does not decide business success.

## Verification

- Windows x64 final native build and --self-test passed, including configuration preservation/idempotence, pending-history recovery, operation lock rejection/release, no-replace auth restoration, ordinary rollback and redacted diagnostics.
- Tests/windows-ui.py passed against the final EXE: two actual failure transitions show an enabled log button, retain the isolated config and write failure records. The test does not use a simulated screenshot state.
- Final normal Windows GUI capture inspected: dist/windows-v332.png.
- Codex app-server 0.153.4 integration passed: dist/runtime-test-i947JG/result.json. Verified gpt-6-astra catalog preservation; API account=null/requiresOpenaiAuth=false; actual mock HTTP bearer authentication; native image tool/header; automatic history list/read/resume; API-to-official configuration; and rollback/log privacy on injected failures. Uses isolated homes and a localhost mock provider.
- Additional final driver acceptance passed: dist/review-v332/acceptance.json. Seven same-Key switches keep a single provider and identical config bytes; a completed pending marker permits official switching; a prepared marker recovers before API switching and retains an appended message; reset preserves history and malformed pending bytes.
- macOS universal workflow: https://github.com/kingduoyu/yilai-codex-switcher/actions/runs/34595009345. Succeeded at the application source SHA above. Both arm64 and x86_64 compiled; native self-test passed on the arm64 runner, including operation locking, pending recovery and no-replace auth restoration with private file permissions. DMG verification and final GUI capture passed. Ad-hoc signed, not notarized; no Intel hardware run claimed.

## Boundaries

- Close Codex and CC-Switch before use; the operation lock coordinates this configurator, not arbitrary third-party programs. Configuration rollback checks detect observed external changes but cannot promise filesystem-wide compare-and-swap against unrelated writers.
- Shared history is limited to existing local CODEX_HOME records. Unknown/malformed recovery states fail conservatively and preserve backups; no silent skipping of damaged history.
- The two-button UI, automatic API image configuration, current auth deletion and rename-only reset remain unchanged.
- Real paid image generation and real ChatGPT login were not performed. Large-history performance and Windows multi-DPI behavior are not claimed as optimized in this release.
- No modification of real user credentials/history or CCS source/settings during tests.

## Assets

| Asset | Bytes | SHA-256 |
| --- | ---: | --- |
| YilaiCodexSwitcher.exe | 4955136 | 3c701a8e7058d6654a7d1f43e3861b12c5cb5363dcd83d392177685e19e3bc80 |
| YilaiCodexSwitcher-macOS-universal.dmg | 5993764 | d59182e7458ac45cf492f6208e2a0d0bc55e3d17de8dac6c00213deda86db63b |
| YilaiCodexSwitcher-macOS-universal.zip | 5264995 | e44ebcaa162bb07e2ab13a37be0b29bdc99b7983816b3f29498f36f8371e7f9a |
