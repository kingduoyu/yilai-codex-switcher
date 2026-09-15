# Dependencies

- SQLite amalgamation: sqlite3.c / sqlite3.h, existing locally vendored distribution; SQLite public domain notice and exact version are in the files.
- nlohmann/json single header: ../Shared/vendor/json.hpp, MIT license included in the header.
- toml++ remains in ConfigRewrite/vendor/toml.hpp, MIT license included.

SQLite is unchanged from the previously tracked dependency at commit 48aff85. HistoryRepair reuses the prior metadata parsing and atomic file helpers, with a new explicit-only, per-item repair flow. It does not restore the old automatic history transactions or backup system. No CCS program or settings are modified.
