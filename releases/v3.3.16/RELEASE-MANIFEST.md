# v3.3.16 Release Manifest

Status: verified candidate; publication pending.

Scope: remove historical ownership migration and all related checks from switching. Configuration and effective-config verification remain unchanged; history files are untouched.

Version metadata: Windows 3.3.16.0; macOS 3.3.16/build28.

Validation correction: earlier candidate self-tests failed because migration-warning assertions remained; previous claims of passing tests were incorrect. History processing and its build dependencies have now been removed and preservation tests updated.

Verified source: 8edfdaa62f5ba7fc079b7bddac80f01598071c31. PR #5 merged as bd9a5ad; merged tree matches the tested candidate exactly.

Passed locally: Windows self-test (explicit exit code 0), Windows UI regression, source integration, real Codex CLI 0.153.4 runtime integration, and git diff --check.

Passed CI: Windows run 34881941175; macOS run 34881940886. macOS UI screenshot reviewed.

Release assets:
- YilaiCodexSwitcher.exe: 3941376 bytes; SHA-256 8CA829C53834D9C76F85BBC5815D2C938FB79F3341E70DAE1B6299478525AC76.
- YilaiCodexSwitcher-macOS-universal.dmg: 4823244 bytes; SHA-256 81F9203F4F5B1C7F25F51BE3B8238A8443535C4605668AFF9B1BBAE43A7A64B0.
