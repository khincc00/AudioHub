#!/bin/sh
# Bangun lalu pasang driver AVHub v2 dengan satu klik.
# Klik dua kali file ini. macOS akan meminta password admin lewat jendela sistem.
ROOT="$(cd "$(dirname "$0")" && pwd)"

show_dialog() {
  /usr/bin/osascript - "$1" >/dev/null 2>&1 <<'DIALOG_SCRIPT'
on run argv
  display dialog (item 1 of argv) buttons {"OK"} default button "OK" with title "AVHub Driver v2"
end run
DIALOG_SCRIPT
}

if ! sh "$ROOT/build.sh"; then
  MSG="Pembangunan gagal. Pastikan Xcode Command Line Tools terpasang (jalankan: xcode-select --install)."
  echo "$MSG"
  show_dialog "$MSG"
  exit 1
fi

# Salin file yang dipasang ke folder sementara di luar Documents, lalu pasang dari sana.
# Proses root dari osascript gagal membaca folder Documents ("Operation not permitted").
STAGE="$(mktemp -d "${TMPDIR:-/tmp}/avhub-install.XXXXXX")" || {
  MSG="Gagal membuat folder sementara."
  echo "$MSG"
  show_dialog "$MSG"
  exit 1
}
trap 'rm -rf "$STAGE"' EXIT
mkdir -p "$STAGE/build"
if ! cp -R "$ROOT/build/AVHubDriver.driver" "$STAGE/build/" || ! cp "$ROOT/install.sh" "$STAGE/"; then
  MSG="Gagal menyalin file pemasangan."
  echo "$MSG"
  show_dialog "$MSG"
  exit 1
fi

RESULT=$(/usr/bin/osascript - "$STAGE/install.sh" 2>&1 <<'INSTALL_SCRIPT'
on run argv
  try
    do shell script "/bin/sh " & quoted form of (item 1 of argv) with administrator privileges
    return "OK"
  on error errMsg number errNum
    if errNum is -128 then return "BATAL"
    return "GAGAL: " & errMsg
  end try
end run
INSTALL_SCRIPT
)

case "$RESULT" in
  OK) MSG="Driver AVHub v2 terpasang. Jika perangkat belum muncul, restart Mac." ;;
  BATAL) MSG="Pemasangan dibatalkan." ;;
  *) MSG="$RESULT" ;;
esac

echo "$MSG"
show_dialog "$MSG"
