# Log Tahap 3: Aplikasi Desktop (MVP)

Kode: `apps/macos/AudioHub/Sources/` (SwiftUI), dibangun dengan `sh apps/macos/AudioHub/build.sh`.

## Yang sudah ada

- **Pemilihan perangkat**: nama perangkat fisik (default `Maono`), dengan opsi AudioHub Virtual. Keduanya dijalankan sebagai satu aggregate privat, dan aggregate dihapus saat aplikasi menutup perangkat.
- **Matriks routing**: input × output dengan toggle. Kanal diberi label Fisik atau Virtual.
- **Route virtual ke virtual ditolak** di backend dan di UI (toggle dinonaktifkan), untuk mencegah loop di driver.
- **Mixer**: fader gain −60 sampai +12 dB per input, tombol bisu, dan meter per input.
- **Meter output**: peak per output dengan warna hijau, kuning, dan merah.
- **Status**: jumlah callback, jumlah dropout, dan buffer aktif, ditampilkan tiap 0,1 detik.
- **Deteksi perangkat hilang**: jika perangkat tidak lagi hidup di Core Audio, aplikasi berhenti dan memberi tahu pengguna. Tidak pindah ke perangkat lain secara diam-diam.
- **Scene**: simpan dan muat JSON versi 1 di `~/Library/Application Support/AudioHub/scene.json`. Muat menolak scene dengan jumlah kanal yang tidak cocok, atau route virtual ke virtual.

## Verifikasi yang sudah dilakukan

- Aplikasi dibangun, bundel `.app` dibuat dengan `Info.plist` dan izin mikrofon, dan aplikasi bisa berjalan tanpa crash.
- Backend diuji dengan `backends/coreaudio/io/tests/smoke.c`:
  - Aggregate PS22 + AudioHub Virtual: 4 input dan 4 output, 2 di antaranya fisik.
  - Route virtual ke virtual ditolak.
  - Receive: tone di AudioHub output 1 keluar di PS22 output 1 pada −29 dBFS, 0 dropout.
- Tata letak diubah mengikuti acuan: daftar input di kiri, matriks routing di tengah, panel preset dan pengaturan perangkat di kanan, strip output dengan fader vertikal, meter, mute, dan tombol 0 dB di bawah. Warna aksen dipilih sendiri dan tidak memakai identitas visual merek lain.
- Fader dan mute output ditambahkan ke engine (`ah_engine_set_output`, `ah_io_set_output`), dengan uji unit (22 cek lulus).
- Uji interaksi lewat accessibility di mesin ini: perangkat terbuka dengan label Fisik dan Virtual, Mulai menjalankan engine (buffer 192, 776 callback, 0 dropout), dan Muat scene menolak scene lama dengan jumlah kanal berbeda.
- Screenshot belum bisa diambil, karena Terminal belum punya izin perekaman layar. Tampilan visual perlu dicek langsung oleh pengguna.

## Yang belum ada

- Graph view dan editor bus. Matriks hanya menghubungkan input langsung ke output.
- Pan untuk pasangan stereo, dan solo.
- Pemilihan perangkat dari daftar. Sekarang masih berupa nama yang diketik.
- Diagnostik dan ekspor laporan.
- Mengganti buffer atau sample rate saat berjalan.
- Penyimpanan scene terakhir saat aplikasi dibuka ulang.

## Perbaikan setelah laporan "aplikasi stuck"

- Diagnosis: proses tidak macet. Thread utama menunggu event, dan thread audio tidak bermasalah. Penyebab beban CPU sekitar 20–30% adalah SwiftUI yang menghitung ulang seluruh jendela setiap 0,1 detik karena meter.
- Perbaikan: meter dan statistik dipisah ke objek `Meters`. Hanya subview meter yang dihitung ulang tiap tick.
- Hasil: CPU sekitar 7–14% saat engine berjalan, dengan engine tetap berjalan normal.
- Catatan: laporan "stuck" dari pengguna belum dirinci langkahnya. Jika masih terjadi, perlu diketahui tombol mana yang ditekan dan apa yang tampil saat itu.

## Fitur tambahan dari daftar PS22

Pemetaan lengkap ada di [feature-coverage.md](../compatibility/feature-coverage.md).

- **Send per koneksi:** klik kanan di sel matriks untuk 0, −6, atau −12 dB, atau mematikan koneksi.
- **Tautan stereo:** dua input bisa ditautkan. Gain dan mute berubah bersama.
- **Peak hold dan clip:** garis putih menunjukkan puncak yang tertahan. Lampu merah menyala saat level mencapai −0,5 dBFS, dan bisa dihapus dengan klik.
- **Jumlah sumber per output:** ditampilkan di setiap strip output.
- **Preset bernama:** simpan, muat, dan hapus. Preset terakhir dimuat otomatis saat aplikasi dibuka.
- **Template:** Streaming, Online Meeting, dan Recording.
- **Sambung ulang otomatis:** bisa dimatikan. Jika aktif, aplikasi mencoba menyambung setiap 2 detik setelah perangkat terputus.
- **Ekspor diagnostik:** file teks berisi konfigurasi, routing, dan status.

