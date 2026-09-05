# v3.2.1 Release Manifest

- Date: 2026-09-05 (Asia/Shanghai).
- Repository: kingduoyu/yilai-codex-switcher.
- Previous source: 94324a6fa9cd3cc8f000dd5d29583067b23e7910 (Windows v3.2.0).
- Scope: macOS 13+ universal app, version 3.2.1 (build 8). Windows keeps the accepted v3.2.0 executable unchanged. No Sub2 application deployment.

## Changes

- Embed the same fixed model catalog as Windows: gpt-5.6-sol, gpt-5.6-terra, gpt-6-astra.
- Replace old and duplicate catalog settings, preserve initial auth backups, and remove the fixed catalog selection when returning to official mode.
- Roll back the config and catalog to the state before the current operation on failure.
- Retain the existing macOS Keychain isolation, native input controls and error alerts.
- Add catalog JSON shape and three-model membership checks to the existing isolated self-test. Existing migration and rollback tests run with the formal macOS build.

## Build And Publication

- Formal build: existing Build macOS app GitHub Actions workflow on macos-14, arm64 and x86_64 merged by lipo.
- Build commit: 69612b0ff2660d2241c786779c9c0e0dfcb773f4.
- Actions run: 33938933352, success. Both architectures compiled; isolated self-test and hdiutil verification passed.
- UI proof: implementation-macos.png, 960 x 650, visually checked as v3.2.1 with intact controls and no exposed key.
- Status: formal artifacts accepted for publication; no additional build or repeat test suite.
- Windows SHA-256 remains 55f6e3954185bbb391d11ff89c5810b15e24a1e7b0caafe1c819aba34ece692a; no Windows rebuild or repeat acceptance suite.

| Asset | Version | SHA-256 |
| --- | --- | --- |
| YilaiCodexSwitcher.exe | Windows 3.2.0, unchanged | 55f6e3954185bbb391d11ff89c5810b15e24a1e7b0caafe1c819aba34ece692a |
| YilaiCodexSwitcher-macOS-universal.dmg | macOS 3.2.1 | 82c14bf8026698c09e6ea946084bcdaa036269b65bc051866c939fd6aa613015 |
| YilaiCodexSwitcher-macOS-universal.zip | macOS 3.2.1 | 68b53bff9bd2fe4122f7f847055ef79baa53b9c344aa69cdf2c5675b0ef64781 |

## Announcement

- After both installers are available, update the relevant existing live announcement to direct users who cannot obtain models to reconfigure with the switcher, then restart Codex.
- Preserve other announcement content and visibility settings. Record the selected announcement and final verification here after the update.
- Selected live announcement: ID 25, GPT-6 已上线, active/popup, targeting all users, no schedule limit. Replace its old restart-only guidance, not unrelated announcement ID 24.
- The Sub2 development baseline check detected pre-existing CI worktree changes; preserve those changes and do not build or deploy Sub2 for this content update.
