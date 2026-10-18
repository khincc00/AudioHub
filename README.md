# AudioHub

Routing audio untuk macOS: matriks routing per kanal, gain dan mute, meter, scene, efek per input (gate, EQ, kompresor, delay), dan driver perangkat virtual AVHub v2 yang ikut dipasang. Tidak terkunci ke merek hardware tertentu.

- Versi aplikasi: **0.3.0** (build 3)
- Driver perangkat virtual: **AVHub v2** (versi 2.0.0)
- Lisensi: [LICENSE](LICENSE), berlaku untuk aplikasi dan driver

Status: **prototipe yang berfungsi di mesin uji.** Belum siap rilis publik. Lihat [checklist rilis](docs/release/checklist.md) dan [compatibility matrix](docs/compatibility/matrix.md).

## Persyaratan sistem

| Komponen | Minimum | Optimal |
|---|---|---|
| Sistem operasi | macOS 13.0 untuk aplikasi. Driver membutuhkan macOS 11.0 | macOS terbaru. Pengujian dilakukan di Darwin 27 (macOS 27) |
| Arsitektur | Intel (x86_64) atau Apple Silicon (arm64). Biner bersifat universal | Apple Silicon (arm64). Dukungan Intel ada di biner, belum diuji langsung |
| Perangkat audio | Perangkat input dan output yang dikenali Core Audio | Audio interface USB. Diuji dengan Maono ProStudio 2x2 (PS22) |
| Buffer | 512 frame (default). Driver mendukung 16–4096 frame | 512 frame (sekitar 10,7 ms per tahap). Buffer 128 frame pernah menghasilkan 1 dropout dalam uji 3 detik, dan 64 frame belum diuji |
| Sample rate | 48 kHz | 48 kHz |
| Memori dan CPU | Belum diukur | Belum diukur. Perlu uji di mesin minimum sebelum angka dipublikasikan |
| Hak administrator | Diperlukan untuk memasang atau mencopot driver | Sama |
| Izin sistem | Akses mikrofon, jika macOS memintanya saat input dipakai | Sama |
| Membangun dari sumber | Xcode dengan Command Line Tools. Sertifikat Developer ID di keychain untuk membuat DMG | Xcode 27, versi yang dipakai saat pengujian |

## Pemasangan

1. Buka `AudioHub-0.3.0.dmg`.
2. Jalankan `Install AudioHub.pkg`, lalu masukkan kata sandi administrator saat diminta.
3. Installer memasang:
   - `AudioHub.app` ke `/Applications`;
   - driver `AVHubDriver.driver` ke `/Library/Audio/Plug-Ins/HAL`, lalu memuat ulang layanan `coreaudiod`.
4. Pastikan kedua perangkat muncul:

   ```sh
   system_profiler SPAudioDataType | grep avhub
   ```

   Hasilnya harus menampilkan `avhub_virtual1` dan `avhub_virtual2`. Jika belum, restart Mac.
5. Jika macOS memblokir karena paket belum dinotarisasi, klik kanan installer lalu pilih Open.

### Pencopotan

Hapus `/Applications/AudioHub.app` dan `/Library/Audio/Plug-Ins/HAL/AVHubDriver.driver` dengan kata sandi administrator, lalu muat ulang `coreaudiod` atau restart Mac. Langkah rinci untuk driver ada di [virtual-device/AVHubDriver/README.md](virtual-device/AVHubDriver/README.md).

## Cara membangun dan menguji

| Bagian | Perintah | Keterangan |
|---|---|---|
| Uji unit engine | `sh tests/unit/run.sh` | Tanpa perangkat keras |
| Tes driver | `sh virtual-device/AVHubDriver/tests/run_tests.sh` | Tidak memasang apa pun ke sistem |
| Aplikasi | `xcodebuild -project AudioHub.xcodeproj -target AudioHub -configuration Release` | Hasil build di folder `build/` |
| AudioHub Studio | `xcodegen generate --spec AudioHubStudio.yml`, lalu build target `AudioHubStudio` | Routing AudioHub ditambah rantai FX (Audio Unit) per track |
| Probe AU | `sh tests/integration/run_au_probe.sh` | Memuat AU bawaan Apple, tanpa perangkat audio |
| Installer DMG | `sh scripts/build_dmg.sh` | Menjalankan tes driver, membangun aplikasi dan driver, lalu menghasilkan `build/AudioHub-0.3.0.dmg`. Perlu sertifikat Developer ID |

Aplikasi memakai driver AVHub v2 dari `virtual-device/AVHubDriver/`. Driver ini ikut dalam DMG dan dipasang otomatis bersama aplikasi.

## Struktur

```text
apps/macos/AudioHub/        Aplikasi SwiftUI (matriks, mixer, meter, scene)
apps/macos/AudioHubStudio/  AudioHub Studio: track, rantai FX Audio Unit, routing, proyek (memakai Model.swift bersama)
backends/coreaudio/io/      Backend CoreAudio: perangkat fisik, aggregate dengan virtual device, rantai FX
engine/graph/               Graph routing: channel, bus, output, gain, send, meter
engine/api/                 API C untuk engine
engine/fx/                  Aturan nama dan penyisipan FX
virtual-device/AVHubDriver/ Driver perangkat virtual AVHub v2, tes, dan skrip pemasangan
contracts/scene-schema/     Skema scene versi 1
tests/                      Uji unit dan probe perangkat
docs/                       Blueprint, log tiap tahap, compatibility, checklist rilis, status Studio
```

Status AudioHub Studio terhadap spesifikasi REAPER: [docs/studio/status-spesifikasi.md](docs/studio/status-spesifikasi.md).

## Batasan yang sudah diketahui

- Paket belum dinotarisasi oleh Apple. Aplikasi dan driver sudah ditandatangani Developer ID.
- Route dari input virtual ke output virtual ditolak jika keduanya berasal dari perangkat virtual yang sama, karena akan membentuk loop di driver. Route antara `avhub_virtual1` dan `avhub_virtual2` diizinkan.
- Agregat dengan sub-device yang buffer-nya tidak sama menghasilkan klik periodik saat ada klien kedua. Karena itu aplikasi memakai 512 frame.
- Pemasangan driver belum diuji di mesin bersih, dan pencopotan belum diuji ulang.
- Pan, solo, dan graph view belum ada.
- Aplikasi musik, panggilan, dan OBS belum diuji langsung.
