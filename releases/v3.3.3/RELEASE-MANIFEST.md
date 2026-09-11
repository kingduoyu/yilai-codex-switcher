# v3.3.3 Release Manifest

Date: 2026-09-11 (Asia/Shanghai).
Publication confirmed: 2026-09-11T12:23:17Z (UTC). GitHub latest is v3.3.3, neither draft nor prerelease. All four remote assets matched local digest, byte length and uploaded state before publication; final published application assets were rechecked. Local publish/ binaries, checksum file and UI previews were hash-verified.
Release tag commit: 20dfd264f995a6dd879b3258deb542ac9db8f9f0.

Application source commit (both platforms): 685e43cb3f4e24e8bf08d0ff6758eb21e50a6bff. Subsequent release-tag changes are release documentation/checksums only.

## Scope

- Before either main switch, read actual loaded config layers and field origins using the installed Codex app-server config/read.
- For current local active projects, back up and remove loaded higher-priority connection/image overrides before writing the user connection. Preserve model/catalog, MCP, permissions and unrelated configuration; do not scan inactive profiles or modify untrusted project layers.
- Read effective configuration again and verify provider, authentication, endpoint and API image settings before history synchronization. On failure restore source, config and auth edits with external-change checks.
- Source backup journals permit recovery of interrupted source edits and remain pending if recovery fails. Do not durably complete the source journal until the outer history operation succeeds.
- Preserve the two main buttons, automatic local history synchronization, current auth.json deletion and rename-only config reset. Do not modify CCS itself.

## Verification

- Final Windows x64 build and native --self-test passed. GUI regression Tests/windows-ui.py passed: two actual failures expose an enabled logs button, retain isolated config and write failure records. Final screenshot dist/windows-v333.png inspected.
- Real Windows Codex 0.153.4 integration passed: dist/runtime-test-rY4sD1/result.json. Confirms API account=null/requiresOpenaiAuth=false, actual mock bearer authentication, native image tool/header, gpt-6-astra catalog retention, automatic local history list/read/resume, official switching and failure rollback/log privacy. Isolated data homes and localhost mock provider; no paid requests.
- Final source integration passed: dist/source-test-opag1cx1/result.json. Covers first-use empty home, trusted parent+child connection overrides with unrelated settings retained, byte-identical untrusted project config, history failure restoring project/user/auth, and unreadable source rejecting before mutation.
- Source journal/schema stub checks passed: missing/malformed layers and unsupported profile paths rejected; interrupted edits recover; verification does not commit the journal; finish commits; external changes survive failed rollback with recoverable markers retained.
- Windows runtime transport checks passed: real config/read, invalid JSON, stderr output cap, exit17 and 20-second timeout with child process cleanup.
- macOS universal workflow https://github.com/kingduoyu/yilai-codex-switcher/actions/runs/34598259099 succeeded at the application source commit. Both architectures compiled; native macOS self-test passed including POSIX stub protocol probe, missing-runtime preservation and existing config/history/auth rollback checks. DMG verification passed; final screenshot inspected.
- Downloaded macOS ZIP independently checked: version 3.3.3 build 15, Mach-O universal arm64+x86_64. Ad-hoc signed; not notarized. Native self-test ran on arm64, no Intel hardware execution claimed.

## Boundaries

- Verification covers default local startup and the desktop state's current active local workspaces. Arbitrary independently launched CLI --profile/-c, future workspaces and later launch parameter changes are outside scope. Current app-server cannot select an independent CLI profile; scanning files is not treated as proof of selection.
- Unknown/unreadable active sources fail explicitly; system/organization policy is not removed. Close Codex and CCS before switching. The configurator lock does not coordinate arbitrary external writers.
- A real macOS Codex config/read integration was not run; macOS uses the shared source core with a native POSIX protocol stub test. Real runtime acceptance was performed on Windows.
- Tests do not modify real user configs, credentials/history or CCS. No real paid image generation, production Key entitlement or actual ChatGPT login was tested. Shared history remains limited to existing local records.

## Assets

| Asset | Bytes | SHA-256 |
| --- | ---: | --- |
| YilaiCodexSwitcher.exe | 5162496 | 543fb35705d7d9ab85a78fc5ff6072609b78d281f541c0a8758f9d5d8563f8fd |
| YilaiCodexSwitcher-macOS-universal.dmg | 6124265 | 40002881eb8a6c1e6596b159f11406d303363049455a80f60e3083deca457206 |
| YilaiCodexSwitcher-macOS-universal.zip | 5390404 | fdcec2c74f41cae0d18e61eaa141c3afa4d0d8995ecef0fd314c2b8c286c0613 |
