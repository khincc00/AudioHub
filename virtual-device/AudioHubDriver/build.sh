#!/bin/sh
# Membangun AudioHub.driver ke build/. Tidak memasang apa pun.
set -e
ROOT="$(cd "$(dirname "$0")" && pwd)"
OUT="$ROOT/build/AudioHub.driver"
rm -rf "$OUT"
mkdir -p "$OUT/Contents/MacOS"
clang -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter -bundle \
    -framework CoreAudio -framework CoreFoundation \
    "$ROOT/AudioHubDriver.c" -o "$OUT/Contents/MacOS/AudioHub"
cp "$ROOT/Info.plist" "$OUT/Contents/Info.plist"
codesign --force --sign - "$OUT" 2>/dev/null || echo "Peringatan: tanda tangan ad-hoc gagal"
echo "Dibangun: $OUT"
