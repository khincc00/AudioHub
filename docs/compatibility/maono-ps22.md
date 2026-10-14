# Profil Perangkat: Maono PS22

Status: **sebagian terverifikasi**. Identitas, kanal, dan pemisahan input sudah diuji di perangkat tersambung. Kolom yang masih `TODO` perlu diisi dari dokumentasi resmi atau pengukuran.

Perangkat ini dipakai sebagai interface fisik referensi untuk Tahap 0. AudioHub tidak boleh bergantung pada merek ini; semua logika harus lewat peran logis dan binding.

## Identitas

| Kolom | Nilai |
|---|---|
| Merek / model | Maono PS22 (terdeteksi di macOS sebagai "Maono ProStudio 2x2", perlu dikonfirmasi bahwa keduanya perangkat yang sama) |
| Nama tampilan di macOS | Maono ProStudio 2x2 |
| UID Core Audio | `AppleUSBAudioEngine:Maono ProStudio 2x2:Maono ProStudio 2x2:100000:1,2` |
| AudioObjectID (sesi ini) | 106 (berubah antar-sesi, jangan dipakai sebagai identifier) |
| Koneksi | USB |
| Driver | TODO (class-compliant atau driver khusus) |
| Sample rate saat ini | 48 kHz |
| Sebagai output sistem | Ya, saat ini default system output |
| Mode USB yang diuji | Stereo (saklar di panel perangkat, gain di posisi tengah) |

## Kanal

| Arah | Jumlah kanal | Pemetaan terverifikasi |
|---|---|---|
| Input | 2 | Input 1 → kanal 1, Input 2 → kanal 2 (mode stereo) |
| Output | 2 | Stereo, saat ini default system output |

Pemetaan kanal awal, sekarang sudah terverifikasi untuk input:

```text
Input 1  → kanal 1 → Main Microphone (peran logis)
Input 2  → kanal 2 → peran logis lain (mis. sumber kedua)
Output 1–2 → Monitor Output (peran logis), belum diuji
```

## Hasil probe

Probe dijalankan dengan `tests/integration/probe/` (`devices.swift` dan `capture.swift`). Rekaman hanya membaca input dan tidak memutar audio, jadi tidak ada risiko feedback.

### Properti perangkat

| Parameter | Nilai |
|---|---|
| Sample rate nominal | 48 000 Hz |
| Buffer saat ini | 512 frame (~10,7 ms) |
| Rentang buffer yang didukung | 15–4096 frame |
| Latensi input perangkat | 14 frame (~0,29 ms) |
| Latensi output perangkat | 14 frame (~0,29 ms) |

Latensi di atas berasal dari properti perangkat, belum latensi end-to-end. Latensi end-to-end membutuhkan pengukuran loopback.

### Pemisahan kanal input

Semua tes memakai Core Audio langsung, 6 detik per rekaman.

| Tes | Mode USB | Mic di | Kanal 1 | Kanal 2 | Sampel identik |
|---|---|---|---|---|---|
| A | Mono | Input 1 | −37 sampai −43 dBFS | −37 sampai −43 dBFS (sama) | 100% |
| B | Mono | Input 2 | −45 sampai −53 dBFS | −45 sampai −53 dBFS (sama) | 100% |
| C | Stereo | Input 2 | −inf (diam) | −26 sampai −33 dBFS | 0% (3 dari 287.744, hanya nol) |
| D | Stereo | Input 1 | −27 sampai −37 dBFS | −inf (diam) | 0% (3 dari 288.256, hanya nol) |

Kesimpulan:

- Di **mode mono**, kedua kanal berisi sinyal yang sama. Pemisahan input tidak tersedia di mode ini.
- Di **mode stereo**, setiap input terpisah ke kanalnya sendiri. Pemetaan Input 1 → kanal 1 dan Input 2 → kanal 2 sudah terbukti.
- Rekaman ambient sebelumnya (mode mono, tanpa sinyal) juga identik di kedua kanal, konsisten dengan temuan ini.

Implikasi untuk AudioHub: **mode stereo wajib** untuk profil ini, dan scene harus memakai kanal terpisah. Kebijakan untuk mode mono perlu diputuskan: menolak konfigurasi atau menandainya sebagai input tunggal.

### Tes output ke headphone (`tests/integration/probe/playtone`)

Tone 440 Hz, −30 dBFS, 2 detik di output 1, diam 1 detik, 2 detik di output 2. Dijalankan dengan headphone di jack PS22, gain output diatur di tengah.

| Output | Hasil |
|---|---|
| 1 | Terdengar setelah gain output dinaikkan ke tengah (sebelumnya sangat kecil) |
| 2 | Terdengar |

Tes pemisahan telinga (`tests/integration/probe/playdual`): 440 Hz di output 1 dan 880 Hz di output 2 diputar bersamaan. Hasil: **kedua nada terdengar di kedua telinga**.

Kesimpulan sementara: jalur headphone di PS22 **tidak memisahkan output 1 dan 2** ke telinga kiri dan kanan. Kemungkinan penyebabnya:

