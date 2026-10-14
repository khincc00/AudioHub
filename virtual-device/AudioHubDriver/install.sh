#!/bin/sh
# Pemasangan manual. Butuh hak admin dan me-restart coreaudiod (audio akan terputus sebentar).
# Jalankan sendiri di Terminal: sh install.sh
set -e
ROOT="$(cd "$(dirname "$0")" && pwd)"
DEST=/Library/Audio/Plug-Ins/HAL
if [ ! -d "$ROOT/build/AudioHub.driver" ]; then
    echo "Belum dibangun. Jalankan sh build.sh dulu."; exit 1
fi
sudo rm -rf "$DEST/AudioHub.driver"
sudo cp -R "$ROOT/build/AudioHub.driver" "$DEST/"
sudo chown -R root:wheel "$DEST/AudioHub.driver"
sudo killall coreaudiod || true
echo "Terpasang. Periksa Audio MIDI Setup untuk perangkat \"AudioHub Virtual\"."
