#!/bin/sh
# Membangun AudioHub-0.3.0.dmg: satu paket instalasi untuk aplikasi dan driver virtual AVHub v2, dibungkus DMG.
# Aplikasi masuk ke /Applications, driver masuk ke /Library/Audio/Plug-Ins/HAL. Keduanya dipasang sekaligus.
# Ditandatangani Developer ID. Notarisasi dilakukan terpisah (lihat docs/release/checklist.md).
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build/release"
PRODUCTS="$BUILD/products"
STAGE="$BUILD/stage"
DRIVER_SRC="$ROOT/virtual-device/AVHubDriver"
DRIVER_BUNDLE="$DRIVER_SRC/build/AVHubDriver.driver"
DRIVER_STAGE="$BUILD/driver-stage"
DRIVER_SCRIPTS="$BUILD/driver-scripts"
DMG_SRC="$BUILD/dmg"
PKG_NAME="Install AudioHub.pkg"
DMG="$ROOT/build/AudioHub-0.3.0.dmg"
APP_ID="${APP_ID:-Developer ID Application: taufiq sholikhin (MYQ2H766UX)}"
INSTALLER_ID="${INSTALLER_ID:-Developer ID Installer: taufiq sholikhin (MYQ2H766UX)}"

rm -rf "$BUILD" "$DMG"
mkdir -p "$BUILD"

# Tes driver dulu. Tes tidak memasang apa pun ke sistem.
sh "$DRIVER_SRC/tests/run_tests.sh" >/dev/null

# Driver dibangun ulang dengan Developer ID, bukan tanda tangan ad-hoc dari tes
SIGN_IDENTITY="$APP_ID" sh "$DRIVER_SRC/build.sh" >/dev/null

xcodebuild -project "$ROOT/AudioHub.xcodeproj" -target AudioHub \
    -configuration Release CONFIGURATION_BUILD_DIR="$PRODUCTS" build >/dev/null

# Kunci lokasi pemasangan: tanpa ini installer bisa menaruh bundel di lokasi lain yang ditemukan di Mac
lock_location() {
    pkgbuild --analyze --root "$1" "$2" >/dev/null
    python3 - "$2" <<'PY'
import plistlib, sys
path = sys.argv[1]
with open(path, "rb") as f:
    comps = plistlib.load(f)
for c in comps:
    c["BundleIsRelocatable"] = False
with open(path, "wb") as f:
    plistlib.dump(comps, f)
PY
}

# Paket aplikasi
mkdir -p "$STAGE/Applications"
ditto --norsrc --noextattr --noacl "$PRODUCTS/AudioHub.app" "$STAGE/Applications/AudioHub.app"
# pkgbuild mengubah xattr (mis. com.apple.provenance) menjadi file ._* di payload, jadi hapus dulu
xattr -cr "$STAGE"
find "$STAGE" -name "._*" -delete
lock_location "$STAGE" "$BUILD/app-components.plist"

APP_PKG="$BUILD/AudioHub-app.pkg"
pkgbuild --root "$STAGE" \
    --component-plist "$BUILD/app-components.plist" \
    --identifier com.audiohub.installer \
    --version 0.3.0 \
    --install-location / \
    --ownership recommended \
    --sign "$INSTALLER_ID" \
    "$APP_PKG"

# Paket driver. Postinstall memuat ulang coreaudiod agar perangkat avhub_virtual1 dan avhub_virtual2 langsung muncul.
mkdir -p "$DRIVER_STAGE/Library/Audio/Plug-Ins/HAL" "$DRIVER_SCRIPTS"
ditto --norsrc --noextattr --noacl "$DRIVER_BUNDLE" "$DRIVER_STAGE/Library/Audio/Plug-Ins/HAL/AVHubDriver.driver"
xattr -cr "$DRIVER_STAGE"
find "$DRIVER_STAGE" -name "._*" -delete
cat > "$DRIVER_SCRIPTS/postinstall" <<'SCRIPT'
#!/bin/sh
killall coreaudiod 2>/dev/null || true
exit 0
SCRIPT
chmod +x "$DRIVER_SCRIPTS/postinstall"
lock_location "$DRIVER_STAGE" "$BUILD/driver-components.plist"

DRIVER_PKG="$BUILD/AVHubDriver-2.0.0.pkg"
pkgbuild --root "$DRIVER_STAGE" \
    --component-plist "$BUILD/driver-components.plist" \
    --identifier com.avhub.driver.installer \
    --version 2.0.0 \
    --install-location / \
    --ownership recommended \
    --scripts "$DRIVER_SCRIPTS" \
    --sign "$INSTALLER_ID" \
    "$DRIVER_PKG"

# Gabungkan aplikasi dan driver menjadi satu installer
PKG="$BUILD/AudioHub-0.3.0.pkg"
productbuild --package "$APP_PKG" --package "$DRIVER_PKG" \
    --sign "$INSTALLER_ID" \
    "$PKG"

# DMG hanya berisi installer
mkdir -p "$DMG_SRC"
cp "$PKG" "$DMG_SRC/$PKG_NAME"
hdiutil create -volname "AudioHub 0.3.0" -srcfolder "$DMG_SRC" -ov -format UDZO "$DMG" >/dev/null
codesign --force --sign "$APP_ID" --timestamp "$DMG"

echo "DMG: $DMG"
