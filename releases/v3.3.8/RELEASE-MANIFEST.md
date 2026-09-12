# v3.3.8 Release Manifest

Application source: 4e96749406ce8fbd8b32fe6335718aa2ef821403. Both platforms use the shared history module and catalog/config rules.

Explicit local-history unification migrates eligible legacy provider metadata and SQLite rows to custom. Known CCS legacy ids, local configured ids and openai are eligible; unknown private ids remain unchanged. First SessionMeta only; fork-parent metadata and message bytes are preserved. History operation uses backups, rollback and interrupted-operation recovery; it never rewrites config/auth. API and official switches use custom with synchronized yilai compatibility definitions. Official routes have no third-party URL, key or headers. Ordinary switches do not scan history; the separate history action may take longer on large stores. No CCS DB mutation or cloud history fetching.

Verification: Windows native self-test, history self-test (failure injection and interrupted transaction recovery), GUI failure recovery passed. Tests/unified-history.py passed JSONL/SQLite migration, unknown-id preservation, titles/archive/message preservation, idempotence without another backup, and API/official route round trips (synthetic migration 0.069 seconds). Real Codex 0.153.4 integration dist/runtime-test-wOCmy8/result.json passed: real runtime-created rollout/index changed to yilai then unified; official config runtime reads the unified thread; API resumes it and receives mock response; exactly three models and image tool/header verified.

Mac CI https://github.com/kingduoyu/yilai-codex-switcher/actions/runs/34664894196 succeeded, including history self-test, arm64/x86_64 builds, DMG verification and screenshot. Downloaded ZIP version 3.3.8/build20 and Mach-O architectures verified independently. Both UI screenshots inspected.

Limits: no production official-login send, no paid image request, no Intel hardware execution. Existing official auth is preserved but missing auth requires login; no deleted auth snapshot restoration implemented. Unification is user-triggered, not CCS startup automation. Do not claim full CCS feature parity or ten-second migration guarantee. Mac ad-hoc signed, not notarized.

| Asset | Bytes | SHA-256 |
| --- | ---: | --- |
| YilaiCodexSwitcher-v3.3.8.exe | 5243904 | bd8a53a3d1c8efc2229589ce6330e0ce9089e70155846ce3693e0788a120ce69 |
| YilaiCodexSwitcher-v3.3.8-macOS-universal.dmg | 6164061 | ebfc5e9db89e363ae22fb0527c673efeb4218ecc188491b6431252376dbdaad5 |
| YilaiCodexSwitcher-v3.3.8-macOS-universal.zip | 5404337 | 6160089d01ab5c8f9751b416472967e12fd7d0ccf0c4406ef3fe691c76ecfc3c |
