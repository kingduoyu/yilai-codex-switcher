# Model catalog revision 2

- Published on 2026-10-27 (Asia/Shanghai). Catalog-only publication; software stable latest remains v3.4.1 and no binaries or tutorial version were changed.
- Added only `gpt-6.1-terra` / `GPT-6.1-Terra`. All five previous model objects are unchanged. Luna and internal models are excluded.
- Model ID, default reasoning `low`, reasoning levels `low, medium, high, xhigh, max, ultra`, context window `500000`, maximum context window `872000`, and priority tier were checked against the live upstream catalog returned by the main site's account model synchronization endpoint. Existing client-compatible tool settings and Terra instruction template are retained.
- Immutable catalog commit: `8cdc37b52c77525d3dd703feed94812689867e95`.
- Channel commit: `08b787250efbd3713f89a4e50cb6b72a159be9b1`.
- Catalog URL: `https://raw.githubusercontent.com/kingduoyu/yilai-codex-switcher/8cdc37b52c77525d3dd703feed94812689867e95/model-catalog.json`.
- SHA-256: `87c4be2262e8b1509797714a7b69d984f3cf7917847a2e2c00e5ed71e05e440d`.

## Validation

- `Tools/Publish-ModelCatalog.ps1`: six models validated. `-Publish -WhatIf` passed; `-Publish` pushed the catalog and channel. Its final HTTP verification initially received cached revision 1; the publisher was not rerun.
- GitHub contents API and `git ls-remote` confirmed the published revision 2 channel and exact main commit. The immutable catalog download matched the declared SHA-256 and contained exactly the intended six models.
- `dist/update-protocol.exe model-catalog.json`: passed catalog validation, additive update checks, immutable channel checks, and future model preservation.
- `python Tests/model-update.py dist/test-driver.exe`: passed model-only update, unchanged auth/config/history, stale and duplicate catalog rejection, and preservation through later API configuration.
- An isolated existing-client installation preserved config.toml and installed the exact new catalog. Real `codex-cli 0.163.0-alpha.10.2` app-server listed all six models, exposed the new model and its reasoning levels, and retained `gpt-6.1-terra` as the configured selection. No paid inference or user profile modification was performed.
- `dist/update-network.exe models`: native Windows HTTPS download and checksum verification passed, reporting `live channel revision 2, 6 models`. Some PowerShell HTTP requests still returned cached revision 1 during verification; those responses were not treated as a successful check.

Users on v3.4.0 or later can close Codex and CC-Switch, click Update Models in the configurator, and reopen Codex. A software upgrade is unnecessary. Updating the catalog does not enable models disallowed by server account or group policies.
