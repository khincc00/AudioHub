#!/bin/sh
# Menjalankan semua uji driver tanpa memasang ke sistem.
# Pemakaian: sh tests/run_tests.sh
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/build/tests"
mkdir -p "$OUT"
CFLAGS="-arch arm64 -O1 -g -Wall -Wextra -Wno-unused-parameter -framework CoreAudio -framework CoreFoundation"

echo "== Uji logika (loopback, volume, mute, pitch, format) =="
clang $CFLAGS -o "$OUT/loopback_test" "$ROOT/tests/loopback_test.c"
"$OUT/loopback_test"

echo
echo "== Uji pemuatan bundle lewat CFPlugIn =="
sh "$ROOT/build.sh" >/dev/null
clang $CFLAGS -o "$OUT/plugin_load_test" "$ROOT/tests/plugin_load_test.c"
"$OUT/plugin_load_test" "$ROOT/build/AVHubDriver.driver"
