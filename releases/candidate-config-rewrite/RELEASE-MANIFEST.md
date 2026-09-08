# Config TOML Rewrite Candidate

- Date: 2026-09-08, Asia/Shanghai.
- Status: candidate branch built and validated on macOS; not a public release.
- Branch: candidate/config-rewrite-macos. Main and release downloads are unchanged.
- Baseline: released v3.2.2. Public Windows executable is unchanged.
- Scope: config.toml generation and configuration tests. No UI features, history migration,
  credential cleanup changes, directory detection, or background process management were added.

## Behavior

- Both platforms use the same toml++-based C ABI rewriter, stored in
  src/YilaiCodexSwitcher.macOS/Sources/ConfigRewrite.
- API mode replaces the entire model_providers.yilai node, including stale nested headers,
  selects yilai, sets the model/catalog path, enables image_generation, uses file credentials,
  and removes forced_login_method, forced_chatgpt_workspace_id, and openai_base_url overrides.
- Other provider definitions, MCP configuration/credentials, plugins, project settings,
  notifications, instructions and unrelated features/permissions are retained by value.
- If the legacy root profile selector is present, its effective settings are merged into the
  root and the selector is removed. Existing profile tables are retained. The selected
  profile's model settings are aligned before merging; its effective permissions are kept.
  This avoids the verified 0.153.4 error that rejects legacy profile selectors.
- Official mode retains the existing contract of removing custom provider definitions.
- Serialization normalizes formatting/order and does not retain comments. Configuration
  values, including multiline strings, remain intact outside the managed scope.
- Malformed TOML (including duplicate keys), invalid expected table shapes, missing selected
  profiles, and embedded NUL input fail before configuration/auth/catalog writes.
- Existing atomic writes and in-memory failure rollback remain unchanged. No real user
  configuration, credentials, sessions, or databases were modified during development.

## Verification

- Final tests/Test-ModelCatalog.ps1 run: PASS, Codex app-server 0.153.4.
- Windows Release build and --self-test: PASS.
- Shared core also compiled as C++17, matching the macOS package language setting.
- Tests cover nested and inline provider headers, stale credentials, disabled image settings,
  active legacy profiles, preservation of MCP/plugin/permission values, multiline content,
  escaped values, repeat-write idempotence, and malformed-input no-write behavior.
- Real runtime in isolated homes: no configWarning/default fallback; account/read reports
  requiresOpenaiAuth=false; config/read reports yilai and image_generation=true; model/list
  returns gpt-5.6-sol, gpt-5.6-terra, gpt-6-astra for both old-user and fresh-user fixtures.
- No paid model turn or image-generation request was made; API requests are redirected to
  loopback in the integration test. This is configuration validation, not a live Key test.
- Windows and macOS applyConfiguration blocks were compared with the released source and
  are unchanged. macOS native build, ad-hoc signing and isolated self-tests passed.
  Real desktop login and live third-party model/image calls remain unverified.
- Candidate Windows SHA-256: 4309E4FADF807B6AA73939136603A2864B21D57F3EB2B897809CC020273FE2E0
- Baseline Windows SHA-256: BFAF18F6A22230D0E75357DB572A0E9CB0823E531B126463B711B86E8B5025E1

## Dependency And Release Boundary

- toml++ v3.4.0, MIT, original license included in vendor/toml.hpp.
- Header verified byte-for-byte against marzer/tomlplusplus tag v3.4.0.
- Header SHA-256: 6B5172AD4DD6519AEC67B919181FA7A38A2234131E5B2AFA232DFE444819783E.
- Candidate branch contains the shared Sources/ConfigRewrite target, Package.swift and
  platform adapters. Windows build paths are adapted to ../Sources/ConfigRewrite.
  Main and release downloads remain unchanged; do not describe this as a public release.

## macOS Candidate Validation

- Build source: 76230edddf75d4502125991214aa521d6a7ea305.
- Branch: candidate/config-rewrite-macos.
- GitHub Actions run: 34176273601, success. Existing workflow was not changed.
- Native arm64 and x86_64 builds completed; ZIP executable FAT header confirms both slices.
- Universal app --self-test passed on the Apple Silicon runner. Intel slice compiled but
  was not separately executed. Tests include file credential selection, legacy login
  archives, structured config rewrite, existing settings preservation and locked-file rollback.
- Existing ad-hoc codesign completed; hdiutil verify reported VALID. Not Developer ID notarized.
- implementation-macos.png inspected: normal layout, no errors, no real credentials.
- Candidate retains the v3.2.2 UI/version label; distinguish it by candidate path and hashes.
- DMG SHA-256: CE221D915813DE41E1B69340995540159ABA3168ED5F827F723F8FD39A0C5170.
- ZIP SHA-256: E9F8157D9E539AD12BF0836BA3A0A5C86B9D26307BD46BD69295A18B42E7FFF0.
- Downloaded hashes match the runner log. Files are in publish/candidate-config-rewrite/mac-universal.
- Windows public-repo-layout build and --self-test passed after build path adaptation.
  Its SHA-256: 6C0DFCD386932229D62E878FCF926A6A19137D08A086C70646EF26B476A117C4.
