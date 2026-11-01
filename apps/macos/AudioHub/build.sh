#!/bin/sh
# Membangun AudioHub.app (mode pengembangan). Tidak memasang apa pun.
# DEMO=1 membangun edisi demo "AudioHub Demo.app" dengan batasan (lihat enum Edition di Model.swift).
set -e
ROOT="$(cd "$(dirname "$0")" && pwd)"
REPO="$ROOT/../../.."
OUT="$ROOT/build"
rm -rf "$OUT"
mkdir -p "$OUT/obj"
CXXFLAGS="-std=c++17 -O2 -Wall -Wextra -Wno-deprecated-declarations"
DEMO_FLAGS=""
APP_NAME="AudioHub"
BUNDLE_ID="com.audiohub.app"
if [ "${DEMO:-0}" = "1" ]; then
    DEMO_FLAGS="-D DEMO"
    APP_NAME="AudioHub Demo"
    BUNDLE_ID="com.audiohub.app.demo"
fi
clang++ $CXXFLAGS -c "$REPO/engine/graph/graph.cpp" -o "$OUT/obj/graph.o"
clang++ $CXXFLAGS -c "$REPO/engine/api/audiohub_engine.cpp" -o "$OUT/obj/engine.o"
clang++ $CXXFLAGS -c "$REPO/backends/coreaudio/io/audiohub_io.cpp" -o "$OUT/obj/io.o"
clang++ $CXXFLAGS -c "$REPO/engine/dsp/dsp.cpp" -o "$OUT/obj/dsp.o"
clang++ $CXXFLAGS -c "$REPO/backends/coreaudio/io/au_insert.cpp" -o "$OUT/obj/au.o"
swiftc -parse-as-library -O $DEMO_FLAGS \
    -import-objc-header "$ROOT/Sources/AudioHub-Bridging.h" \
    -I "$REPO/backends/coreaudio/io" \
    "$ROOT/Sources/"*.swift \
    "$OUT/obj/graph.o" "$OUT/obj/engine.o" "$OUT/obj/io.o" "$OUT/obj/dsp.o" "$OUT/obj/au.o" \
    -framework SwiftUI -framework AppKit -framework CoreAudio -framework CoreFoundation -framework AudioToolbox \
    -Xlinker -lc++ \
    -o "$OUT/AudioHub"
echo "Dibangun: $OUT/AudioHub"
# Bundel .app dengan Info.plist (berisi izin mikrofon) dan tanda tangan ad-hoc
APP="$OUT/$APP_NAME.app"
mkdir -p "$APP/Contents/MacOS"
cp "$OUT/AudioHub" "$APP/Contents/MacOS/AudioHub"
cp "$ROOT/Info.plist" "$APP/Contents/Info.plist"
if [ "$APP_NAME" != "AudioHub" ]; then
    /usr/libexec/PlistBuddy -c "Set :CFBundleName $APP_NAME" -c "Set :CFBundleIdentifier $BUNDLE_ID" "$APP/Contents/Info.plist"
fi
# Ikon sama dengan build Xcode: PNG dari asset catalog dijadikan AppIcon.icns
ICONSET="$OUT/AppIcon.iconset"
mkdir -p "$ICONSET" "$APP/Contents/Resources"
cp "$ROOT/Assets.xcassets/AppIcon.appiconset/"icon_*.png "$ICONSET/"
iconutil -c icns "$ICONSET" -o "$APP/Contents/Resources/AppIcon.icns"
/usr/libexec/PlistBuddy -c "Add :CFBundleIconFile string AppIcon" "$APP/Contents/Info.plist"
codesign --force --sign - "$APP" 2>/dev/null || echo "Peringatan: tanda tangan ad-hoc gagal"
echo "Bundel: $APP"
