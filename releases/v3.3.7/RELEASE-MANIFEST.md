# v3.3.7 Release Manifest

Application source (both platforms): 520673d77205a778b2a9956c7cd2ff3cd48fb0c7.

Main UI flow performs local file operations, without launching Codex for priority inspection or runtime readback. Source diagnostics remain opt-in for maintenance/tests. Embedded catalog writes exactly sol, terra and astra plus root/active-profile pointers. Failures restore prior catalog/config. UI/log version 3.3.7, macOS build19; distribution filenames include the version.

Windows native self-test and GUI failure recovery passed. Real Codex 0.153.4 integration passed: dist/runtime-test-Xa5wmU/result.json, exactly three models, image tool/header, mock Responses, authentication, idempotence and history preservation. Additional local checks passed: fresh user, catalog overwrite, Unicode/space paths, active/inactive profiles, catalog collision preservation, no runtime probe stages. Ten isolated runs measured 0.012771 to 0.019528 seconds, median 0.013077; includes driver startup, excludes apps-closed guard. Not a universal timing guarantee.

macOS CI https://github.com/kingduoyu/yilai-codex-switcher/actions/runs/34623452246 succeeded: both architectures, native self-test including immutable auth/catalog rollback, DMG checksum and screenshot. Downloaded ZIP version/build and Mach-O architectures verified. Both screenshots inspected. Shared embedded bytes match model-catalog.json.

Limits: no paid image-generation or production official-login test, no Intel hardware execution. Mac is ad-hoc signed, not notarized. Reset renames only config.toml. No source priority handling in main flow. CCS external-change backfill conflict remains documented; no CCS DB/history mutation.

| Asset | Bytes | SHA-256 |
| --- | ---: | --- |
| YilaiCodexSwitcher-v3.3.7.exe | 3885568 | 5953b64ec3d54b627a6c7b3062a0f92c1e13ff14cc50eec02e521bb39f1ee244 |
| YilaiCodexSwitcher-v3.3.7-macOS-universal.dmg | 4783328 | cf0d42a8c30b1d4fe9f4b642fb59869480ccb5232a0c67ab20dce9349dfdabab |
| YilaiCodexSwitcher-v3.3.7-macOS-universal.zip | 4001626 | 87ada18a85db1dfddc410e0090ebf282fcc0254fb0219e50b19dbee01eeb57e2 |

Publication confirmed: 2026-09-11T16:46:13Z UTC, latest v3.3.7, not draft/prerelease. Four remote asset hashes/sizes and local publish copies verified. Release target b0d06d9; application source unchanged from 520673d.
