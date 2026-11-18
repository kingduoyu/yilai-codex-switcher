# Model catalog revision 3

- Published on 2026-11-18 (Asia/Shanghai). Catalog-only publication; no software release, binaries, tutorial, or user Codex configuration were changed.
- Added only `gpt-6.2-sol` / `GPT-6.2-Sol` and `gpt-6.2-astra` / `GPT-6.2-Astra`, for eight models total. All six existing model objects remain unchanged.
- IDs, names, descriptions, default reasoning `low`, reasoning levels `low, medium, high, xhigh, max, ultra`, and text/image inputs were checked against the live upstream catalog through the main site's account model synchronization endpoint. Sol context is `500000`, Astra context is `272000`, and both maximum context windows are `872000`. Existing client-compatible Sol/Astra tool settings and instruction templates are retained; no newer tool protocol is enabled by this catalog change.
- Immutable catalog commit: `8ec36ecbe06167e941e923f64c142ca541f589da`.
- Channel commit: `c6601858b569b2fe20fa996085e0636fde0bde3c`.
- Catalog URL: `https://raw.githubusercontent.com/kingduoyu/yilai-codex-switcher/8ec36ecbe06167e941e923f64c142ca541f589da/model-catalog.json`.
- SHA-256: `beadb5ae84306b9fb2416e4d7eb3d6e43d445b269de81e6670e36d419e8318c9`.

## Validation

- `Tools/Publish-ModelCatalog.ps1` validated eight models; `-Publish -WhatIf` passed. The publisher pushed the immutable catalog and validated its hash, then a TLS handshake failure interrupted the channel push. The local channel commit and remote main were inspected before pushing that same existing channel commit; the publisher was not rerun and the revision was not incremented again.
- The public raw channel returned revision 3 with the exact immutable URL and SHA-256. Its catalog download contained eight models, including both new IDs, and matched the declared digest.
- Existing `dist/update-protocol.exe model-catalog.json`: passed release/channel validation, additive catalog checks, and preservation of future model selections.
- Existing-client `Tests/model-update.py`: passed isolated model-only installation, unchanged auth/config/history, stale and duplicate catalog rejection, and preservation through later API configuration.
- A temporary model-only probe compiled against the unchanged Windows downloader on main fetched the live channel and catalog, reporting revision 3, eight models, and the exact SHA-256 above. An older combined software/model probe failed its initial software Release request before reaching the catalog; the model-only probe separately verified the requested path.
- No paid inference or live user profile modification was performed. Earlier uncommitted proxy changes remain isolated on `fix/model-update-proxy` and are not part of this publication.

Users can close Codex and CC-Switch, click Update Models in the existing configurator, and reopen Codex. A software upgrade is unnecessary. Updating the catalog does not override server account or group model policies.
