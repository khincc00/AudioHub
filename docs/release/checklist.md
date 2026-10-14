# Checklist Rilis AudioHub

Status tiap item ditandai jujur. Item yang belum selesai belum boleh diklaim sebagai fitur.

## Build dan artefak

- [x] Engine (`engine/graph`, `engine/api`) lulus 20 uji unit (`sh tests/unit/run.sh`).
- [x] Backend CoreAudio (`backends/coreaudio/io`) dibangun dan lulus smoke test.
- [x] Aplikasi SwiftUI (`apps/macos/AudioHub`) dibangun menjadi bundel `.app` dengan izin mikrofon di `Info.plist`.
- [x] Driver virtual (`virtual-device/AudioHubDriver`) dibangun dan dipasang di mesin uji.
- [x] Paket instalasi driver (`AudioHub-Virtual-0.1.pkg`) dibangun dan ditandatangani Developer ID Installer (`pkg/build_pkg.sh`).
- [x] Tanda tangan developer ID untuk aplikasi dan driver (build Release dari `AudioHub.xcodeproj`, Hardened Runtime, timestamp).
- [ ] Notarisasi Apple. Butuh akun developer Anda.
- [ ] Uji instalasi paket di mesin bersih, termasuk uninstall.
- [ ] Pemasangan ulang driver dengan dua perangkat (AudioHub Virtual dan AudioHub Virtual 2) di mesin uji. Uji offline sudah lulus.

## Fungsi

- [x] Routing per kanal, gain, mute, dan send (uji unit dan perangkat).
- [x] Mix-minus dengan tiga sumber di perangkat.
- [x] Route virtual ke virtual ditolak di backend (mencegah loop).
- [x] Scene JSON dengan validasi versi dan kecocokan kanal.
- [x] Deteksi perangkat hilang: aplikasi berhenti dan memberi tahu pengguna.
- [x] Klik langsung di aplikasi (matriks, fader, meter, tombol). Dilaporkan lulus oleh pengguna; tanpa log dari saya.
- [x] Hot-plug (cabut-pasang PS22 saat berjalan). Dilaporkan lulus oleh pengguna; tanpa log dari saya.
- [x] Sleep/wake. Dilaporkan lulus oleh pengguna; tanpa log dari saya.
- [ ] Perubahan buffer atau sample rate saat berjalan.
- [x] OBS menerima audio dari AudioHub. Dilaporkan oleh pengguna; sumber dan levelnya belum dicatat.
- [ ] Aplikasi musik dan panggilan sungguhan.

## Stabilitas

- [x] Tes 60 detik dan 30 menit pada PS22 (spike passthrough), 0 dropout.
- [ ] Endurance 30 menit lewat aggregate dengan AudioHub Virtual (sedang berjalan, lihat log).
- [ ] Endurance 8 jam (kriteria blueprint). Belum dijalankan.

## Yang belum dikerjakan

- [ ] Pan untuk pasangan stereo dan solo yang hanya memengaruhi monitor.
- [x] Noise gate, EQ 3 band, kompresor, dan limiter (uji unit dan harness).
- [x] Plugin Audio Unit per input (uji host dengan AUDynamicsProcessor).
- [ ] Plugin VST3: perlu SDK Steinberg dan pengecekan lisensi.
- [x] Engine tetap berjalan saat jendela ditutup (diverifikasi: callback terus naik, 0 dropout).
- [x] Skema scene v1 disatukan dengan aplikasi, dan preset dari aplikasi tervalidasi terhadap skema.
- [x] Device binding dengan peran logis; peran yang perangkatnya hilang ditandai, tidak diganti otomatis.
- [x] Daftar perangkat dari sistem, nama dan warna kanal, delay, knee, template Podcast, redaksi diagnostik.
- [~] Investigasi per-app capture: process tap berhasil dibuat dan membaca audio dari `say` (RMS -15,6 dBFS). Belum diintegrasikan ke aplikasi.
- [x] Send per koneksi, tautan stereo, peak hold, clip, preset, template, sambung ulang otomatis (belum diuji langsung), dan ekspor diagnostik. Verifikasi: 17 cek harness pada perangkat.
- [ ] Ramp gain per sampel (sekarang per blok).
- [ ] Ring buffer driver dengan lock atau SPSC yang diuji penuh (saat ini fence memori).
- [ ] Penyebab buffer 192 di HAL.
- [ ] Graph view dan editor bus di aplikasi (sekarang hanya matriks langsung).
- [ ] Diagnostik dan ekspor laporan.
- [ ] Tahap 7: multi-device luas, DSP, plugin.
