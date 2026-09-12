# v3.3.11 Release Manifest

Status: release candidate; publication is not authorized or complete.

Scope: preserve the Codex desktop's saved full-access mode when the TOML has no explicit permission policy; never replace explicit approval/sandbox settings. Speed repeat history checks by reading only the metadata prefix of non-yilai sessions and querying only legacy yilai database rows. Files selected for migration still receive complete JSONL validation, backup, rollback and recovery handling.

Version metadata: Windows 3.3.11.0; macOS 3.3.11/build23.

Windows candidate verification: native executable self-test, application/history driver self-tests, Windows UI failure regression and unified-history routing regression passed. A synthetic repeat check over 64 already-custom sparse rollouts with 16.0 GiB logical size completed in 0.220 and 0.142 seconds; this measures the prefix-only path, not physical sequential disk throughput. The ordinary migration regression completed in 0.173 seconds.

Pending release gates: final versioned Windows rebuild and screenshots; macOS universal CI build, native self-test, DMG verification and screenshot; exact hashes; draft upload and downloaded-asset verification. Publish versioned EXE/DMG/ZIP plus SHA256SUMS and stable generic-name aliases to prevent latest/download 404s.

Limits: no production official OAuth send or paid image generation. No automatic CCS database/current synchronization. API configuration still deletes auth.json; official switching preserves the current file but does not restore deleted login. Mac remains ad-hoc signed and not notarized.
