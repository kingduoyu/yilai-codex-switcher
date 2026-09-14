# v3.3.16 Release Manifest

Status: candidate; platform verification and publication pending.

Scope: remove historical ownership migration and all related checks from switching. Configuration and effective-config verification remain unchanged; history files are untouched.

Version metadata: Windows 3.3.16.0; macOS 3.3.16/build28.

Validation correction: earlier candidate self-tests failed because migration-warning assertions remained; previous claims of passing tests were incorrect. History processing and its build dependencies have now been removed, preservation tests updated, and real Codex CLI 0.153.4 runtime integration passed. Remaining local checks and fresh platform CI are pending.
