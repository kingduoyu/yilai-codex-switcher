# v3.3.4 Release Manifest

Date: 2026-09-11 (Asia/Shanghai).
Publication confirmed: 2026-09-11T12:42:41Z (UTC). GitHub latest is v3.3.4, neither draft nor prerelease. Four uploaded assets matched local hashes, sizes and state before publication; published application assets rechecked. Local publish binaries, checksum and previews hash-verified. The running old Windows executable was renamed and retained so the canonical delivery path could receive the new binary without terminating the user's open window.
Release tag commit: 8791f4c8d5a1a7c761aa894b6725cd2102ca9e01.

Application source commit (both platforms): adba74e4e39c642a65398fbb549f6f8d92741646. Later release-tag changes are documentation/checksums only.

## Fix

- Windows runtime discovery previously prioritized the loose bin/codex.exe over desktop version directories. On the reported machine this selected 0.130.0-alpha.5 instead of 0.153.4 and rejected model_reasoning_effort=max before the source inspection could proceed.
- Prefer version-directory runtimes (existing modification-time ordering), retaining the loose executable and PATH as fallbacks. Preserve model and reasoning settings. macOS retains its separate application-bundle lookup.
- RPC failures include a safe category, numeric code, runtime path and configuration context. Never forward raw error text that could quote credentials/config values.
- Add automatic runtime discovery to source integration tests so explicit runtime overrides cannot hide this regression again.

## Verification

- Final Windows build, native --self-test and real GUI failure/log-button regression passed. Final screenshot dist/windows-v334.png inspected.
- Runtime discovery regression passed: dist/runtime-discovery-yys7duqu/result.json. Versioned candidates outrank a loose legacy executable even with newer modification time; latest versioned candidate and legacy-only fallback work. Actual old runtime rejects max with classified diagnostics; current runtime accepts max. Private unknown values and tokens do not appear in errors.
- Read-only probes on the affected machine automatically selected 0.153.4 and successfully read both default user config and the current active workspace with max preserved. These probes did not modify the user's config/auth/history.
- Full source switching with --auto-runtime passed: dist/source-test-7xz1ys15/result.json. Includes empty-home switching, trusted nested overrides with unrelated settings preserved, untouched untrusted project, history-failure rollback and unreadable-source rejection. Fixtures include model_reasoning_effort=max.
- Full Codex 0.153.4 localhost integration passed: dist/runtime-test-apbq7W/result.json. Confirms native image declaration/header, bearer authentication without official login, model catalog preservation, automatic history list/read/resume and rollback/log privacy. Uses isolated homes and a mock Responses server.
- macOS workflow https://github.com/kingduoyu/yilai-codex-switcher/actions/runs/34599904662 succeeded at the application source SHA. arm64+x86_64 build, native macOS self-test, DMG verification and screenshot passed. Downloaded ZIP checked for version 3.3.4/build16 and both Mach-O architectures. Ad-hoc signed, not notarized; native self-test ran on arm64, no Intel hardware execution claimed.

## Boundaries

- No real paid image request or production ChatGPT login was performed. Real runtime integration ran on Windows; macOS has native self-tests and POSIX protocol stub coverage.
- Detection remains limited to default local configuration and current active local workspaces, not arbitrary independent CLI --profile/-c or future project contexts. System/organization policy is not removed.
- Source selection prefers desktop version-directory executables; it does not infer semantic version order from hash directory names or silently retry a rejected config against arbitrary older runtimes.

## Assets

| Asset | Bytes | SHA-256 |
| --- | ---: | --- |
| YilaiCodexSwitcher.exe | 5165568 | 39d8c2f9f78888f0a94f9974b24badecefa5f7ce880c8129c007d11d409b6c4c |
| YilaiCodexSwitcher-macOS-universal.dmg | 6127573 | e910b20d0c98103a48abf6bd8f8bb69ec10de6accb335267bf8bc28ad3162c74 |
| YilaiCodexSwitcher-macOS-universal.zip | 5393644 | 6e74b2e48bdb21352ec7e9f70f5f2f9f2b3615b2da44549860ab3610c2e80a57 |
