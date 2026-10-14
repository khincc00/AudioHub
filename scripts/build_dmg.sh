#!/bin/sh
# Membangun AudioHub-0.1.dmg: satu paket instalasi untuk aplikasi dan driver, dibungkus DMG.
# Aplikasi masuk ke /Applications, driver ke /Library/Audio/Plug-Ins/HAL.
# Ditandatangani Developer ID. Notarisasi dilakukan terpisah (lihat docs/release/checklist.md).
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build/release"
PRODUCTS="$BUILD/products"
STAGE="$BUILD/stage"
DMG_SRC="$BUILD/dmg"
PKG_NAME="Install AudioHub.pkg"
DMG="$ROOT/build/AudioHub-0.1.dmg"
APP_ID="${APP_ID:-Developer ID Application: taufiq sholikhin (MYQ2H766UX)}"
INSTALLER_ID="${INSTALLER_ID:-Developer ID Installer: taufiq sholikhin (MYQ2H766UX)}"

rm -rf "$BUILD" "$DMG"
mkdir -p "$BUILD"

xcodebuild -project "$ROOT/AudioHub.xcodeproj" -target AudioHub -target AudioHubDriver \
    -configuration Release CONFIGURATION_BUILD_DIR="$PRODUCTS" build >/dev/null

# Susun root paket sesuai lokasi pemasangan
mkdir -p "$STAGE/Applications" "$STAGE/Library/Audio/Plug-Ins/HAL"
ditto --norsrc --noextattr --noacl "$PRODUCTS/AudioHub.app" "$STAGE/Applications/AudioHub.app"
ditto --norsrc --noextattr --noacl "$PRODUCTS/AudioHub.driver" "$STAGE/Library/Audio/Plug-Ins/HAL/AudioHub.driver"
# pkgbuild mengubah xattr (mis. com.apple.provenance) menjadi file ._* di payload, jadi hapus dulu
xattr -cr "$STAGE"
find "$STAGE" -name "._*" -delete

# Kunci lokasi pemasangan: tanpa ini installer bisa menaruh app di lokasi bundel lain yang ditemukan di Mac
pkgbuild --analyze --root "$STAGE" "$BUILD/components.plist" >/dev/null
python3 - "$BUILD/components.plist" <<'PY'
import plistlib, sys
path = sys.argv[1]
with open(path, "rb") as f:
    comps = plistlib.load(f)
for c in comps:
    c["BundleIsRelocatable"] = False
with open(path, "wb") as f:
    plistlib.dump(comps, f)
PY

PKG="$BUILD/AudioHub-0.1.pkg"
pkgbuild --root "$STAGE" \
    --component-plist "$BUILD/components.plist" \
    --identifier com.audiohub.installer \
    --version 0.1 \
    --install-location / \
    --ownership recommended \
    --scripts "$ROOT/virtual-device/AudioHubDriver/pkg/scripts" \
    --sign "$INSTALLER_ID" \
    "$PKG"

# DMG hanya berisi installer
mkdir -p "$DMG_SRC"
cp "$PKG" "$DMG_SRC/$PKG_NAME"
hdiutil create -volname "AudioHub 0.1" -srcfolder "$DMG_SRC" -ov -format UDZO "$DMG" >/dev/null
codesign --force --sign "$APP_ID" --timestamp "$DMG"

echo "DMG: $DMG"
