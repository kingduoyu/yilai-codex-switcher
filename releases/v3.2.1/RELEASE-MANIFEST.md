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
- Status: awaiting the formal build, isolated self-test, DMG verification and UI screenshot. Do not publish before these checks pass.
- Windows SHA-256 remains 55f6e3954185bbb391d11ff89c5810b15e24a1e7b0caafe1c819aba34ece692a; no Windows rebuild or repeat acceptance suite.

## Announcement

- After both installers are available, update the relevant existing live announcement to direct users who cannot obtain models to reconfigure with the switcher, then restart Codex.
- Preserve other announcement content and visibility settings. Record the selected announcement and final verification here after the update.
- The Sub2 development baseline check detected pre-existing CI worktree changes; preserve those changes and do not build or deploy Sub2 for this content update.
