#!/bin/sh
# Uji offline driver: dua perangkat, nama/UID, stream, dan IO terpisah. Tidak memasang apa pun.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$(mktemp -d)"
clang -std=c11 -O0 -w -framework CoreAudio -framework CoreFoundation "$ROOT/tests/twodevice_test.c" -o "$OUT/t"
"$OUT/t"
rm -rf "$OUT"
