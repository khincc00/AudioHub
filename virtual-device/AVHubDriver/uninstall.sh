#!/bin/sh
# Menghapus driver AVHub v2 dari Mac ini.
# Pemakaian: sudo sh uninstall.sh
set -e
DST="/Library/Audio/Plug-Ins/HAL/AVHubDriver.driver"

if [ "$(id -u)" -ne 0 ]; then
  echo "Jalankan dengan sudo: sudo sh uninstall.sh" >&2
  exit 1
fi

rm -rf "$DST"
killall coreaudiod 2>/dev/null || true
echo "Driver dihapus dari $DST"
