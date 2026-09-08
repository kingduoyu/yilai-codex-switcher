# v3.2.3 Windows / macOS Release Manifest

- Date: 2026-09-08 (Asia/Shanghai).
- Status: published as latest v3.2.3; draft=false verified through GitHub API.
- Published at: 2026-09-08T01:35:22Z (2026-09-08 09:35:22 Asia/Shanghai).
- Release: https://github.com/kingduoyu/yilai-codex-switcher/releases/tag/v3.2.3
- Tag target: aa14f4f26eb4de9872d764b6886e95c3dbd4f2cb.
- All four assets verified uploaded with remote SHA-256 digests matching local files.
- SHA256SUMS.txt SHA-256: 64BE33E7E5C471FB779320CAEC12387284895211EF6E032BF63783A071A3FB64.
- Baseline public main: 2009168c00f46ae4882245dabcecdcd4f0bae829 (v3.2.2).
- Implementation: 76230edddf75d4502125991214aa521d6a7ea305; candidate validation: 8fd8052.
- Scope: structural config.toml rewrite shared by Windows and macOS; version and release metadata.
- No changes to history databases, session files, plugin directories or credential cleanup logic.

## Behavior

- Replace model_providers.yilai, including nested headers and old credentials; select yilai,
  use file credentials, write the model/catalog reference, enable image_generation.
- Remove forced_login_method, forced_chatgpt_workspace_id and openai_base_url conflicts.
- Preserve unrelated configuration values and inactive provider definitions in API mode.
- Expand a selected legacy profile into the root, preserving effective permissions/settings;
  remove the legacy selector rejected by runtime 0.153.4. Profile tables remain.
- Official mode retains the previous contract of clearing custom provider definitions.
- Formatting/order are normalized; comments are not preserved.
- Invalid TOML/duplicate keys/embedded NUL/missing selected profiles fail before writes.
- Mac preserves file credential storage, legacy auth.json.yilai-session-* cleanup, atomic
  replacement and in-memory rollback. No system Keychain deletion.

## Verification

- Candidate Windows Release build, shared C++17 core self-tests and final integration script passed.
- Runtime 0.153.4 in isolated homes: no fallback/config warning, requiresOpenaiAuth=false,
  image_generation=true, model/list contains gpt-5.6-sol, gpt-5.6-terra and gpt-6-astra.
- Candidate Mac Actions 34176273601 passed both architecture builds, universal --self-test,
  ad-hoc signing, DMG verification and UI screenshot. Intel slice was not separately run.
- Tests cover nested/inline headers, effective profile settings, unrelated values, malformed
  inputs, repeat switching, credentials cleanup and locked-file rollback.
- No real user profile or credentials were changed. No live model/image call was made.
- Real-account macOS desktop login and online image generation remain unverified; user cannot
  perform that test. Do not claim universal compatibility or guaranteed absence of login UI.
- macOS signing remains ad-hoc, not Developer ID notarization. Windows remains unsigned.

## Dependencies

- toml++ v3.4.0, MIT, original license retained in Sources/ConfigRewrite/vendor/toml.hpp.
- Header SHA-256: 6B5172AD4DD6519AEC67B919181FA7A38A2234131E5B2AFA232DFE444819783E.
- Existing Actions workflow/tool versions unchanged. Only necessary shared-source build wiring changed.

## Formal Artifacts

- Build source: ffb5dc2 (version changes only after the validated implementation).
- Windows Release build and --self-test passed for v3.2.3.
- Formal macOS Actions run: 34176864743, success, built from main.
- Both architectures compiled, universal --self-test passed on Apple Silicon, ad-hoc codesign
  completed, DMG hdiutil verify VALID, screenshot inspected with v3.2.3 label.
- Downloaded DMG/ZIP hashes match the runner log. No live-account verification was added.
- EXE SHA-256: 2EBF00D0128A81F14718F16C29E9233848F1EFAD88D145072E8D548FF1FD4D7B.
- DMG SHA-256: 7C5B18583F041749C2AEA92F9B3DE9209DA96427DA5871A6DBDF628A17E21F36.
- ZIP SHA-256: 72790A73562798C49A487C92439DD09E07AF7C8EC4F468C83339F1A5837CE1D2.
- Local artifacts: 配置器/publish/release-v3.2.3/{win-x64,mac-universal}/.
