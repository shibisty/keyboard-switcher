#!/usr/bin/env bash
# Release build for macOS: universal (Apple Silicon + Intel) .app in a .dmg.
#
#   QT_DIR=~/Qt/6.8.3/macos ./scripts/build-macos.sh 1.1.0.0
#
# Optional signing + notarization (needed so Gatekeeper opens the app without warnings):
#   SIGN_ID="Developer ID Application: Your Name (TEAMID)"
#   NOTARY_PROFILE=<profile created with: xcrun notarytool store-credentials>
set -euo pipefail

VERSION="${1:-1.1.0.0}"
QT_DIR="${QT_DIR:?Set QT_DIR to the Qt macOS kit, e.g. ~/Qt/6.8.3/macos}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build/release-macos"
DIST="$ROOT/dist/macos"
APP="$BUILD/Keyboard Switcher.app"

rm -rf "$BUILD" && mkdir -p "$DIST"
cmake -S "$ROOT" -B "$BUILD" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$QT_DIR" \
  -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
  -DKC_BUILD_VERSION="$VERSION"
cmake --build "$BUILD"

"$QT_DIR/bin/macdeployqt" "$APP" -always-overwrite

if [[ -n "${SIGN_ID:-}" ]]; then
  codesign --force --deep --options runtime --timestamp --sign "$SIGN_ID" "$APP"
fi

DMG="$DIST/KeyboardSwitcher-$VERSION.dmg"
rm -f "$DMG"
hdiutil create -volname "Keyboard Switcher" -srcfolder "$APP" -ov -format UDZO "$DMG"

if [[ -n "${SIGN_ID:-}" ]]; then
  codesign --force --sign "$SIGN_ID" "$DMG"
  if [[ -n "${NOTARY_PROFILE:-}" ]]; then
    xcrun notarytool submit "$DMG" --keychain-profile "$NOTARY_PROFILE" --wait
    xcrun stapler staple "$DMG"
  fi
fi
echo "Done: $DMG"
