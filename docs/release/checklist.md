# Checklist Rilis AudioHub

Status tiap item ditandai jujur. Item yang belum selesai belum boleh diklaim sebagai fitur.

## Build dan artefak

- [x] Engine (`engine/graph`, `engine/api`) lulus 20 uji unit (`sh tests/unit/run.sh`).
- [x] Backend CoreAudio (`backends/coreaudio/io`) dibangun dan lulus smoke test.
- [x] Aplikasi SwiftUI (`apps/macos/AudioHub`) dibangun menjadi bundel `.app` dengan izin mikrofon di `Info.plist`.
- [x] Driver virtual AudioHub (`virtual-device/AudioHubDriver`) dibangun dan diuji offline. Driver ini tidak lagi dipakai dan source-nya dihapus dari folder kerja (tersedia di riwayat git).
- [x] Installer DMG (`scripts/build_dmg.sh`) berisi aplikasi saja dan ditandatangani Developer ID. Driver tidak ikut dipaket.
- [x] Tanda tangan developer ID untuk aplikasi (build Release dari `AudioHub.xcodeproj`, Hardened Runtime, timestamp).
- [ ] Notarisasi Apple. Butuh akun developer Anda.
- [ ] Uji instalasi paket di mesin bersih, termasuk uninstall.
- [~] Pemasangan ulang driver dengan dua perangkat: dibatalkan. Driver AudioHub sudah tidak dipakai; jalur virtual memakai EMEET Virtual Audio.

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
- [ ] Endurance 30 menit lewat aggregate dengan EMEET Virtual Audio (belum dijalankan setelah perubahan ke buffer 512).
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
- [x] Jalur clock driver tanpa lock; ring disentuh dan dikunci saat driver dimuat (`a86af75`, `d032b9b`). Diuji offline.
- [x] Ukuran buffer driver mengikuti HAL (16-1024), timeline sejalan dengan blok IO (`a86af75`). Tes ring 192 dan 512 lulus offline.
- [~] Pemasangan ulang driver dari DMG: dibatalkan, driver sudah tidak dipaket.
- [ ] Uji audio langsung: kresek tidak terdengar selama sesi live, dan dashboard dropout tetap nol.
- [ ] Pembacaan input yang terlambat masih diisi nol. Ukur seberapa sering terjadi saat uji live.
- [ ] Graph view dan editor bus di aplikasi (sekarang hanya matriks langsung).
- [ ] Diagnostik dan ekspor laporan.
- [ ] Tahap 7: multi-device luas, DSP, plugin.

## Yang belum selesai setelah perbaikan klik dan klip

- [ ] Rekaman REAPER dengan suara bicara pada jalur EMEET 512 (belum direkam ulang setelah perbaikan).
- [ ] Aplikasi masih bergantung pada EMEET Virtual Audio (UID `EMEET_VirtualAudio_UID`). Pilihan perangkat virtual umum belum dikerjakan.
- [ ] Lisensi driver EMEET Virtual Audio untuk distribusi belum diperiksa. Driver itu tidak disertakan di DMG.
- [ ] Penyebab lonjakan CPU coreaudiod yang kadang tinggi belum diketahui.
- [ ] Penyebab bundle `/Applications/AudioHub.app` pernah hilang belum diketahui.
