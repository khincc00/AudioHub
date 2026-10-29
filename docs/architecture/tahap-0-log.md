# Log Tahap 0: Technical Spike

Spike: `backends/coreaudio/spike/passthrough.cpp`. Input 1 dikirim ke output 1, input 2 ke output 2, satu IOProc duplex, gain 0 dB, buffer default 512 frame.

Dropout dicatat dari kontinuitas mSampleTime. Ini proksi untuk xrun, bukan pengukuran langsung dari sistem audio.

Perangkat: Maono ProStudio 2x2 (PS22), mode stereo, mix penuh, headphone di jack PS22, kabel loopback dilepas.

## Hasil

| Tes | Durasi | Callback | Dropout | Frame hilang | Catatan |
|---|---|---|---|---|---|
| Passthrough 1 | 60 detik | 5.647 (sesuai perkiraan) | 0 | 0 | Input 1 mengikuti output 1 secara level |
| Passthrough 2 (endurance) | 30 menit (1.800 detik) | 169.837 (sesuai perkiraan) | 0 | 0 | Input ambient; 1 detik mencapai 0 dBFS (t=69 s), 4 detik lain di atas −10 dBFS |

## Kriteria Tahap 0 (dari blueprint)

- [x] Jalur input → engine → output stabil minimal 30 menit pada satu konfigurasi (30 menit, 0 dropout)
- [x] Latensi end-to-end terukur (29,3 ms pada buffer 512; 13,3 ms pada 128)
- [x] Hasil pengukuran tersimpan dengan model perangkat, versi macOS, dan sample rate (lihat [maono-ps22.md](../compatibility/maono-ps22.md))
- [x] Nol xrun pada buffer target, atau xrun tercatat dan dijelaskan (0 dropout pada 60 detik dan 30 menit, buffer 512)

## Catatan tes 30 menit

- Tidak ada dropout dan tidak ada frame hilang selama 1.800 detik. Jumlah callback per detik berkisar 93–95, sesuai buffer 512.
- Pada t=69 detik, input 1 mencapai 0 dBFS. Itu berarti sinyal sempat mencapai batas penuh, kemungkinan karena suara mendadak di dekat mic. Ini tidak tercatat sebagai dropout, tetapi clipping pada input bisa terjadi. Perlu diuji ulang dengan sumber suara yang terkontrol.
- Dropout diukur sebagai proksi dari kontinuitas mSampleTime. Ini tidak mengukur xrun di level sistem secara langsung, jadi hasilnya perlu dikonfirmasi dengan Audio HAL atau Instruments jika dibutuhkan untuk klaim publik.
- Tes ini belum mencakup sleep/wake, cabut-pasang perangkat, atau ganti scene. Itu bagian dari tahap berikutnya.
