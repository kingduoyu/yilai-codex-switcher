# v3.3.6 Release Manifest

Application source (both platforms): a3a9017b780430d471d7dc839fb181c02beb467d.

Scope: API configuration + automatic image enablement; rename-only reset and failure logs. Removed history migration/recovery and SQLite dependency. Probe databases isolated to temporary storage. Existing models/catalog pointers retained; no model fetching or catalog generation. Manual local official-login reset is not application code.

Verification: Windows build/native self-test, real GUI failure tests and source integration passed (dist/source-test-4001f_ss/result.json). Final Codex 0.153.4 runtime integration passed (dist/runtime-test-MNaLtH/result.json): model preservation, API bearer/image tools/mock response, history unchanged and probe isolation. Windows screenshot inspected.

macOS workflow https://github.com/kingduoyu/yilai-codex-switcher/actions/runs/34617468102 succeeded: arm64/x86_64 builds, native self-test, DMG checksum and screenshot. Downloaded ZIP Info.plist 3.3.6/build18 and both Mach-O architectures independently verified; screenshot inspected.

Limits: mock API tests, no paid image or official-login end-to-end verification. macOS ad-hoc signed, not notarized; no Intel hardware execution. CCS external-change backfill conflict remains; select API in CCS before exiting CCS/Codex and applying our configuration. Do not claim official switching is repaired by this release.

| Asset | Bytes | SHA-256 |
| --- | ---: | --- |
| YilaiCodexSwitcher.exe | 3800064 | a05deaeb2029f8fb854e75e6b04adb853619c3d9c47ccae45e688941a1488d3f |
| YilaiCodexSwitcher-macOS-universal.dmg | 4760690 | 53da1a110aa2b158bf19dd35d97fa5e6f33d8e20e81013894b017a546be606eb |
| YilaiCodexSwitcher-macOS-universal.zip | 3969367 | b387bf29e740201f3f28550cb629c0bd473bbc14fdc80046e70e1277d762a451 |
