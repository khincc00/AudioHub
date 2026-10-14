#!/bin/sh
# Membangun paket instalasi driver yang ditandatangani Developer ID Installer.
# Driver dibangun dari proyek Xcode (Release, Developer ID). Tidak memasang apa pun.
# Notarisasi dilakukan terpisah (lihat docs/release/checklist.md).
set -e
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
DRIVER_DIR="$ROOT/virtual-device/AudioHubDriver"
BUILD="$DRIVER_DIR/build"
PRODUCTS="$BUILD/xcode"
STAGE="$BUILD/pkgroot"
OUT="$BUILD/AudioHub-Virtual-0.1.pkg"
INSTALLER_ID="${INSTALLER_ID:-Developer ID Installer: taufiq sholikhin (MYQ2H766UX)}"

rm -rf "$OUT" "$PRODUCTS" "$STAGE"
xcodebuild -project "$ROOT/AudioHub.xcodeproj" -target AudioHubDriver -configuration Release \
    CONFIGURATION_BUILD_DIR="$PRODUCTS" build >/dev/null

DRIVER="$PRODUCTS/AudioHub.driver"
mkdir -p "$STAGE/Library/Audio/Plug-Ins/HAL"
ditto --norsrc --noextattr --noacl "$DRIVER" "$STAGE/Library/Audio/Plug-Ins/HAL/AudioHub.driver"
find "$STAGE" -name "._*" -delete

pkgbuild --root "$STAGE" \
    --identifier com.audiohub.virtual.driver \
    --version 0.1 \
    --install-location / \
    --ownership recommended \
    --scripts "$DRIVER_DIR/pkg/scripts" \
    --sign "$INSTALLER_ID" \
    "$OUT"
echo "Paket: $OUT"
