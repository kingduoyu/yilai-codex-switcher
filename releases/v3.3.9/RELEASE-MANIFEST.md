# v3.3.9 Release Manifest

Application source: 0e4cb08258720f2cb3950ecd256f5f6a69808314.

Existing API and official buttons automatically migrate only legacy yilai session metadata and SQLite provider rows to custom. No standalone history button. openai, ccswitch and unrelated providers remain unchanged. Message bytes, titles, archive state and inherited parent metadata are preserved. Backups, rollback and interrupted-operation recovery remain enabled. Both connection routes define custom and a matching yilai compatibility alias; official definitions contain no API URL/key/headers. Missing official auth requires login; no auth backup restoration.

Verification: Windows build, native app/history self-tests, Tests/unified-history.py and Tests/windows-ui.py passed. Synthetic migration measured 0.056 seconds; this is not a large-history guarantee. Codex CLI 0.153.4 integration dist/runtime-test-ACA66a/result.json passed: a real runtime-generated yilai rollout/index migrates during official switch, loads through official configuration, and resumes with a mock API response. Managed three-model catalog and native image tool/header passed.

Mac CI https://github.com/kingduoyu/yilai-codex-switcher/actions/runs/34668302995 succeeded, including native self-tests, universal build and DMG validation. Downloaded ZIP independently verified as 3.3.9/build21 with Intel and Apple Silicon Mach-O slices. Windows and Mac screenshots inspected: two switching actions, reset, no history button.

Limits: no production official OAuth send, paid image generation, or Intel hardware execution tested. No full CCS account-management parity claimed. First history scan depends on data volume. Mac ad-hoc signed, not notarized. User live configuration/history was not changed in tests.

| Asset | Bytes | SHA-256 |
| --- | ---: | --- |
| YilaiCodexSwitcher-v3.3.9.exe | 5237248 | 279038c85a9afafcb2ee8251e3f4161220ef6902aa5e3d18480ea9cc6042a79f |
| YilaiCodexSwitcher-v3.3.9-macOS-universal.dmg | 6156938 | 0ac140798de4a2c1965f979081fef7e07155cf670390f9cd89ce50a17ae994db |
| YilaiCodexSwitcher-v3.3.9-macOS-universal.zip | 5397219 | 8ff98cad1ff58bb908f5536896f1c50b29d9d24e501775affd7cf667c5417b67 |
