# AudioHub

Routing audio profesional untuk macOS: matriks routing per kanal, gain dan mute, meter, scene, dan virtual device sendiri. Tidak terkunci ke merek hardware tertentu.

Status: **prototipe yang berfungsi di mesin uji.** Belum siap rilis. Lihat [checklist rilis](docs/release/checklist.md) dan [compatibility matrix](docs/compatibility/matrix.md).

## Cara membangun dan menguji

| Bagian | Perintah | Keterangan |
|---|---|---|
| Uji unit engine | `sh tests/unit/run.sh` | 20 cek, tanpa perangkat keras |
| Aplikasi | `sh apps/macos/AudioHub/build.sh` | Menghasilkan `build/AudioHub.app` |
| Driver virtual | `sh virtual-device/AudioHubDriver/build.sh` | Tidak memasang apa pun |
| Paket driver | `sh virtual-device/AudioHubDriver/pkg/build_pkg.sh` | Paket tanpa tanda tangan |
| Pasang driver | `sh virtual-device/AudioHubDriver/install.sh` | Butuh password admin; me-restart `coreaudiod` |
| Lepas driver | `sh virtual-device/AudioHubDriver/uninstall.sh` | Butuh password admin |

## Struktur

```text
apps/macos/AudioHub/        Aplikasi SwiftUI (matriks, mixer, meter, scene)
backends/coreaudio/io/      Backend CoreAudio: perangkat fisik, aggregate dengan virtual device
engine/graph/               Graph routing: channel, bus, output, gain, send, meter
engine/api/                 API C untuk engine
virtual-device/             Driver virtual AudioHub (AudioServerPlugIn)
contracts/scene-schema/     Skema scene versi 1
tests/                      Uji unit dan probe perangkat
docs/                       Blueprint, log tiap tahap, compatibility, checklist rilis
```

## Batasan yang sudah diketahui

- Route dari input virtual ke output virtual selalu ditolak, karena akan membentuk loop di driver.
- Core Audio melaporkan buffer 192 frame untuk driver virtual, penyebabnya belum diketahui.
- Driver dan aplikasi belum ditandatangani dengan identitas developer, dan belum dinotarisasi.
- Pan, solo, dan graph view belum ada.
- Aplikasi musik, panggilan, dan OBS belum diuji langsung.
