# Compatibility Matrix AudioHub

Dua status dipisah sesuai prinsip produk:

- **Terdeteksi**: perangkat muncul di Core Audio dan kanalnya terbaca.
- **Tervalidasi**: lulus uji Tahap 0 sampai 2 pada konfigurasi tersebut, termasuk loopback dan uji endurance.

| Perangkat | Mode | Terdeteksi | Tervalidasi | Catatan |
|---|---|---|---|---|
| Maono ProStudio 2x2 (PS22) | Stereo, buffer 512 | Ya | Ya, sebagian | Input 1 dan 2 terpisah hanya di mode stereo. Loopback 29 ms (buffer 512). Tes 30 menit lulus. Monitoring headphone PS22 punya jalur sendiri dan tidak bisa diasumsikan mengikuti routing macOS. |
| Maono ProStudio 2x2 (PS22) | Mono | Ya | Tidak | Kedua kanal berisi sinyal yang sama. Tidak didukung untuk routing per kanal. |
| AudioHub Virtual (driver sendiri) | Stereo, buffer 192 | Ya | Ya, sebagian | Loopback 16,08 ms setelah perbaikan lag. Receive dan send lulus. Core Audio melaporkan buffer 192 dengan rentang 15–192; penyebab di HAL belum diketahui. |
| AudioHub Virtual 2 (driver sendiri) | Stereo | Belum (perlu pasang ulang) | Belum | Uji offline lulus. Belum diuji di mesin dengan driver terpasang. |
| EMEET Virtual Audio | Stereo | Ya | Ya, sebagian | Dipakai sebagai virtual cable pembanding saat driver sendiri belum ada. Tidak dipakai lagi untuk produk. |
| MacBook Air mic dan speaker | Built-in | Ya | Tidak | Tidak diuji. |

Aplikasi yang sudah diuji: `say` (sistem) lewat default output ke AudioHub Virtual. Aplikasi musik, panggilan, dan OBS belum diuji.

Yang belum tervalidasi: sleep/wake, cabut-pasang perangkat sungguhan, multi-device selain PS22 + AudioHub Virtual, sample rate selain 48 kHz, dan buffer di luar 192 dan 512.
