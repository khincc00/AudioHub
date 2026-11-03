# Cakupan Fitur: Routing Center PS22 vs AudioHub

Acuan: daftar fitur yang diberikan pengguna dari panduan PS22. Status per butir:

- **Ada**: sudah diimplementasikan dan diuji (uji unit atau harness pada perangkat).
- **Ada, perlu uji langsung**: diimplementasikan, tetapi belum diuji dalam skenario penuh.
- **Tidak berlaku**: fungsi khusus Windows atau hardware yang tidak ada di AudioHub.
- **Di luar cakupan**: tidak ada di blueprint saat ini.

## 1. Fitur software dan routing

| No | Fitur | Status | Catatan |
|---|---|---|---|
| 1 | Routing matrix visual | Ada | Matriks input × output, klik untuk menyambung. |
| 2 | Routing per kanal | Ada | Setiap koneksi adalah kanal mono. |
| 3 | Routing input hardware | Ada | Input 1 dan 2 dari PS22 (fisik). |
| 4 | Routing kanal virtual | Ada | Virtual In dari AudioHub Virtual 1 dan 2. |
| 5 | Menggabungkan beberapa sumber | Ada | Beberapa input ke satu output dijumlahkan. Jumlah sumber ditampilkan per output. Uji mix-minus pada perangkat lolos. |
| 6 | Routing ke headphone / main output | Ada | Output fisik 1 dan 2. |
| 7 | Routing ke virtual output | Ada | Output virtual, dipakai aplikasi lain. |
| 8 | Loopback routing | Ada | Virtual In ke output fisik. Diuji dengan tone. |
| 9 | Pemetaan mono / stereo | Ada, perlu uji langsung | Tautan stereo: dua input dikendalikan bersama (gain dan mute). Pan belum ada. |
| 10 | Indikator level output | Ada | Meter, peak hold, dan indikator clip. |
| 11 | Integrasi routing audio aplikasi | Ada, perlu uji langsung | Lewat AudioHub Virtual. `say` dan OBS sudah diuji. |
| 12 | Penggunaan untuk recording / streaming | Ada | Template Streaming dan Recording. OBS sudah dilaporkan berhasil. |

## 2. Daftar kanal audio

| Kanal PS22 | Padanan AudioHub | Status |
|---|---|---|
| MIC In 1 / MIC In 2 | Fisik In 1 / 2 | Ada |
| VC In 1/2, VC In 3/4 | Virtual In 1 sampai 4 (avhub_virtual1 dan avhub_virtual2) | Ada |
| Loopback 1/2 | Virtual In dari avhub_loopback (perangkat virtual ketiga) | Ada, perlu uji langsung. Belum diuji di mesin dengan driver terpasang |
| HP Out 1/2 | Fisik Out 1 / 2 | Ada |
| VC Out 1/2, VC Out 3/4 | Virtual Out | Ada |

## 3. ASIO dan pengaturan driver

| Fitur | Status | Catatan |
|---|---|---|
| Dukungan ASIO | Tidak berlaku | ASIO adalah standar Windows. macOS memakai Core Audio. |
| Pengaturan sample rate | Ada, terbatas | Saat ini 48 kHz. Pengaturan lain belum diuji. |
| Pengaturan buffer size | Di luar cakupan UI | Buffer tetap dari perangkat. Belum bisa diubah dari aplikasi saat berjalan. |
| Info input dan output latency | Ada, sebagian | Aplikasi menampilkan buffer dalam ms. Latensi terukur ditulis sebagai referensi dari tes loopback. |
| ASIO status | Tidak berlaku | Tidak ada ASIO di macOS. |

## 4. Monitoring dan OTG

