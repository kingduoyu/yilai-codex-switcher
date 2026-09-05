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
- Status: published as the latest non-prerelease v3.2.1 at 2026-09-05T02:31:32Z.
- Release commit: 0757599992cf6ed49ff0e634d650762234866445. Its differences from the build commit are documentation and checksum files only.
- All four uploaded asset digests and sizes matched local artifacts before publication. The existing Windows and macOS latest/download entries both returned HTTP 200 after publication. No additional build or repeat test suite.
- Windows SHA-256 remains 55f6e3954185bbb391d11ff89c5810b15e24a1e7b0caafe1c819aba34ece692a; no Windows rebuild or repeat acceptance suite.

| Asset | Version | SHA-256 |
| --- | --- | --- |
| YilaiCodexSwitcher.exe | Windows 3.2.0, unchanged | 55f6e3954185bbb391d11ff89c5810b15e24a1e7b0caafe1c819aba34ece692a |
| YilaiCodexSwitcher-macOS-universal.dmg | macOS 3.2.1 | 82c14bf8026698c09e6ea946084bcdaa036269b65bc051866c939fd6aa613015 |
| YilaiCodexSwitcher-macOS-universal.zip | macOS 3.2.1 | 68b53bff9bd2fe4122f7f847055ef79baa53b9c344aa69cdf2c5675b0ef64781 |

## Announcement

- Updated after both installers were publicly available: 2026-09-05T10:31:53.720674+08:00.
- Live announcement ID 25, GPT-6 已上线: replaced the old restart-only guidance with instructions to use the latest switcher when models cannot be obtained, then restart Codex. Included the verified Windows/macOS download entries and noted that old users need not delete configuration or chat history.
- Updated through the existing admin announcement API using a content-only request. Read-back verified the exact content and preserved title, active/popup status, targeting, scheduling and creation metadata. Unrelated announcement ID 24 and existing read markers were not changed.
- The original announcement was backed up on the production server before writing: announcement-25-before-switcher-v3.2.1-20260905T023153Z.json. No credentials were written into the backup or this manifest.
- Final content UTF-8 SHA-256: 9a848b1bcbbc45b40854eb1f0838fb19f850126de0a5b96f65e63902b0d799a2.
- The Sub2 development baseline check detected pre-existing CI worktree changes; preserve those changes and do not build or deploy Sub2 for this content update.
