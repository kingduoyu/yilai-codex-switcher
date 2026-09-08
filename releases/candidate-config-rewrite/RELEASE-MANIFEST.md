# Config TOML Rewrite Candidate

- Date: 2026-09-08, Asia/Shanghai.
- Status: candidate branch prepared for macOS Actions validation; not a public release.
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
  are unchanged. macOS native build, signing and desktop login verification remain pending.
- Candidate Windows SHA-256: 4309E4FADF807B6AA73939136603A2864B21D57F3EB2B897809CC020273FE2E0
- Baseline Windows SHA-256: BFAF18F6A22230D0E75357DB572A0E9CB0823E531B126463B711B86E8B5025E1

## Dependency And Release Boundary

- toml++ v3.4.0, MIT, original license included in vendor/toml.hpp.
- Header verified byte-for-byte against marzer/tomlplusplus tag v3.4.0.
- Header SHA-256: 6B5172AD4DD6519AEC67B919181FA7A38A2234131E5B2AFA232DFE444819783E.
- Before any future release, synchronize the shared Sources/ConfigRewrite target,
  Package.swift, platform adapters and tests to the public repository. Public Windows build
  paths must refer to ../Sources/ConfigRewrite rather than the local sibling macOS project.
- Do not distribute this candidate as a validated macOS release.
