# Dependencies

- SQLite amalgamation: sqlite3.c / sqlite3.h, existing locally vendored distribution; SQLite public domain notice and exact version are in the files.
- nlohmann/json single header: ../Shared/vendor/json.hpp, MIT license included in the header.
- toml++ remains in ConfigRewrite/vendor/toml.hpp, MIT license included.

The new HistorySync implementation is independently written. CCS v3.20.2 source/guides were reviewed for the custom bucket, official authentication shape, JSONL metadata, state_5.sqlite, and backup/restore semantics. No CCS program or settings are modified.
