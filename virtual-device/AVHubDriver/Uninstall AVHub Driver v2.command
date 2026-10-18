#!/bin/sh
# Hapus driver AVHub v2 dengan satu klik.
# Klik dua kali file ini. macOS akan meminta password admin lewat jendela sistem.
ROOT="$(cd "$(dirname "$0")" && pwd)"

show_dialog() {
  /usr/bin/osascript - "$1" >/dev/null 2>&1 <<'DIALOG_SCRIPT'
on run argv
  display dialog (item 1 of argv) buttons {"OK"} default button "OK" with title "AVHub Driver v2"
end run
DIALOG_SCRIPT
}

# Salin ke folder sementara di luar Documents, lalu jalankan dari sana (lihat komentar di file pemasangan).
STAGE="$(mktemp -d "${TMPDIR:-/tmp}/avhub-uninstall.XXXXXX")" || {
  MSG="Gagal membuat folder sementara."
  echo "$MSG"
  show_dialog "$MSG"
  exit 1
}
trap 'rm -rf "$STAGE"' EXIT
if ! cp "$ROOT/uninstall.sh" "$STAGE/"; then
  MSG="Gagal menyalin file penghapusan."
  echo "$MSG"
  show_dialog "$MSG"
  exit 1
fi

RESULT=$(/usr/bin/osascript - "$STAGE/uninstall.sh" 2>&1 <<'UNINSTALL_SCRIPT'
on run argv
  try
    do shell script "/bin/sh " & quoted form of (item 1 of argv) with administrator privileges
    return "OK"
  on error errMsg number errNum
    if errNum is -128 then return "BATAL"
    return "GAGAL: " & errMsg
  end try
end run
UNINSTALL_SCRIPT
)

case "$RESULT" in
  OK) MSG="Driver AVHub v2 sudah dihapus." ;;
  BATAL) MSG="Penghapusan dibatalkan." ;;
  *) MSG="$RESULT" ;;
esac

echo "$MSG"
show_dialog "$MSG"
