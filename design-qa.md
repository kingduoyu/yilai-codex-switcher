# Native UI QA

## Target And Evidence

- Source: `C:/Users/ADMINI~1/AppData/Local/Temp/codex-clipboard-9f036a34-6b13-43b6-9034-1f9148334cfa.png`.
- Source pixels: 790 x 702. Client crop: (1, 43, 741, 669), yielding 740 x 626.
- Implementation: `dist/implementation-windows-models.png`, 740 x 626 native client pixels, 1:1 density. No browser or CSS viewport applies.
- State: model list open, masked synthetic key, official mode, blocked configuration error. Version intentionally advances from 3.4.0 to 3.4.1.
- Full comparison: `dist/comparison-final-full.png` contains both images in one input.
- Focused comparisons: `dist/comparison-final-header.png` and `dist/comparison-final-models.png` contain corresponding regions side by side.
- Additional states: `dist/implementation-windows-default.png`, `dist/implementation-windows-update.png`, and `dist/implementation-windows-warning.png`.
- Final macOS capture: `dist/mac-v3.4.1-36730834718/implementation-macos.png`, 760 x 700 native pixels at 1:1 density. State: unconfigured, with a synthetic available-update version of 3.4.2.
- macOS comparisons: `dist/comparison-macos-final.png` and `dist/comparison-macos-actions.png` show the first iteration on the left and final implementation on the right, at matching dimensions and state. This supplementary baseline is not the Windows source screenshot.

## Findings And Iterations

1. Initial comparison: `dist/comparison-iteration-1-full.png`. [P1] Native icon rendering corrupted the brand colors. Fix: decode the existing embedded PNG with WIC and draw premultiplied pixels instead of the icon-mask path. Result was blocked.
2. Revised capture: `dist/comparison-final-full.png`. The raster brand mark is sharp and blue, duplicate slugs are gone, the version fits one line, and the legacy repair button is absent. No remaining Windows P0/P1/P2 findings.
3. macOS first native capture: `dist/iteration-1-macos.png`, 760 x 700. [P2] Official switching remained a tiny prominent button below a full-width API action. Fix: align both actions in an equal-width 48 px row and give official switching a neutral border. This iteration was blocked.
4. Final macOS full and focused comparisons were inspected together. Both action labels fit equal-width 48 px buttons; the version and yellow indicator remain on one line. The update indicator contains 24 yellow pixels. No remaining P0/P1/P2 findings on either platform.

## Required Surfaces

- Fonts: native Microsoft YaHei UI, 24 px semibold heading, 15 px body, 13 px secondary text, 16 px action text. System sans-serif fallback is requested; no negative letter spacing. Header and button labels do not wrap. GDI font-family probes were checked locally.
- macOS uses the native system font, a stable 760 x 700 capture, and a 720 x 700 minimum window size. Its action buttons use an 8 px radius.
- Spacing: fixed 740 x 626 client; retained 28 px page inset and existing form alignment. Form radius is 8 px, action buttons 6 px, and model rows 28 px. Shorter form and earlier status block intentionally remove obsolete explanatory space.
- Colors: retained neutral canvas (245,247,251), white form, blue primary action (37,99,235), muted secondary text, and red/amber semantic states. The available-update dot is yellow (234,179,8).
- Assets: reused `Windows/Assets/yilai-switcher-logo.png` without alteration. Replacing the old drawn Y with the existing product raster is intentional. No generated or handcrafted replacement logo.
- Copy: one display name per model; compact current version; current-version and check status inside the menu; no repair feature or obsolete history compatibility claim. Safety prerequisites and actionable error messages remain.

## Interaction Verification

- Real native GUI: model names/count, single current-version value, header placement, absence of repair/log controls, two recoverable configuration failures, unchanged fixture config, and no process logs.
- UI tests and screenshot commands skip the real saved-key cache and update receipt. Only isolated Codex homes and synthetic keys are used.
- Core integration, source override, and independent model-update regressions passed after removal of the history module.
- macOS workflow `36730834718` passed the universal build, app self-test, DMG verification, native helper install/relaunch, backup retention, and candidate-move failure rollback/relaunch. The source app and isolated Codex data were unchanged. ZIP CRC, version 3.4.1/build32, and both Mach-O architectures were verified.
- Windows update-helper tests passed normal replacement/relaunch, wrong-hash rejection, and launch-failure rollback. A fresh final build passed native self-test and real GUI tests.
- Residual coverage: Windows capture is at 100% display density; other Windows font installations and display scales were not separately captured. macOS x86_64 was compiled but not separately tested on physical hardware; the app is ad-hoc signed, not notarized. No browser console applies to this native application.

## Checklist

- Completed: Windows full and focused visual comparisons, updater indicator, default/error/warning states, repair removal.
- Completed: macOS full and focused visual comparisons, universal build, native updater/rollback, and artifact integrity verification.
- No unresolved P0/P1/P2 findings. Release evidence is recorded in `releases/v3.4.1/RELEASE-MANIFEST.md`.

final result: passed
