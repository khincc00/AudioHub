# Log Tahap 1: Routing Engine

Status: **selesai dengan catatan**. Mix-minus penuh pada perangkat ditunda sampai ada sumber suara ketiga yang terkontrol.

Kode: `engine/graph/graph.h`, `engine/graph/graph.cpp`. Uji: `tests/unit/test_graph.cpp`, dijalankan dengan `sh tests/unit/run.sh`.

## Yang sudah dikerjakan

- Node channel, bus, dan output. Sumber harus channel atau bus, tujuan harus bus atau output. Kombinasi lain ditolak saat `connect`.
- Gain dan mute per node (channel dan bus), dengan ramp linear per blok agar tidak ada lompatan.
- Send per route (dB), bisa diaktifkan atau dimatikan dengan ramp yang sama.
- Urutan bus dengan algoritma Kahn. Siklus antar-bus ditolak di `prepare()` dengan pesan error.
- Meter peak dan RMS per node setelah setiap blok.
- Buffer dialokasikan di `prepare()`. `process()` tidak mengalokasikan memori.

## Hasil uji

| Uji | Hasil |
|---|---|
| Gain channel −6 dB | Lulus (0,5) |
| Mute | Lulus (nol) |
| Ramp mute | Lulus, linear dan monoton |
| Send −20 dB | Lulus (0,1) |
| Route nonaktif | Lulus (nol) |
| Rantai dua bus | Lulus (0,25) |
| Gain bus | Lulus (0,5) |
| Mix-minus: monitor dan virtual mic | Lulus. Virtual mic tidak berubah saat Communication berubah |
| Siklus A → B → A | Ditolak |
| Koneksi tidak valid | Ditolak |
| Meter peak dan RMS | Lulus (0,8) |

Total: 20 cek, 0 gagal. Dikompilasi dengan `-Wall -Wextra -Werror`.

## Yang belum ada

- Pan untuk pasangan stereo. Belum diimplementasikan di engine.
- Solo yang hanya memengaruhi monitor. Belum diimplementasikan.
- Ramp per sampel. Saat ini ramp linear per blok, yang cukup untuk perubahan kecil tetapi tidak sehalus smoothing per sampel untuk perubahan besar.
- Thread-safety antara UI dan engine (ring buffer SPSC dan pertukaran perubahan di batas blok). Belum dihubungkan ke CoreAudio.
- Integrasi ke spike `passthrough` dan uji loopback dengan PS22 menggunakan graph ini.

## Integrasi ke PS22 (spike engine)

Spike: `backends/coreaudio/spike/passthrough_engine.cpp`. Deinterleave dan interleave dilakukan di luar alokasi callback. Meter diambil dari node engine, dan peak input mentah diukur di spike sebelum masuk graph.

### Gain pada perangkat

| Tes | Durasi | Callback | Dropout | Frame hilang | Hasil |
|---|---|---|---|---|---|
| Gain input 1 −6 dB | 10 detik | 941 | 0 | 0 | Selisih raw dan post-gain = **−6,00 dB** pada setiap detik |

Level raw berubah antara −42 dan −50 dBFS karena ambient, tetapi selisihnya tetap konstan. Ini membuktikan gain node bekerja pada data perangkat nyata.

### Routing dan mixing pada perangkat

| Tes | Durasi | Callback | Dropout | Frame hilang | Hasil |
|---|---|---|---|---|---|
| Input 1 → bus A (0 dB) → output 1; input 1 → bus B (−6 dB) → output 2 | 10 detik | 940 | 0 | 0 | Output 1 − output 2 = **+6,00 dB** pada setiap detik |
| Input 1 → output 1, input 2 → output 2 (mode default) | 10 detik | 941 | 0 | 0 | Output 2 tetap diam saat input 1 aktif, tanpa kebocoran |

Catatan:

- Mix-minus (monitor berisi Communication, virtual mic tidak) baru diuji di unit test. Di perangkat, bus dan route sudah terbukti, tetapi tiga sumber suara yang terkontrol belum tersedia untuk uji mix-minus penuh.
- Uji dilakukan dengan input ambient mic, sehingga sinyal berubah-ubah. Itu justru membuat tes lebih realistis, karena selisih yang diukur tetap konstan.
- Loopback kabel dengan spike ini berbahaya: output 1 → input 1 membentuk loop. Jangan pasang kabel loopback saat spike berjalan.

## Kriteria Tahap 1 (dari blueprint)

- [x] Hasil routing dan mixing lolos tes unit (20 cek).
- [x] Routing per kanal dan mixing terverifikasi pada perangkat untuk gain, route, dan bus (selisih tepat 6,00 dB).
- [x] Mix-minus penuh pada perangkat, dengan tiga sumber suara terkontrol (lihat di bawah).

### Mix-minus pada perangkat (tiga sumber lewat AudioHub Virtual)

Voice = PS22 input 1 (ambient mic), Desktop = AudioHub in 1, Communication = AudioHub in 2. Monitor = Voice + Desktop + Communication. Virtual mic = Voice + Desktop, diukur di PS22 output 2.

| Fase | Sumber aktif | Monitor (PS22 out 1) | Virtual mic (PS22 out 2) |
|---|---|---|---|
| 1 | Communication saja (880 Hz) | naik ke sekitar −28,5 dBFS | tetap di level ambient (−42 sampai −46 dBFS) |
| 2 | Desktop saja (440 Hz) | naik ke sekitar −28 dBFS | naik ke sekitar −28 dBFS |

Communication masuk ke monitor dan tidak masuk ke virtual mic, sesuai rancangan. Dropout 0 pada kedua fase.