- Jack headphone menerima mix mono dari kedua output.
- Ada mode mix atau mono pada perangkat yang memengaruhi headphone.
- Headphone atau adaptor yang dipakai menjumlahkan kanal.

Ini hanya didengar, belum diukur. Penentuan pasti perlu tes dengan headphone lain, atau pemeriksaan manual perangkat.

Dampak untuk AudioHub: monitoring lewat jack headphone PS22 tidak bisa diasumsikan stereo sesuai routing di macOS. Monitor path harus diverifikasi terpisah sebelum dipakai untuk pekerjaan mix.

### Monitoring langsung dan kontrol mix

Pengamatan dari panel perangkat:

- Input 1 terdengar di **telinga kiri**, Input 2 terdengar di **telinga kanan**. Ini pemetaan stereo input di monitoring langsung.
- Knob **mix** di PS22 mengatur keseimbangan antara monitoring langsung dan playback dari macOS:
  - Mix **rendah**: hanya terdengar input 1 dan 2 (monitoring langsung).
  - Mix **tengah**: normal, campuran keduanya.
  - Mix **penuh**: hanya terdengar playback dari macOS, tanpa monitoring langsung.

Akibatnya, tes output sebelumnya (440 Hz dan 880 Hz di kedua telinga) mungkin dipengaruhi posisi mix, karena posisi mix tidak dicatat saat tes berlangsung. Tes itu perlu diulang dengan mix penuh, agar hanya playback macOS yang terdengar.

Risiko untuk AudioHub: jika monitoring langsung di perangkat aktif bersamaan dengan monitor dari macOS, suara mic bisa terdengar dua kali dengan jeda, atau menimbulkan efek comb filter. Scene harus memberi tahu pengguna sumber monitor mana yang aktif.

Latensi end-to-end belum bisa diukur dari tes ini. Pengukuran yang sah membutuhkan loopback elektrik (kabel dari output ke input) atau loopback di dalam engine. Loopback akustik lewat mic akan memasukkan jalur udara dan tidak bisa dipakai sebagai angka latensi.

## Format yang didukung

| Parameter | Nilai yang didukung | Nilai yang diuji |
|---|---|---|
| Sample rate | TODO | 48 kHz (target MVP) |
| Bit depth | TODO | TODO (probe mengasumsikan float32 dan memeriksa ukuran buffer) |
| Buffer size | TODO | 512 (default saat ini); 64, 128, 256 belum diuji |

## Status kompatibilitas

Dua status dibedakan sesuai prinsip produk:

- **Terdeteksi:** perangkat muncul di Core Audio, kanalnya terbaca, dan pemisahan input terverifikasi pada mode stereo. Status: **ya**.
- **Tervalidasi:** lulus kriteria Tahap 0 pada konfigurasi tertentu, termasuk loopback output dan latensi end-to-end. Status: **belum**.

| Konfigurasi | Versi macOS | Sample rate | Buffer | Status | Tanggal uji |
|---|---|---|---|---|---|
| Mode stereo, input 1 dan 2 terpisah | TODO | 48 kHz | 512 | Terdeteksi, input terverifikasi | TODO |
| Mode stereo, output 1 dan 2 terdengar di headphone | TODO | 48 kHz | — | Output terdengar, latensi belum diukur | TODO |

## Catatan saat perangkat dicabut

Sesuai kebijakan AudioHub: input menjadi silence, output tidak pindah ke speaker lain, route berstatus `unavailable`, dan identitas diverifikasi saat perangkat dipasang kembali. Perilaku ini perlu diuji langsung dengan mencabut PS22 selama sesi.

## Hal yang perlu dikonfirmasi

- Apakah PS22 memiliki mixer atau DSP internal. Jika ada, AudioHub tidak mengontrolnya pada MVP, dan monitoring hardware bisa menyebabkan kebingungan jalur monitor.
- Apakah monitoring hardware PS22 aktif sebelum sinyal masuk ke komputer. Ini memengaruhi strategi feedback dan mix-minus.
- Apakah "Maono ProStudio 2x2" sama dengan PS22 yang dimaksud, karena nama di macOS berbeda dari nama model.
- Kanal output: apakah output 1–2 hardware sama dengan output stereo yang terlihat di macOS, sebelum loopback diuji.

## Catatan tes dengar (tidak konklusif)

Tes `playdual` dengan nada bergantian (440 Hz dan 880 Hz, output 1 lalu output 2, mix penuh):

- Nada tinggi pada output 1 terdengar lebih jelas di telinga kiri.
- Nada rendah pada output 1 terdengar lebih jelas di telinga kanan.

Pola ini tidak sesuai dengan pemisahan kiri–kanan yang bersih, dan tidak bisa dibedakan dari asimetri pendengaran atau kebocoran kecil antar-kanal. Tes dengar tidak cukup untuk menentukan pemetaan output. Pemeriksaan objektif membutuhkan loopback elektrik: kabel dari output 1 dan 2 ke input 1 dan 2, lalu rekam dengan `capture`. Pengukuran itu juga bisa menghasilkan latensi end-to-end.

