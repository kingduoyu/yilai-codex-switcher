#!/bin/bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
PUBLISH="${PUBLISH_DIR:-$ROOT/../../publish/mac-universal}"
APP="$PUBLISH/易来 Codex 切换器.app"
ARM_BUILD="$ROOT/.build-arm64"
X64_BUILD="$ROOT/.build-x86_64"

rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources" "$PUBLISH"

swift build --package-path "$ROOT" -c release --arch arm64 --scratch-path "$ARM_BUILD"
swift build --package-path "$ROOT" -c release --arch x86_64 --scratch-path "$X64_BUILD"
lipo -create \
  "$ARM_BUILD/arm64-apple-macosx/release/YilaiCodexSwitcherMac" \
  "$X64_BUILD/x86_64-apple-macosx/release/YilaiCodexSwitcherMac" \
  -output "$APP/Contents/MacOS/YilaiCodexSwitcherMac"

cp "$ROOT/Info.plist" "$APP/Contents/Info.plist"
cp "$ROOT/Resources/liquid-glass-background.png" "$APP/Contents/Resources/"
cp "$ROOT/Resources/yilai-switcher-logo.png" "$APP/Contents/Resources/"

ICONSET="$ROOT/.AppIcon.iconset"
rm -rf "$ICONSET"
mkdir -p "$ICONSET"
for spec in "16 16x16" "32 16x16@2x" "32 32x32" "64 32x32@2x" "128 128x128" "256 128x128@2x" "256 256x256" "512 256x256@2x" "512 512x512" "1024 512x512@2x"; do
  set -- $spec
  sips -z "$1" "$1" "$ROOT/Resources/yilai-switcher-logo.png" --out "$ICONSET/icon_$2.png" >/dev/null
done
iconutil -c icns "$ICONSET" -o "$APP/Contents/Resources/AppIcon.icns"
rm -rf "$ICONSET"

chmod +x "$APP/Contents/MacOS/YilaiCodexSwitcherMac"
codesign --force --deep --sign - "$APP"
"$APP/Contents/MacOS/YilaiCodexSwitcherMac" --self-test

DMG_STAGE="$ROOT/.dmg-stage"
DMG="$PUBLISH/YilaiCodexSwitcher-macOS-universal.dmg"
rm -rf "$DMG_STAGE"
mkdir -p "$DMG_STAGE/.background"
cp -R "$APP" "$DMG_STAGE/"
ln -s /Applications "$DMG_STAGE/Applications"
sips -z 440 704 "$ROOT/Resources/liquid-glass-background.png" \
  --out "$DMG_STAGE/.background/background.png" >/dev/null
hdiutil create -volname "易来 Codex 切换器" -srcfolder "$DMG_STAGE" \
  -ov -format UDZO "$DMG"
hdiutil verify "$DMG"
rm -rf "$DMG_STAGE"

rm -f "$PUBLISH/YilaiCodexSwitcher-macOS-universal.zip"
ditto -c -k --sequesterRsrc --keepParent "$APP" "$PUBLISH/YilaiCodexSwitcher-macOS-universal.zip"
shasum -a 256 "$PUBLISH/YilaiCodexSwitcher-macOS-universal.zip"
shasum -a 256 "$DMG"
