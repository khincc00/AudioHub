# AudioHub

Routing audio untuk macOS: matriks routing per pasangan stereo, gain dan mute, pan, solo, meter, scene, efek per input (gate, EQ, kompresor, delay, plugin Audio Unit, dan plugin VST2 dengan parameter yang bisa diatur), penangkapan audio per aplikasi dengan opsi isolasi (macOS 14.4 ke atas), dan driver perangkat virtual AVHub v2 yang ikut dipasang. Tiga pasangan virtual: VC 1/2, VC 3/4, dan Loopback 1/2. Antarmuka tersedia dalam Bahasa Inggris (default) dan Bahasa Indonesia. Tidak terkunci ke merek hardware tertentu.

- Versi aplikasi: **0.3.0** (build 3)
- Driver perangkat virtual: **AVHub v2** (versi 2.0.0)
- Lisensi: [LICENSE](LICENSE), berlaku untuk aplikasi dan driver

Status: **prototipe yang berfungsi di mesin uji.** Belum siap rilis publik. Lihat [checklist rilis](docs/release/checklist.md) dan [compatibility matrix](docs/compatibility/matrix.md).

## Persyaratan sistem

| Komponen | Minimum | Optimal |
|---|---|---|
| Sistem operasi | macOS 14.4 untuk aplikasi (diperlukan untuk menangkap audio per aplikasi). Driver membutuhkan macOS 11.0 | macOS terbaru. Pengujian dilakukan di Darwin 27 (macOS 27) |
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
4. Pastikan ketiga perangkat muncul:

   ```sh
   system_profiler SPAudioDataType | grep avhub
   ```

   Hasilnya harus menampilkan `avhub_virtual1`, `avhub_virtual2`, dan `avhub_loopback`. Jika belum, restart Mac.
5. Jika macOS memblokir karena paket belum dinotarisasi, klik kanan installer lalu pilih Open.

### Pencopotan

Hapus `/Applications/AudioHub.app` dan `/Library/Audio/Plug-Ins/HAL/AVHubDriver.driver` dengan kata sandi administrator, lalu muat ulang `coreaudiod` atau restart Mac. Langkah rinci untuk driver ada di [virtual-device/AVHubDriver/README.md](virtual-device/AVHubDriver/README.md).

## Cara pakai singkat

1. Pilih perangkat fisik dan virtual di panel kanan **Pengaturan Perangkat**, lalu tekan **Open** (Buka).
2. Panel kiri hanya menampilkan meter. Suara diteruskan dengan menekan sel di matriks tengah.
3. Tekan **Start** (Mulai) untuk menjalankan engine. Engine tidak berjalan otomatis saat aplikasi dibuka.
4. Untuk menangkap audio aplikasi, centang aplikasi di bagian **Aplikasi (tangkap audio)**. Centang **Isolasi** jika audio aplikasi tidak boleh terdengar lagi di output asli. Berlaku setelah Open.
5. Bahasa dipilih di menu roda gigi (**Bahasa** / **Language**). Default-nya Inggris.

## Cara membangun dan menguji

| Bagian | Perintah | Keterangan |
|---|---|---|
| Uji unit engine | `sh tests/unit/run.sh` | Tanpa perangkat keras |
| Tes driver | `sh virtual-device/AVHubDriver/tests/run_tests.sh` | Tidak memasang apa pun ke sistem |
| Aplikasi | `xcodebuild -project AudioHub.xcodeproj -target AudioHub -configuration Release` | Hasil build di folder `build/` |
| Edisi demo | `DEMO=1 sh apps/macos/AudioHub/build.sh` | Menghasilkan `build/AudioHub Demo.app`. Dibatasi: maks. 8 route aktif. VC 3/4, Loopback, tangkap aplikasi, efek/dinamika, rekaman, preset, dan template tampil tetapi terkunci |
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

- Agregat **AudioHub Aggregate** untuk DAW hanya ada saat perangkat terbuka (Open). Saat aplikasi ditangkap, engine memakai agregat private tambahan yang berisi tap, dan agregat publik untuk DAW tetap terlihat.
- PS22 dipakai dua klien sekaligus saat aplikasi ditangkap (agregat DAW dan engine AudioHub). Belum diuji dengan audio nyata di DAW.
- Tangkap audio per aplikasi butuh macOS 14.4 atau lebih baru, izin Audio Capture dari macOS, dan minimal satu virtual (VC 1/2) dipilih. Aplikasi harus sedang berjalan saat Buka ditekan.
- Aplikasi yang audionya keluar dari proses pembantu (helper), misalnya browser, mungkin hanya menghasilkan sebagian audio. Hanya aplikasi yang dikenal sistem sebagai aplikasi biasa yang muncul di daftar.

- Paket belum dinotarisasi oleh Apple. Aplikasi dan driver sudah ditandatangani Developer ID. Build dari sumber ditandatangani ad-hoc, sehingga izin mikrofon dan Audio Capture diminta lagi.
- Route dari virtual in ke virtual out dalam perangkat yang sama tidak mengirim audio. Sel itu hanya menandai perangkat virtual tersebut aktif. Audio ke VC In hanya datang saat output perangkat itu dikirimi sesuatu dari sel lain.
- Agregat dengan sub-device yang buffer-nya tidak sama menghasilkan klik periodik saat ada klien kedua. Karena itu aplikasi memakai 512 frame.
- Pemasangan driver belum diuji di mesin bersih, dan pencopotan belum diuji ulang.
- Isolasi aplikasi (Muted When Tapped) belum diuji secara audio. Pastikan audionya terdengar lewat AudioHub setelah diteruskan dari matriks.
- Aplikasi musik, panggilan, dan OBS belum diuji langsung. Tangkap audio Spotify sudah diuji: meter kanalnya bergerak saat diputar.
- Level route dalam perangkat virtual yang sama (VC In → VC Out) di 0 dB dapat berdengung. Atur level sendiri atau hindari route itu.
- Plugin VST2 dimuat dari file `.vst` atau `.dylib` yang dipilih lewat **Tambah plugin VST2…** di tab Plugin. Plugin diproses sebagai mono (kiri dan kanan sama, keluaran kiri dipakai). Plugin yang memakai SWELL dari REAPER, misalnya ReaFIR, tidak bisa dimuat di luar REAPER. Uji dilakukan dengan plugin uji buatan sendiri. Plugin VST2 pihak ketiga belum diuji.
