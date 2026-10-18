#!/bin/sh
# Probe AU insert dengan efek bawaan Apple (AUDelay). Tidak memerlukan perangkat audio. Dari root repo:
#   sh tests/integration/run_au_probe.sh
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$(mktemp -d)"
clang++ -std=c++17 -O1 -Wall -Wextra -Werror "$ROOT/backends/coreaudio/io/au_insert.cpp" "$ROOT/tests/integration/au_insert_probe.cpp" \
    -framework AudioToolbox -framework CoreFoundation -framework CoreAudio -o "$OUT/au_insert_probe"
"$OUT/au_insert_probe"
rm -rf "$OUT"
