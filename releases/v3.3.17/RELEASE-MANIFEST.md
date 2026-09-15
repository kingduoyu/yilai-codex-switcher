# v3.3.17 Release Manifest

Status: candidate; platform CI pending.

Scope: move auth.json to the system Recycle Bin / Trash during API configuration instead of permanently deleting it. Existing disabled login backups are not consulted or overwritten. Add an explicit-only legacy yilai-to-custom history repair button. Repair skips duplicate IDs, malformed files and conflicting paths; per-row index failures do not revert other completed work. Configuration, authentication and existing backups remain untouched by repair.
Version metadata: Windows 3.3.17.0; macOS 3.3.17/build29.
Local Windows build, isolated self-test (including manual repair and retry cases), GUI regression, source integration and real Codex CLI 0.153.4 runtime integration passed. Fresh platform CI is required for the added manual button.
No release download validation is performed, per user request.
