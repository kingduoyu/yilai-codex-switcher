# v3.3.17 Release Manifest

Status: verified candidate; publication pending.

Scope: move auth.json to the system Recycle Bin / Trash during API configuration instead of permanently deleting it. Existing disabled login backups are not consulted or overwritten. Add an explicit-only legacy yilai-to-custom history repair button. Repair skips duplicate IDs, malformed files and conflicting paths; per-row index failures do not revert other completed work. Configuration, authentication and existing backups remain untouched by repair.
Version metadata: Windows 3.3.17.0; macOS 3.3.17/build29.
Local Windows build, isolated self-test (including manual repair and retry cases), GUI regression, source integration and real Codex CLI 0.153.4 runtime integration passed.

Verified source: c9090822ab540ecaef0e33a939da2bcbf9029eda. PR #6 merged as d9c299d; merged tree matches the tested candidate.
Passed final CI: Windows 34925465181; macOS 34925464836. Both native self-tests and UI screenshots verified.
Release assets:
- YilaiCodexSwitcher.exe: 5225472 bytes; SHA-256 90FDB53DF094A4CD21D424E7D221CEA4626386D7864268B4B0F7BA03F9DC6999.
- YilaiCodexSwitcher-macOS-universal.dmg: 6093695 bytes; SHA-256 9E898AD2D6BA24202297118716E1DFEE0A7CF6B95B09799C6CE8E7F516A2F6F4.
No release download validation is performed, per user request.
