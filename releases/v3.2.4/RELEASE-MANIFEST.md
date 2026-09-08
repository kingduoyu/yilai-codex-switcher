# v3.2.4 Windows / macOS Release Manifest

- Date: 2026-09-08 (Asia/Shanghai).
- Status: artifacts verified; ready for draft upload, not yet published.
- Baseline: eae00b1 (published v3.2.3).
- Build source: a22cf1c (v3.2.4).
- Scope: recoverable cleanup of old credential and managed backup files; version metadata.

## Behavior

- Windows uses IFileOperation with FOFX_RECYCLEONDELETE; macOS uses FileManager.trashItem.
- Cleanup targets are unchanged: auth.json, auth.json.yilai-disabled, managed legacy backup
  files, plus auth.json.yilai-session-* on macOS. Empty legacy directories may be removed.
- No permanent-delete retry is used when recycling fails; existing in-memory rollback remains.
- Recycled credentials are not automatically restored. A failed switch may leave recycled
  copies in addition to restored originals. Users must protect credentials in the trash.
- config.toml rewrite, model catalog, MCP, plugins, permissions and histories are unchanged.
- No real user configuration, authentication files or histories were used in tests.

## Verification

- Windows Release build passed; executable --self-test and Test-Recycle.ps1 passed.
- Fifteen synthetic files were found in the Windows Recycle Bin; auth bytes matched fixtures.
- Existing locked-file rollback, repeat switches and shared config rewrite tests passed.
- Formal macOS Actions run: 34179665173, success (50 seconds), built from a22cf1c.
- New Mac self-test checks retained trash contents and same-name collision preservation.
- Mac arm64 and x86_64 compiled; universal --self-test passed on Apple Silicon.
- Trash content/collision and immutable-file rollback checks passed; Intel slice not separately run.
- Ad-hoc signing and DMG hdiutil verification passed. v3.2.4 UI screenshot inspected.
- No real-account login or live image-generation test.
- Windows remains unsigned; macOS remains ad-hoc signed, not Developer ID/notarized.
- Existing dependencies and CI configuration unchanged.

## Artifacts

- Windows EXE SHA-256: A17089ACA2B78FB2946E838EECD0B8BE3EBB6075D8DC4986AFF5F6060AD9B666.
- DMG SHA-256: 4733FE57AADF4FE298A1C526EE846ABA7D624FCFEC8A6732D23173EB5CDC6517.
- ZIP SHA-256: 614165123AB8F86868C364FB79F017A859E2DA59AF86C3E9D48FA3ED404C383E.
- Local artifacts: 配置器/publish/release-v3.2.4/{win-x64,mac-universal}/.
