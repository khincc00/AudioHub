#!/bin/sh
# Menghapus driver dari sistem. Butuh hak admin.
set -e
sudo rm -rf /Library/Audio/Plug-Ins/HAL/AudioHub.driver
sudo killall coreaudiod || true
echo "Driver AudioHub dihapus."
