# Clock dan Buffer (Tahap 0)

Dokumen ini menjadi spesifikasi sebelum membangun technical spike. Angka di sini adalah target awal yang harus diverifikasi dengan pengukuran, bukan nilai yang sudah terbukti.

## Pertanyaan yang harus dijawab Tahap 0

1. Clock mana yang menjadi master untuk satu konfigurasi perangkat?
2. Berapa buffer size yang dipakai, dan berapa latensi yang dihasilkan per tahap?
3. Bagaimana xrun (underrun/overrun) terdeteksi, dihitung, dan dilaporkan?
4. Apa yang terjadi saat sample rate perangkat tidak cocok dengan sample rate graph?

## Kebijakan MVP

- **Satu sample rate untuk seluruh graph** (target 48 kHz). Perangkat yang tidak mendukung rate ini ditandai tidak kompatibel untuk MVP.
- **Satu perangkat fisik sebagai clock master** pada mode utama. Virtual cable mengikuti clock sistem atau perangkat utama, bergantung pada pengukuran.
- **Mode multi-device ditunda** sampai mode utama stabil. Drift compensation hanya diuji di sana.
- Buffer size awal yang diuji: 64, 128, 256, dan 512 sampel. Nilai default ditetapkan dari hasil uji, bukan dari asumsi.

## Ring buffer antar-thread

- SPSC (single producer, single consumer) dengan kapasitas tetap.
- Kapasitas dihitung dari buffer size dan jumlah periode cadangan, bukan angka tetap.
- Callback audio hanya membaca dan menulis; tidak ada alokasi, lock, atau syscall.

## Pencatatan xrun

Setiap kejadian dicatat dengan:

```text
timestamp        waktu monotonic
perangkat        identifier perangkat
arah             input / output / internal
jenis            underrun / overrun / drop
durasi           jumlah sampel yang hilang
```

Jumlah xrun ditampilkan di dashboard. Gangguan tidak disembunyikan dari pengguna.

## Pengukuran latensi

Latensi dilaporkan per konfigurasi, tidak sebagai satu angka umum:

```text
latensi end-to-end = latensi input perangkat + buffer engine + latensi output perangkat
```

Metode pengukuran (mis. loopback dengan klik uji) dicatat di `tests/integration/` bersama konfigurasi yang dipakai.

## Kriteria lulus Tahap 0

- Jalur input → engine → output stabil selama minimal 30 menit pada satu konfigurasi.
- Nol xrun pada buffer size target untuk konfigurasi tersebut, atau xrun tercatat dan dijelaskan.
- Latensi end-to-end terukur dan tercatat.
- Hasil pengukuran tersimpan dengan model perangkat, versi macOS, dan sample rate.
