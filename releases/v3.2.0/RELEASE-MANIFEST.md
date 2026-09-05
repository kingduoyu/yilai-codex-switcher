# v3.2.0 Release Manifest

- Date: 2026-09-05 (Asia/Shanghai).
- Repository: kingduoyu/yilai-codex-switcher.
- Source reference: tag v3.2.0; the GitHub Release is pinned to its exact commit.
- Previous repository HEAD: 805c1b456a9e99ec933cd63ceaec14d7e65ad3aa (origin/main matched before publication).
- Scope: Windows 10/11 x64 only. No Sub2 server deployment, upstream merge, macOS build, or macOS candidate source publication.

## Changes

- Write an embedded catalog containing only gpt-5.6-sol, gpt-5.6-terra and gpt-6-astra. Default selection remains Sol.
- Replace old top-level model_catalog_json entries, including duplicates, and overwrite the managed catalog on each switch.
- Preserve the initial auth backup, unrelated configuration and old models_cache.json. Roll back config and catalog together after a failed switch.
- Remove the catalog selection when switching back to official mode. Users must fully exit Codex and CC-Switch before switching, then restart Codex.
- The catalog is fixed, not synchronized with future server whitelist changes. Server authorization remains unchanged.

## Existing Verification

- Reuse the accepted Windows candidate; do not rebuild or rerun the same suite for publication.
- Windows --self-test: exit 0, including repeated switches, old-user migration, auth preservation, rollback and official recovery.
- Codex app-server 0.153.0 model/list: exactly the three expected models for fresh users and migrated users, including an obsolete catalog and a cache containing Luna.
- Migration without an existing catalog setting and repeated switching also passed.
- Verification used isolated CODEX_HOME directories and dummy keys, not live user authentication or inference requests.
- Older Codex runtime compatibility is outside this release scope.

## Assets

| File | Version | SHA-256 |
| --- | --- | --- |
| YilaiCodexSwitcher.exe | Windows 3.2.0 | 55f6e3954185bbb391d11ff89c5810b15e24a1e7b0caafe1c819aba34ece692a |
| YilaiCodexSwitcher-macOS-universal.dmg | macOS 3.1.1, unchanged | ff6ebb289347a8bf99e62610f39360ba896b051e401fbc32ed373b79eea93af9 |
| YilaiCodexSwitcher-macOS-universal.zip | macOS 3.1.1, unchanged | 314c1a05ff9bc68c11aed47390ddbcea455bf4647d65eca9003e2a472a57b9db |

The macOS files are copied byte-for-byte from release v3.1.1 solely to preserve existing latest/download links. They do not contain the new Windows model-catalog feature. SHA256SUMS.txt covers all three distributable files.

## Source Matching

The accepted build source and public repository working copy matched byte-for-byte before committing:

| File | SHA-256 before Git line-ending normalization |
| --- | --- |
| Windows/config.cpp | 103b5df4ee13475f9b06945b9a0f9bc025dc30c6e3aab75a3095518e78c00b0d |
| Windows/config.hpp | 614760ba1fa7dcddaafb22ecdaf72912908cb45845861ae1f65fb7c840c3348e |
| Windows/main.cpp | 3daec2459c8674a820375167719eb920ddf9313923d8f887914dac2bbd2366ae |
| Windows/model_catalog.hpp | 99146ac5eb902b4c8a0144d78c6a06b2c24bb52b9134613a222bc7fd6a8272c8 |
| model-catalog.json | eadc0bbf30e37b349c505925519dc947d2ce3c307bea4cd1a82e3c49475035c2 |
