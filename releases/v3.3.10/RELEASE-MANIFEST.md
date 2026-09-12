# v3.3.10 Release Manifest

Status: release candidate; publication is not yet complete.

Scope: reject NUL input before official rewrites; revalidate config snapshots before writes and rollback; preserve inactive root/profile connections referenced through custom or legacy yilai. Official custom/yilai aliases remain free of third-party endpoint, bearer-token and header settings. Existing API authentication policy, history migration scope, UI actions and model catalog remain unchanged.

Version metadata: Windows 3.3.10.0; macOS 3.3.10/build22.

Required release gates: versioned Windows build and isolated regressions; macOS universal CI build, native self-tests and DMG verification; both UI screenshots; asset hashes; draft asset verification before publication. Verification results and exact source commits will be recorded here as completed.

Limits: no production official OAuth send or paid image generation; no automatic official-login restoration or CCS database synchronization. macOS remains ad-hoc signed and not notarized. User live configuration and history must not be changed by verification.
