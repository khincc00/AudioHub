#!/bin/sh
# Runs the AVHub loopback FIFO unit test on macOS or Linux. Run from the repository root:
#   sh windows/driver/tests/run.sh
set -e
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
DRV="$ROOT/windows/driver"
OUT="$(mktemp -d)"
clang++ -std=c++17 -O1 -g -Wall -Wextra -Werror -Wno-unknown-pragmas -Wno-unused-parameter \
  -I "$DRV/tests/shim" -I "$DRV/src/Source/Inc" \
  "$DRV/src/Source/Main/avhub_loopback.cpp" "$DRV/tests/test_avhub_fifo.cpp" -o "$OUT/test_avhub_fifo"
"$OUT/test_avhub_fifo"
rm -rf "$OUT"
