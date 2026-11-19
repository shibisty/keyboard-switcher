#!/usr/bin/env bash
# Release build for Linux: .deb, .rpm and .tar.gz via CPack.
#
#   ./scripts/build-linux.sh 1.1.0.0
#
# Build dependencies (Ubuntu 24.04+, Debian 13):
#   sudo apt install build-essential cmake ninja-build qt6-base-dev qt6-tools-dev libx11-dev libxtst-dev libxkbcommon-dev
# Ubuntu 22.04 / Debian 12 (Qt 6 too old there, Qt 5.15 is used automatically):
#   sudo apt install build-essential cmake ninja-build qtbase5-dev qttools5-dev libx11-dev libxtst-dev libxkbcommon-dev
# Fedora:
#   sudo dnf install gcc-c++ cmake ninja-build qt6-qtbase-devel qt6-qttools-devel libX11-devel rpm-build
set -euo pipefail

VERSION="${1:-1.1.0.0}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build/release-linux"
DIST="$ROOT/dist/linux"

# GNOME 42-44 (Ubuntu 22.04, Debian 12) needs the legacy-format GNOME Shell extension.
GNOME_LEGACY=OFF
if command -v gnome-shell >/dev/null; then
  GNOME_MAJOR=$(gnome-shell --version | grep -oE '[0-9]+' | head -1)
  [[ -n "$GNOME_MAJOR" && "$GNOME_MAJOR" -lt 45 ]] && GNOME_LEGACY=ON
fi

rm -rf "$BUILD" && mkdir -p "$DIST"
cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
  -DKC_BUILD_VERSION="$VERSION" -DKC_GNOME_LEGACY_EXTENSION="$GNOME_LEGACY"
cmake --build "$BUILD"

GENERATORS="TGZ"
command -v dpkg-deb >/dev/null && GENERATORS="$GENERATORS;DEB"
command -v rpmbuild >/dev/null && GENERATORS="$GENERATORS;RPM"
(cd "$BUILD" && cpack -G "$GENERATORS" -B "$DIST")
rm -rf "$DIST/_CPack_Packages"
ls -1 "$DIST"
