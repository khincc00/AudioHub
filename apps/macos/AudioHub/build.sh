#!/bin/sh
# Membangun AudioHub.app (mode pengembangan). Tidak memasang apa pun.
set -e
ROOT="$(cd "$(dirname "$0")" && pwd)"
REPO="$ROOT/../../.."
OUT="$ROOT/build"
rm -rf "$OUT"
mkdir -p "$OUT/obj"
CXXFLAGS="-std=c++17 -O2 -Wall -Wextra -Wno-deprecated-declarations"
clang++ $CXXFLAGS -c "$REPO/engine/graph/graph.cpp" -o "$OUT/obj/graph.o"
clang++ $CXXFLAGS -c "$REPO/engine/api/audiohub_engine.cpp" -o "$OUT/obj/engine.o"
clang++ $CXXFLAGS -c "$REPO/backends/coreaudio/io/audiohub_io.cpp" -o "$OUT/obj/io.o"
clang++ $CXXFLAGS -c "$REPO/engine/dsp/dsp.cpp" -o "$OUT/obj/dsp.o"
clang++ $CXXFLAGS -c "$REPO/backends/coreaudio/io/au_insert.cpp" -o "$OUT/obj/au.o"
swiftc -parse-as-library -O \
    -import-objc-header "$ROOT/Sources/AudioHub-Bridging.h" \
    -I "$REPO/backends/coreaudio/io" \
    "$ROOT/Sources/"*.swift \
    "$OUT/obj/graph.o" "$OUT/obj/engine.o" "$OUT/obj/io.o" "$OUT/obj/dsp.o" "$OUT/obj/au.o" \
    -framework SwiftUI -framework AppKit -framework CoreAudio -framework CoreFoundation -framework AudioToolbox \
    -Xlinker -lc++ \
    -o "$OUT/AudioHub"
echo "Dibangun: $OUT/AudioHub"
# Bundel .app dengan Info.plist (berisi izin mikrofon) dan tanda tangan ad-hoc
APP="$OUT/AudioHub.app"
mkdir -p "$APP/Contents/MacOS"
cp "$OUT/AudioHub" "$APP/Contents/MacOS/AudioHub"
cp "$ROOT/Info.plist" "$APP/Contents/Info.plist"
codesign --force --sign - "$APP" 2>/dev/null || echo "Peringatan: tanda tangan ad-hoc gagal"
echo "Bundel: $APP"
