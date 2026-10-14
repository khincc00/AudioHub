#!/bin/sh
# Membangun paket instalasi driver (tidak ditandatangani, tidak memasang apa pun).
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
STAGE="$ROOT/build/pkgroot"
OUT="$ROOT/build/AudioHub-Virtual-0.1.pkg"
sh "$ROOT/build.sh" >/dev/null
find "$ROOT/build" -name "._*" -delete
rm -rf "$STAGE"
mkdir -p "$STAGE/Library/Audio/Plug-Ins/HAL"
ditto --norsrc --noextattr --noacl "$ROOT/build/AudioHub.driver" "$STAGE/Library/Audio/Plug-Ins/HAL/AudioHub.driver"
dot_clean -m "$STAGE" 2>/dev/null || true
find "$STAGE" -name "._*" -delete
pkgbuild --root "$STAGE" \
    --identifier com.audiohub.virtual.driver \
    --version 0.1 \
    --install-location / \
    --ownership recommended \
    --scripts "$ROOT/pkg/scripts" \
    "$OUT"
echo "Paket: $OUT"
