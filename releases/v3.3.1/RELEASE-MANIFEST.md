# v3.3.1 Release Manifest

Date: 2026-09-11 (Asia/Shanghai).

Publication: pending remote asset verification.
Release: https://github.com/kingduoyu/yilai-codex-switcher/releases/tag/v3.3.1

Application source commit (both platforms): 1de900b4c8d8061f17aade84d6c12a261d14135c. Subsequent release-tag changes contain only documentation, runtime test coverage and checksums; application build inputs are unchanged.

## Scope

Windows and macOS native two-button interface. API switching configures image support; both switches delete current auth.json and automatically synchronize local history. API authentication follows CCS provider-owned token plus requires_openai_auth=false. Official switching restores the native official provider shape and requires normal subsequent login. Reset renames config.toml with a unique disabled suffix, without confirmation or auth/history changes. Existing CCS models/catalogs and inactive profile routing are preserved.

Automatic operation logs include stage, sanitized failure reason and rollback result. The log entry point appears only after failure. Logs redact credentials and omit config dumps, login payloads and conversation content. Logging failures do not change the operation result. Logs cover configurator operations only, not subsequent Codex chat/network requests.

## Build and verification

- Windows x64: final native build and --self-test passed. Includes configuration preservation, automatic history sync, locked auth deletion rollback, malformed history rollback, rename-only reset and diagnostic redaction. Final normal and simulated-failure GUI captures inspected.
- macOS 13+, universal arm64 + x86_64: Actions run 34589786138 completed successfully at the application source commit above. Both architectures compiled; native self-tests including diagnostics passed on the arm64 runner; DMG verification passed. Final GUI capture inspected. Ad-hoc signed, not notarized; no Intel hardware execution claimed.
- Workflow: https://github.com/kingduoyu/yilai-codex-switcher/actions/runs/34589786138
- Codex app-server 0.153.4 integration passed using isolated CODEX_HOME/CODEX_SQLITE_HOME and a localhost mock Responses service. Evidence: dist/runtime-test-XRuA82/result.json. Reproduce with Tests/run-runtime.ps1.
- Runtime verified existing gpt-6-astra model catalog, API account=null and requiresOpenaiAuth=false after auth deletion, actual bearer authorization, native image tool exposure/header, automatic local-history list/read/resume, continued messages retained after undo, direct API-to-official switching and history readability across two synthetic credential changes.
- Failed automatic synchronization was injected: config/auth restored, failure stage and rollback logged, no synthetic keys or private conversation sentinel in diagnostics/stderr.

## Assets

| Asset | Bytes | SHA-256 |
| --- | ---: | --- |
| YilaiCodexSwitcher.exe | 4882944 | 25a9f332e9ca32800825cfd7165a3a6a11f14eea41b5affb342aab4d61fe6949 |
| YilaiCodexSwitcher-macOS-universal.dmg | 5919849 | 1f933e00813a51eeeed5a9913e0db67b7818ebd61b4617e1485566220ad225d6 |
| YilaiCodexSwitcher-macOS-universal.zip | 5198591 | fc319903edb4fefaa394fbd00597173aed5bfc5e7e8d2ed2744d55b825e4f0f9 |

## Boundaries

- Exit Codex and CC-Switch before switching. Later CCS switches should use its unified Codex history setting.
- History synchronization covers the same local CODEX_HOME, including archives; it does not download other accounts' cloud history.
- Current auth.json is deleted during either switch. System credential stores, archived auth files and CCS account databases are not cleared.
- Configuration rewriting normalizes TOML formatting and does not retain comments.
- Production paid image generation and actual ChatGPT login were not exercised. Switching does not validate quota or server-side model access.
- Development and tests did not mutate real user credentials/history or CCS source/settings.
