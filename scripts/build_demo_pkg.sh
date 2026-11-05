#!/bin/sh
# Membangun AudioHub-Demo-1.0.0.pkg: installer edisi demo berisi AudioHub Demo.app dan driver AVHub v2 (VC 1/2).
# Ditandatangani Developer ID. Notarisasi dijalankan hanya jika NOTARY_PROFILE diisi, yaitu nama profil notarytool di Keychain.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
APP_SRC="$ROOT/apps/macos/AudioHub"
DRIVER_SRC="$ROOT/virtual-device/AVHubDriver"
OUT_DIR="${OUT_DIR:-$ROOT/build}"
BUILD="$OUT_DIR/demo-pkg"
PKG="$OUT_DIR/AudioHub-Demo-1.0.0.pkg"
APP_ID="${APP_ID:-Developer ID Application: taufiq sholikhin (MYQ2H766UX)}"
INSTALLER_ID="${INSTALLER_ID:-Developer ID Installer: taufiq sholikhin (MYQ2H766UX)}"
APP="$BUILD/products/AudioHub Demo.app"

rm -rf "$BUILD" "$PKG"
mkdir -p "$BUILD/products" "$OUT_DIR"

# Tes driver dulu, lalu bundel driver dengan Developer ID
sh "$DRIVER_SRC/tests/run_tests.sh" >/dev/null
SIGN_IDENTITY="$APP_ID" sh "$DRIVER_SRC/build.sh" >/dev/null

# Aplikasi demo dibangun ulang, lalu ditandatangani ulang dengan Developer ID dan Hardened Runtime
DEMO=1 sh "$APP_SRC/build.sh" >/dev/null
ditto --norsrc --noextattr --noacl "$APP_SRC/build/AudioHub Demo.app" "$APP"
codesign --force --options runtime --timestamp --entitlements "$APP_SRC/AudioHub.entitlements" --sign "$APP_ID" "$APP"
codesign --verify --deep --strict "$APP"

# Kunci lokasi pemasangan, sama seperti build_dmg.sh
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

# Paket aplikasi demo
APP_STAGE="$BUILD/app-stage"
mkdir -p "$APP_STAGE/Applications"
ditto --norsrc --noextattr --noacl "$APP" "$APP_STAGE/Applications/AudioHub Demo.app"
xattr -cr "$APP_STAGE"
find "$APP_STAGE" -name "._*" -delete
lock_location "$APP_STAGE" "$BUILD/app-components.plist"
pkgbuild --root "$APP_STAGE" \
    --component-plist "$BUILD/app-components.plist" \
    --identifier com.audiohub.demo.installer \
    --version 1.0.0 \
    --install-location / \
    --ownership recommended \
    --sign "$INSTALLER_ID" \
    "$BUILD/AudioHub-Demo-app.pkg"

# Paket driver, sama seperti build_dmg.sh. Postinstall memuat ulang coreaudiod agar perangkat langsung muncul.
DRIVER_STAGE="$BUILD/driver-stage"
DRIVER_SCRIPTS="$BUILD/driver-scripts"
mkdir -p "$DRIVER_STAGE/Library/Audio/Plug-Ins/HAL" "$DRIVER_SCRIPTS"
ditto --norsrc --noextattr --noacl "$DRIVER_SRC/build/AVHubDriver.driver" "$DRIVER_STAGE/Library/Audio/Plug-Ins/HAL/AVHubDriver.driver"
xattr -cr "$DRIVER_STAGE"
find "$DRIVER_STAGE" -name "._*" -delete
printf '#!/bin/sh\nkillall coreaudiod 2>/dev/null || true\nexit 0\n' > "$DRIVER_SCRIPTS/postinstall"
chmod +x "$DRIVER_SCRIPTS/postinstall"
lock_location "$DRIVER_STAGE" "$BUILD/driver-components.plist"
pkgbuild --root "$DRIVER_STAGE" \
    --component-plist "$BUILD/driver-components.plist" \
    --identifier com.avhub.driver.installer \
    --version 1.0.0 \
    --install-location / \
    --ownership recommended \
    --scripts "$DRIVER_SCRIPTS" \
    --sign "$INSTALLER_ID" \
    "$BUILD/AVHubDriver-1.0.0.pkg"

# Gabungkan keduanya menjadi satu installer
productbuild --package "$BUILD/AudioHub-Demo-app.pkg" --package "$BUILD/AVHubDriver-1.0.0.pkg" \
    --sign "$INSTALLER_ID" \
    "$PKG"

# Notarisasi, hanya jika profilnya disediakan
if [ -n "${NOTARY_PROFILE:-}" ]; then
    xcrun notarytool submit "$PKG" --keychain-profile "$NOTARY_PROFILE" --wait
    xcrun stapler staple "$PKG"
fi

echo "Paket demo: $PKG"
