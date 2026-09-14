# v3.3.14 Release Manifest

Status: candidate; platform CI and publication pending.

Scope: initialize disposable probe databases against an empty history before reading real configuration. Reuse one runtime session for all configuration contexts and share the existing 20-second deadline across bootstrap and verification. Isolate the startup working directory and identify the timeout phase. API rollback semantics are unchanged.

Version metadata: Windows 3.3.14.0; macOS 3.3.14/build26.

Local validation before version update: Windows build, real Codex CLI 0.153.4 runtime integration, source-layer integration and GUI regression passed. On the same local configuration, one measured probe took 17.32 seconds before the bootstrap change and 0.47 seconds after it. These measurements are not a customer-wide performance guarantee.

Limits: no paid image generation, production official OAuth send or physical Intel execution was tested. Temporary bootstrap storage is discarded; user history databases are not used for probing. macOS is ad-hoc signed and not notarized.
