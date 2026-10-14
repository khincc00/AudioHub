# Log Tahap 2: Virtual Routing

Spike: `backends/coreaudio/spike/virtual_cable.cpp`. Engine yang sama dengan Tahap 1 (`engine/graph`), dijalankan di atas **aggregate device privat** yang dibuat sementara:

- Master: Maono ProStudio 2x2 (PS22), tanpa drift compensation.
- Sub-device: EMEET Virtual Audio, dengan drift compensation.
- Aggregate dibuat saat tes dan dihapus setelah tes. Sudah diverifikasi tidak tersisa di `system_profiler`.

Virtual cable yang dipakai adalah EMEET Virtual Audio, karena ini satu-satunya perangkat virtual yang terpasang. Ini bukan virtual device produk.

## Hasil

### Receive: aplikasi → virtual cable → engine → PS22

Tone 880 Hz, −30 dBFS, diputar ke EMEET output 1 selama 3 detik.

| Titik | Level |
|---|---|
| EMEET input 1 (masuk engine) | −30,0 dBFS |
| PS22 output 1 | −30,0 dBFS |
| PS22 output 2 | −inf (tanpa kebocoran) |

Dropout 0, frame hilang 0, selama 9 detik.

### Send: PS22 input 1 → engine → virtual cable → EMEET input 1

Ambient mic dikirim ke EMEET output 1, lalu dibaca kembali dari EMEET input 1.

| Titik | Level (kisaran per detik) |
|---|---|
| PS22 input 1 | −42,5 sampai −46,5 dBFS |
| EMEET output 1 (dikirim engine) | sama dengan input 1 di setiap detik |
| EMEET input 1 (dibaca kembali) | −42,9 sampai −47,2 dBFS, mengikuti dengan selisih sekitar 1 sampai 3 dB |

Dropout 0, frame hilang 0, selama 8 detik.

## Kriteria Tahap 2 (dari blueprint)

- [x] Alur kirim/terima melalui virtual cable terbukti: receive dan send berjalan pada aggregate.
- [x] Dropout 0 selama tes.
- [x] Latensi loopback lewat AudioHub Virtual diukur: 29,42 ms pada buffer 192 frame. Angka ini didominasi oleh lag input 1024 frame di driver. Perbaikan (lag mengikuti 2 × buffer aktif) sudah ditulis dan lulus uji ring buffer offline, tetapi perlu dipasang ulang dan diukur lagi.
- [x] Pengukuran latensi setelah perbaikan driver: **16,08 ms** pada buffer 192 frame (turun dari 29,42 ms). Driver yang terpasang sudah sama dengan build terbaru (MD5 cocok).
- [x] Aplikasi sistem sungguhan (`say`) diuji dengan default output diarahkan ke AudioHub Virtual. Suara masuk ke input AudioHub Virtual dengan level sekitar −15 dBFS selama bicara, lalu diam. Default output dikembalikan ke PS22.
- [ ] Pemutar musik, panggilan, dan OBS belum diuji. Pengujian dengan `say` menunjukkan jalurnya bekerja, tetapi aplikasi lain perlu dicoba langsung.
- [ ] Pemisahan per aplikasi belum dikerjakan. Ini investigasi terpisah di blueprint.

## Catatan

- Aggregate dibuat sementara. Jika spike crash, aggregate bisa tertinggal dan perlu dihapus manual dari Audio MIDI Setup.
- Uji receive memutar audio ke EMEET output. Jangan pasang kabel loopback saat spike berjalan.
- Virtual cable EMEET bukan produk sendiri. Ketergantungan pada perangkat ini harus dihapus di Tahap 5.

## Dua virtual device

Driver sekarang membuat dua perangkat virtual: `AudioHub Virtual` dan `AudioHub Virtual 2`. Masing-masing punya UID, ring buffer, dan clock sendiri. Uji offline (`virtual-device/AudioHubDriver/tests/run.sh`) lulus: daftar perangkat, nama dan UID, stream, dan IO yang terpisah. Aplikasi bisa memakai keduanya sekaligus lewat dua pilihan di bar atas. Perlu pemasangan ulang driver di mesin uji sebelum diuji langsung.

## Ulangi dengan virtual device sendiri (AudioHub Virtual)

Spike yang sama dijalankan dengan `com.audiohub.virtual.device` sebagai sub-device, menggantikan EMEET.

| Mode | Hasil | Dropout |
|---|---|---|
| Receive | Tone −30 dBFS di virtual input, −30,0 dBFS di PS22 output 1, output 2 diam | 0 dalam 9 detik |
| Send | Level yang dikirim sama dengan input mic, kembali dari virtual cable | 0 dalam 8 detik |

Catatan: Core Audio melaporkan buffer 192 frame dengan rentang 15–192, bukan 512 yang diset di driver. Fungsi driver yang dipanggil langsung mengembalikan 512, jadi penyebabnya ada di sisi HAL. Belum ditemukan penyebab pasti. Tidak menghalangi fungsi audio.

Latensi loopback lewat AudioHub Virtual: 29,42 ms, sama dengan PS22 karena lag input yang tetap 1024 frame di driver. Perbaikan sudah ditulis, lihat `virtual-device/README.md`.