## Hasil tes dengar satu output (nada 880 Hz)

Tes memakai `tests/integration/probe/playtone` dengan satu output aktif dan output lainnya diam, mix penuh.

- **Output 1:** terdengar jelas di telinga kiri. Bocor sedikit ke kanan, diperkirakan sekitar 20% dari level kiri (perkiraan subjektif).
- **Output 2:** diputar setelah itu untuk pembanding. Hasilnya belum dicatat dari pendengaran.

Kesimpulan sementara: pemisahan kiri–kanan pada output 1 cukup jelas. Angka 20% belum terukur, dan sebagian bisa dipengaruhi pendengaran atau headphone. Pengukuran objektif tetap butuh loopback elektrik.

### Pemetaan telinga output (tes dengar, mix penuh)

| Output | Telinga utama | Bocor ke sisi lain |
|---|---|---|
| 1 | Kiri | Sekitar 20% (perkiraan subjektif) |
| 2 | Kanan | Sekitar 20% (perkiraan subjektif, sisi kiri berdasarkan kebalikan dari output 1) |

Hasil ini mengoreksi tes `playdual` sebelumnya, yang tampak tidak memisahkan kanal. Tes sebelumnya dilakukan dengan nada bersamaan dan posisi mix yang tidak dicatat, jadi lebih sulit dibaca. Tes dengar satu output per waktu lebih jelas.

Pemetaan ini masih perlu diverifikasi secara objektif dengan loopback elektrik untuk memastikan angka kebocoran dan latensi.

## Loopback lewat virtual cable (EMEET Virtual Audio)

Tes ini memakai `EMEET Virtual Audio` sebagai jalur virtual, bukan kabel fisik. Tone 880 Hz diputar ke **output kanal 1** selama 3 detik, lalu direkam dari input perangkat yang sama dengan `capture`.

| Waktu | Kanal 1 | Kanal 2 |
|---|---|---|
| 0–3 detik | −33 dBFS (stabil) | −inf |
| 3–5 detik | −inf (diam) | −inf |

Hasil:

- Output kanal 1 kembali di **input kanal 1**, dan kanal 2 tidak menerima sinyal. Pemisahan kanal berjalan di jalur virtual ini.
- Level −33 dBFS sesuai RMS dari tone −30 dBFS puncak (sinus: RMS = puncak − 3 dB), jadi tidak ada perubahan level dari loopback.
- Angka "identik 40%" pada output `capture` berasal dari dua kanal yang sama-sama nol di 2 detik terakhir, bukan dari duplikasi sinyal.

Yang belum diukur di tes ini: latensi. Pengukuran per sampel diperlukan, misalnya dengan klik dan deteksi onset.

Catatan: ini membuktikan jalur virtual EMEET, bukan PS22. Untuk PS22, loopback fisik atau virtual pada perangkat itu sendiri masih perlu dilakukan.

## Loopback kabel fisik (output 1 → input 1, output 2 → input 2)

Tes memakai `tests/integration/probe/loopback` dengan burst 1 kHz selama 10 ms, −20 dBFS, pada satu IOProc. Gain input diset normal (tidak clipping). Buffer 512 frame, 48 kHz.

| Jalur | Puncak input | Latensi | Kebocoran ke kanal lain |
|---|---|---|---|
| Output 1 → Input 1 | −9,2 dBFS | 1406 sampel (29,29 ms) | −63,9 dBFS (isolasi ±55 dB) |
| Output 2 → Input 2 | −5,7 dBFS | 1406 sampel (29,29 ms) | −69,8 dBFS (isolasi ±64 dB) |

Catatan:

- Latensi end-to-end sekitar **29,3 ms** dengan buffer 512 frame, dan sama di kedua kanal.
- Isolasi dihitung dari selisih puncak kanal sendiri dan kanal lain di jendela yang sama. Ini pengukuran yang sah karena gain tidak clipping.
- Gain input 1 dan 2 tidak identik (selisih sekitar 3,5 dB), jadi level puncak keduanya berbeda walau sinyal output sama.
- Latensi per buffer ukuran lain (mis. 128 atau 256) belum diukur.

### Latensi per ukuran buffer (loopback, mode stereo)

| Buffer | Latensi loopback | Keterangan |
|---|---|---|
| 512 frame | 29,29 ms | Default perangkat, dikembalikan setelah tes |
| 256 frame | 18,62 ms | Diukur |
| 128 frame | 13,29 ms | Diukur |

Hubungan ini linear: latensi ≈ 2 × buffer ÷ 48 000 + ±8 ms (overhead tetap perangkat dan USB). Setiap penurunan buffer menghemat sekitar 5,3 ms per 128 frame. Dengan buffer 128, loopback tetap bersih: tidak ada clipping, dan kebocoran antar-kanal tetap sekitar −65 dBFS.

Buffer 64 frame belum diuji. Catatan: buffer lebih kecil meningkatkan risiko xrun, jadi stabilitas perlu diuji lebih lama sebelum angka ini dipakai.
