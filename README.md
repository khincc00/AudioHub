# AudioHub

Routing audio profesional untuk macOS: matriks routing per kanal, gain dan mute, meter, scene, dan virtual device sendiri. Tidak terkunci ke merek hardware tertentu.

Status: **prototipe yang berfungsi di mesin uji.** Belum siap rilis. Lihat [checklist rilis](docs/release/checklist.md) dan [compatibility matrix](docs/compatibility/matrix.md).

## Cara membangun dan menguji

| Bagian | Perintah | Keterangan |
|---|---|---|
| Uji unit engine | `sh tests/unit/run.sh` | Tanpa perangkat keras |
| Aplikasi | `xcodebuild -project AudioHub.xcodeproj -target AudioHub -configuration Release` | Hasil build di folder `build/` |
| Installer DMG | `sh scripts/build_dmg.sh` | Menghasilkan `build/AudioHub-0.1.dmg` (hanya aplikasi, ditandatangani Developer ID) |

Aplikasi memakai perangkat virtual yang sudah terpasang di Mac (driver dari vendor mana pun, misalnya perangkat virtual atau BlackHole 2ch). DMG tidak menyertakan driver itu.

## Struktur

```text
apps/macos/AudioHub/        Aplikasi SwiftUI (matriks, mixer, meter, scene)
backends/coreaudio/io/      Backend CoreAudio: perangkat fisik, aggregate dengan virtual device
engine/graph/               Graph routing: channel, bus, output, gain, send, meter
engine/api/                 API C untuk engine
contracts/scene-schema/     Skema scene versi 1
tests/                      Uji unit dan probe perangkat
docs/                       Blueprint, log tiap tahap, compatibility, checklist rilis
```

## Batasan yang sudah diketahui

- Route dari input virtual ke output virtual ditolak jika keduanya dari perangkat virtual yang sama, karena akan membentuk loop di driver. Route antar dua perangkat virtual berbeda diizinkan, tetapi saat ini hanya bisa diuji jika dua perangkat virtual terpasang.
- Aplikasi memakai buffer 512 frame. PS22 dan perangkat virtual berjalan di 512 frame di macOS ini. Agregat dengan sub-device yang tidak sama buffer-nya menghasilkan klik periodik saat ada klien kedua.
- Driver AudioHub Virtual dibatasi HAL ke 192 frame. Karena itu driver tidak dipakai lagi.
- Virtual 1 dan Virtual 2 dipilih dari menu virtual, dari perangkat yang punya transport virtual dan kanal input serta output. Tidak ada perangkat yang wajib. Tanpa perangkat virtual, aplikasi tetap bisa dipakai untuk routing fisik.
- Driver dan aplikasi belum ditandatangani dengan identitas developer, dan belum dinotarisasi.
- Pan, solo, dan graph view belum ada.
- Aplikasi musik, panggilan, dan OBS belum diuji langsung.
