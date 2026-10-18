#!/bin/sh
# Memasang driver AVHub v2 ke Mac ini. Jalankan build.sh dulu.
# Pemakaian: sudo sh install.sh
set -e
ROOT="$(cd "$(dirname "$0")" && pwd)"
SRC="$ROOT/build/AVHubDriver.driver"
DST="/Library/Audio/Plug-Ins/HAL/AVHubDriver.driver"

if [ "$(id -u)" -ne 0 ]; then
  echo "Jalankan dengan sudo: sudo sh install.sh" >&2
  exit 1
fi
if [ ! -d "$SRC" ]; then
  echo "Bundle tidak ditemukan: $SRC (jalankan build.sh dulu)" >&2
  exit 1
fi

codesign --verify --deep --strict "$SRC"
rm -rf "$DST"
ditto "$SRC" "$DST"
chown -R root:wheel "$DST"
chmod -R go-w "$DST"

killall coreaudiod 2>/dev/null || true
echo "Driver terpasang di $DST"
echo "Jika avhub_virtual1 dan avhub_virtual2 belum muncul, restart Mac."