Verifikasi: harness CLI (`apptest`) menjalankan 17 cek pada perangkat sungguhan dan semuanya lulus. Sambung ulang otomatis belum diuji, karena butuh pencabutan perangkat saat aplikasi berjalan.

## DSP dan plugin

- Engine: noise gate, EQ 3 band, dan kompresor per input, serta limiter per output (`engine/dsp/`). Parameter diubah dari UI dengan mutex yang tidak menunggu di jalur audio.
- Backend: host Audio Unit per input (`backends/coreaudio/io/au_insert.cpp`). Plugin dipasang hanya saat IO berhenti, jadi aplikasi berhenti sebentar saat memasang.
- Aplikasi: panel "Dinamika" per input dan "Limiter" per output. Parameter ikut tersimpan di preset.
- Verifikasi: 13 uji DSP, uji host AU, dan harness aplikasi (27 cek, dropout 0 dengan DSP aktif).
- Belum: VST3 (perlu SDK dan lisensi), sidechain, lookahead limiter, filter HP/LP, dan GUI parameter plugin.

## Putaran kedua: sinkronisasi dengan blueprint

- Skema scene v1 disatukan: `contracts/scene-schema/scene.v1.schema.json` sekarang sama dengan format yang ditulis aplikasi. Preset hasil aplikasi divalidasi dengan jsonschema (valid). Format lama tetap bisa dimuat lewat jalur migrasi; efek DSP pada format lama tidak ikut dimuat.
- Device binding dengan peran logis: "Main Microphone", "Input 2", "Virtual Input 1..n", "Monitor Output", "Output 2", "Virtual Output 1..n". Peran yang perangkatnya tidak tersedia ditandai di panel, tidak diganti otomatis.
- Daftar perangkat dari sistem (dengan jumlah kanal) menggantikan kolom nama yang diketik.
- Nama kanal yang bisa diubah dan warna kanal (6 pilihan).
- Delay per input (sampai 1 detik, buffer dialokasikan saat engine dibuat) dan knee pada kompresor.
- Template Podcast: mic ke monitor dan virtual out, dengan gate, kompresor knee 6 dB, dan EQ presence.
- Diagnostik dengan redaksi nama perangkat dan UID (aktif secara default).
- Engine tetap berjalan saat jendela ditutup. Aplikasi hanya menutup engine saat keluar.

Verifikasi: 17 uji DSP lulus, 22 uji graph lulus, uji host AU lulus, dan harness aplikasi 24 cek lulus pada perangkat (termasuk DSP dan delay aktif, 501 callback, 0 dropout).

Belum dikerjakan di putaran ini: bus Voice/Desktop/Communication/Monitor/Stream/Record di UI, pan dan solo, graph view, dashboard lengkap, Recorder node, limiter dengan lookahead, sidechain kompresor, penyebab buffer 192 di HAL, ring buffer dengan lock di driver, perubahan sample rate saat berjalan, dan integrasi per-app capture ke aplikasi.

## Putaran ketiga

- Pan per input (untuk pasangan output kiri-kanan) dengan hukum linear: kiri = min(1, 1 − pan), kanan = min(1, 1 + pan). Uji API engine lulus.
- Solo: hanya memengaruhi monitor (output 1). Input yang di-solo menjadi satu-satunya sumber monitor. Routing matriks tidak berubah.
- Sidechain pada kompresor: detektor memakai sinyal input lain. Uji DSP: −29,0 dB tanpa key, −41,3 dB dengan key.
- Limiter lookahead (0–20 ms): penjumlahan minimum sliding, batas tidak dilewati, latensi = lookahead. Uji DSP dan impuls lulus.
- Rekaman WAV float 32-bit mono per output, ditulis di thread terpisah lewat ring SPSC. Uji: 72.000 frame untuk 1,5 detik, 0 frame terbuang, header RIFF valid.
- Tampilan graph (hanya menampilkan) dan dashboard ringkas di panel kanan.
- Driver: penanda tulis per perangkat. Pembaca memakai nol untuk posisi yang belum ditulis. Uji offline lulus. Perlu pemasangan ulang agar berlaku.
- Investigasi per-app capture: process tap berhasil dibuat dan membaca audio dari `say` (RMS −15,6 dBFS, puncak −1,8 dBFS). Perlu izin System Audio Recording pada bundel.

Belum: bus Voice/Desktop/Communication/Monitor/Stream/Record di UI, integrasi per-app capture ke aplikasi, penyebab buffer 192 di HAL (butuh percobaan dengan pemasangan ulang driver), perubahan sample rate saat berjalan.