| Fitur | Status | Catatan |
|---|---|---|
| Direct monitoring | Ada, sebagai fitur perangkat | Dilakukan oleh PS22, bukan AudioHub. Perlu diatur di panel perangkat. |
| Monitoring audio komputer | Ada | Lewat output fisik. |
| Campuran monitoring (knob MIX) | Ada, di perangkat | Mengatur monitoring langsung dan playback macOS. Belum diotomasi dari aplikasi. |
| Pemilihan sumber ke HP Out | Ada | Matriks. |
| OTG switch | Di luar cakupan | Tidak ada di blueprint. |
| Recording / streaming via smartphone | Di luar cakupan | |
| Monitoring playback smartphone | Di luar cakupan | |

## 5. Platform

| Aspek | Status |
|---|---|
| macOS | Didukung (target AudioHub). |
| Windows | Dalam pengembangan (folder `windows/`). Engine sama dengan macOS. Backend WASAPI dan UI WinUI 3 belum diuji di perangkat, dan driver virtual belum dibuat. Lihat [windows/README.md](../../windows/README.md). |
| Pemilihan output per aplikasi | Dilakukan di macOS atau aplikasi terkait. AudioHub menyediakan endpoint virtual. |

## 6. Fitur yang belum terkonfirmasi di PS22 dan tidak diklaim di AudioHub

| Fitur | Status di AudioHub |
|---|---|
| EQ, compressor, limiter | Ada (lihat bagian 7). |
| Noise reduction / gate | Noise gate ada (lihat bagian 7). Noise reduction berbasis AI tidak ada. |
| VST plugin hosting | Belum. Audio Units (AU) didukung sebagai gantinya (lihat bagian 7). VST3 perlu SDK Steinberg dan lisensinya dicek dulu. |
| Sub-mix bernama Stream / Chat | Tidak ada. Template Streaming dan Online Meeting memberi kombinasi serupa. |
| Mixer lengkap dengan pan dan solo | Sebagian. Gain dan mute ada. Pan dan solo belum ada. |
| Mix-minus otomatis | Ada sebagai routing, bukan tombol khusus. |
| Recorder internal | Di luar cakupan. |
| Multi-interface lintas merek | Di luar cakupan. |
| Audio jaringan, remote control | Di luar cakupan. |

## Ringkasan

- Ada: 12 dari 12 fitur routing dasar, dengan beberapa yang masih perlu uji langsung.
- Tidak berlaku: ASIO dan status ASIO (fungsi Windows).
- Di luar cakupan: EQ, dinamika, plugin, recorder internal, OTG, jaringan.

## 7. DSP dan plugin (ditambahkan kemudian)

| Fitur | Status | Catatan |
|---|---|---|
| Noise gate | Ada, per input | Threshold, attack, release, hold. Tanpa sidechain. |
| EQ 3 band | Ada, per input | Low shelf, peak (dengan Q), high shelf. Tanpa filter high-pass atau low-pass. |
| Kompresor | Ada, per input | Feed-forward, threshold, rasio, attack, release, makeup. Tanpa knee, sidechain, atau lookahead. |
| Limiter | Ada, per output | Batas dijaga per sampel tanpa lookahead, jadi transien sangat cepat bisa sedikit melewati batas. |
| Plugin Audio Unit (AU) | Ada, per input | Memuat efek AU terpasang (termasuk bawaan Apple). Parameter plugin belum ditampilkan dan belum ada GUI plugin. |
| Plugin VST3 | Belum | Perlu SDK Steinberg dan pengecekan lisensi sebelum dibangun. |
| Plugin VST2 | Tidak didukung | Sudah tidak didukung oleh Steinberg. |

Urutan proses per input: plugin AU → gate → EQ → kompresor → routing. Limiter ada di setiap output setelah mixing.

Verifikasi: 13 uji DSP (EQ peak +6 dB, shelf, kompresor −10,17 dB terhadap target −10,5 dB, limiter, gate, bypass), uji host AU dengan AUDynamicsProcessor (−9,0 dB menjadi −18,9 dB), dan harness aplikasi pada perangkat (27 cek, termasuk DSP aktif dengan 878 callback dan 0 dropout).

Yang belum diverifikasi: kualitas bunyi didengar langsung. Angka di atas dari pengukuran sinyal uji, belum penilaian telinga.
