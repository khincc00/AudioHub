#!/bin/sh
# Membangun bundle driver AVHub v2 (universal: Intel + Apple Silicon).
# Hasilnya di build/AVHubDriver.driver. Tidak perlu sudo.
# Pemakaian: sh build.sh
# Penandatanganan: default ad-hoc. Untuk Developer ID, set SIGN_IDENTITY, contoh:
#   SIGN_IDENTITY="Developer ID Application: Nama (TEAMID)" sh build.sh
set -e
ROOT="$(cd "$(dirname "$0")" && pwd)"
BUNDLE="$ROOT/build/AVHubDriver.driver"
BIN="$BUNDLE/Contents/MacOS/AVHubDriver"

rm -rf "$BUNDLE"
mkdir -p "$BUNDLE/Contents/MacOS" "$BUNDLE/Contents/Resources"
cp "$ROOT/src/Info.plist" "$BUNDLE/Contents/Info.plist"
# Lisensi lengkap ikut ke dalam bundel, jadi tetap ada setelah driver dipasang
cp "$ROOT/LICENSE" "$BUNDLE/Contents/Resources/LICENSE"

clang -bundle -arch x86_64 -arch arm64 -mmacosx-version-min=11.0 -O2 \
  -Wall -Wextra -Wno-unused-parameter -fvisibility=default \
  -framework CoreAudio -framework CoreFoundation \
  -o "$BIN" "$ROOT/src/AVHubDriver.c"

SIGN="${SIGN_IDENTITY:--}"
if [ "$SIGN" = "-" ]; then
  codesign --force --sign - "$BUNDLE"
else
  codesign --force --options runtime --timestamp --sign "$SIGN" "$BUNDLE"
fi
codesign --verify --deep --strict "$BUNDLE"

echo "Bundle dibuat: $BUNDLE"
