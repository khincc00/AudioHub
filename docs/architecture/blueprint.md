# AudioHub: Blueprint Arsitektur

Status: rancangan produk yang diusulkan, bukan fitur yang sudah terimplementasi. Angka dalam dokumen ini adalah target uji awal, bukan jaminan performa.

Dokumen ini adalah versi terrevisi dari blueprint awal, dengan tambahan dari review tahap pertama.

## 1. Visi dan batas cakupan

```text
Perangkat fisik ──┐
                  │
Virtual input ────┼──→ Routing engine ──→ Mixer / bus ──┬──→ Output fisik
                  │                                    │
Audio aplikasi ───┘                                    └──→ Virtual output
```

Target pengguna: kreator podcast dan streaming, musisi di luar DAW, pengguna multi-aplikasi, operator recording dan broadcast skala kecil.

Prinsip produk:

1. Tidak dikunci ke merek interface tertentu.
2. Routing per kanal, bukan sekadar memilih satu input dan output.
3. Monitoring terpisah dari audio yang dikirim ke aplikasi.
4. Engine tetap berjalan saat jendela UI ditutup.
5. Tidak memindahkan audio ke perangkat lain tanpa kebijakan yang jelas.
6. Kompatibilitas dibedakan antara terdeteksi dan tervalidasi.

Di luar cakupan awal: DAW lengkap, kontrol phantom power/preamp/DSP hardware, plugin hosting, audio jaringan, dan jaminan identik untuk semua kombinasi interface.

## 2. Model domain

| Entitas | Arti |
|---|---|
| Device | Perangkat fisik atau virtual |
| Port | Titik masuk/keluar audio milik sebuah node |
| Channel | Satu jalur audio mono |
| Node | Sumber, pemroses, mixer, atau tujuan audio |
| Route | Koneksi dari port sumber ke port tujuan |
| Bus | Jalur untuk menggabungkan beberapa sumber |
| Scene | Snapshot konfigurasi routing dan mixer |
| Device binding | Hubungan antara peran logis dan perangkat aktual |

Scene mengacu pada **peran logis** (mis. `Main Microphone`, `Monitor Output`), bukan nama perangkat. Jika perangkat tidak ditemukan, binding ditandai hilang dan pengguna mengganti secara eksplisit.

## 3. Arsitektur

```text
DESKTOP UI  ──IPC: commands/events──  CONTROL SERVICE
                                       (device registry, validasi graph,
                                        scene manager, engine lifecycle)
                                              │ prepared configuration
                                     REAL-TIME AUDIO ENGINE
                                       (buffers, graph, mixing, gain,
                                        metering, resampling)
                                  ┌───────────┴───────────┐
                        PHYSICAL BACKEND          VIRTUAL DEVICE
                        (Core Audio)              (cable eksternal → produk sendiri)
```

| Komponen | Tanggung jawab | Bukan tanggung jawab |
|---|---|---|
| Desktop UI | Editor graph, mixer, meter, konfigurasi | Memproses buffer audio |
| Control service | Validasi, perangkat, scene, konfigurasi | Pekerjaan lambat di callback audio |
| Audio engine | Pemrosesan dan timing | Menulis file atau menggambar UI |
| Device backend | Membuka perangkat, mengelola stream | Menentukan UX routing |
| Virtual device | Mengekspos endpoint ke aplikasi lain | Logika mixer |

**Keputusan awal (revisi):** untuk MVP, engine dan control service berjalan dalam satu proses. Pemisahan ke proses terpisah dengan XPC ditunda sampai ada alasan terukur (isolasi crash atau kebutuhan latensi).

**Background operation:** engine tetap berjalan saat UI ditutup melalui launch agent atau login item (`SMAppService`). Perilaku saat app di-quit harus didefinisikan eksplisit.

## 4. Engine

Alur pemrosesan:

```text
Capture → Channel mapping → Penyesuaian format/sample rate → Input gain
  → Routing graph → Bus mixing → Bus gain/mute → Output mapping → Playback/virtual endpoint
```

Node awal: `PhysicalInputNode`, `VirtualInputNode`, `GainNode`, `MixerBusNode`, `MeterNode`, `PhysicalOutputNode`, `VirtualOutputNode`.
Node lanjutan (tahap berikutnya): `DelayNode`, `EQNode`, `CompressorNode`, `LimiterNode`, `PluginNode`, `RecorderNode`.

Aturan implementasi:

- Buffer dialokasikan sebelum pemrosesan dimulai. Callback audio tidak mengalokasikan memori, tidak memakai lock yang bisa diblokir, dan tidak mengakses database atau filesystem.
- Pertukaran data antar-thread memakai ring buffer SPSC dengan kapasitas tetap.
- Perubahan routing disiapkan di luar callback, lalu diaktifkan pada batas pemrosesan.
- Perubahan gain memakai ramp, bukan lompatan mendadak.
- Telemetri berupa data ringkas. UI tidak membaca state internal engine secara langsung.
- **Satu sample rate untuk seluruh graph pada MVP** (mis. 48 kHz). Konversi hanya terjadi di batas perangkat.

Validasi graph: tanpa siklus. `Input → Bus → Output` dan `Input → Bus A → Bus B` diizinkan, `Bus A → Bus B → Bus A` ditolak. Larangan siklus internal tidak mencegah feedback melalui aplikasi di luar, sehingga UI wajib menunjukkan jalur monitor dan virtual routing dengan jelas.

## 5. Virtual routing

### Tahap 2 (prototipe): cable eksternal

Gunakan virtual cable yang sudah terpasang. Catat lisensi, jumlah kanal, dan nama perangkat yang dipakai, karena batas ini memengaruhi desain bus di Tahap 2. Nama endpoint di bawah bersifat ilustratif.

| Endpoint | Fungsi |
|---|---|
| AudioHub Playback | Tujuan output aplikasi yang masuk ke mixer |
| AudioHub Virtual Mic | Input virtual untuk aplikasi komunikasi |
| AudioHub Stream Mix | Input virtual untuk aplikasi streaming |
| AudioHub Record Mix | Input virtual untuk aplikasi recording |

### Tahap 5: virtual device produk

Opsi: AudioServerPlugIn (HAL plug-in). Perlu diperhitungkan:

- Plugin berjalan di dalam `coreaudiod`. Bug di sana memengaruhi seluruh audio mesin.
- Instalasi membutuhkan akses admin, notarisasi, dan restart service audio.
- Upgrade dan uninstall harus teruji sebelum rilis (lihat acceptance Tahap 5).

### Per-app capture (investigasi terpisah)

Pemisahan audio per aplikasi belum dianggap selesai. Jalur yang perlu diteliti: Core Audio process tap (`AudioHardwareCreateProcessTap`, sejak macOS 14.2 menurut catatan awal, perlu diverifikasi di dokumentasi Apple). Investigasi ini berdiri sendiri dan tidak menghambat Tahap 0–2.

## 6. Mixer dan bus

| Bus | Tujuan |
|---|---|
| Voice | Mic dan sumber suara utama |
| Desktop | Audio aplikasi yang diarahkan ke virtual input |
| Communication | Audio panggilan |
| Monitor | Audio yang didengar operator |
| Stream | Campuran untuk broadcast |
| Record | Campuran untuk recording |

Channel strip awal: nama dan warna, gain, mute, pan (untuk pasangan stereo), meter, indikator clipping, send ke bus, status sumber.

Mix-minus:

```text
Monitor:               Voice + Desktop + Communication
Stream:                Voice + Desktop + Communication
Virtual Mic (panggilan): Voice + Desktop
```

Communication tidak dikembalikan ke aplikasi panggilan.

Solo pada MVP hanya memengaruhi monitor. Mix stream dan record tidak berubah kecuali pengguna memilih mode lain.

## 7. Perangkat dan clock

Registry perangkat menyimpan: identifier, nama tampilan, jenis (physical/virtual), jumlah kanal input/output, format tersedia, format aktif, status koneksi, status stream, dan binding ke peran logis.

Dua mode:

- **Mode utama:** satu interface utama untuk I/O kritis, ditambah virtual routing. Ini konfigurasi pertama yang distabilkan.
- **Mode multi-device:** beberapa perangkat fisik dengan kebijakan clock, buffer, dan penyesuaian sample rate yang eksplisit. Status dan daftar konfigurasi tervalidasinya terpisah.

Clock drift dan strategi sinkronisasi dibahas di [clock-and-buffers.md](clock-and-buffers.md).

Kebijakan saat perangkat dicabut:

- Input yang hilang menjadi silence.
- Output yang hilang tidak pindah otomatis ke speaker lain.
- Route tetap tersimpan dengan status `unavailable`.
- Perangkat yang kembali dipasang diverifikasi identitasnya sebelum dipakai.
- Auto-reconnect hanya berlaku sesuai pilihan pengguna.

## 8. UI

- **Dashboard:** status engine, perangkat utama, sample rate dan buffer aktif, scene aktif, kanal/bus bermasalah, ringkasan gangguan, tombol stop.
- **Routing matrix:** tampilan default. Klik sel untuk koneksi atau level send.
- **Graph view:** tampilan alternatif. Matrix dan graph mengedit model routing yang sama.
- **Mixer:** channel strip dan bus strip.
- **Devices:** daftar perangkat, kanal, format aktif, dan device binding.
- **Scenes:** Podcast, Recording, Streaming, Online Meeting, Custom.
- **Diagnostics:** detail konfigurasi dan event, serta ekspor laporan yang dipicu pengguna. Sebelum ekspor, nama perangkat dan data identitas diredaksi sesuai pilihan pengguna.

## 9. Scene

Scene mencakup device bindings, node dan route, gain/mute/pan/send, nama kanal dan bus, konfigurasi virtual endpoint, dan pengaturan monitor. Skema ada di [scene.v1.schema.json](../../contracts/scene-schema/scene.v1.schema.json).

Alur load: validasi struktur dan versi → cocokkan binding → tampilkan perangkat yang hilang → siapkan graph baru → aktifkan → konfirmasi status.

Scene tidak pernah mengganti perangkat secara diam-diam. Perubahan yang butuh restart stream (mis. sample rate) harus dibedakan di UI dari perubahan yang bisa langsung diterapkan.

## 10. Stack

| Bagian | Pilihan awal |
|---|---|
| Desktop UI | SwiftUI, AppKit bila perlu |
| Audio engine | C++ (keputusan awal, bukan mutlak) |
| Integrasi audio | Core Audio |
| IPC | XPC atau mekanisme lokal dengan kontrak pesan |
| Virtual device | AudioServerPlugIn (Tahap 5) |
| Scene/konfigurasi | JSON versioned dengan migrasi |
| Pengujian DSP | Unit test dan offline audio fixtures |
| Distribusi | App bundle dan installer komponen virtual |

Jika memilih Rust, itu keputusan terpisah berdasarkan kemampuan tim. Jangan memakai C++ dan Rust sekaligus tanpa kebutuhan yang jelas.

## 11. Roadmap

| Tahap | Deliverable | Syarat lanjut |
|---|---|---|
| 0: Technical spike | Input fisik → engine → output fisik | Jalur audio stabil pada konfigurasi uji, clock dan buffer tercatat |
| 1: Routing engine | Gain, meter, bus, routing per kanal | Hasil routing dan mixing lolos tes |
| 2: Virtual routing | Audio antar-aplikasi lewat cable eksternal | Workflow kirim/terima terbukti |
| 3: Desktop MVP | Matrix, mixer, devices, scene | Workflow berjalan tanpa edit file |
| 4: Reliability | Reconnect, diagnosis, validasi scene, background operation | Skenario gagal berperilaku sesuai kebijakan |
| 5: Virtual device produk | Endpoint dan installer sendiri | Install, upgrade, uninstall teruji |
| 6: Professional release | Dokumentasi, compatibility matrix, endurance test | Klaim dukungan punya bukti |
| 7: Ekspansi | Multi-device lebih luas, DSP/plugin | Tidak merusak baseline stabilitas |

Tahap 0 dan 1 boleh gagal tanpa menghentikan proyek. Jika fondasi clock gagal, arsitektur ditinjau ulang sebelum lanjut. Tanggal rilis tidak ditetapkan sebelum prototipe membuktikan bagian yang paling berisiko.

## 12. Acceptance criteria

Kebenaran audio: pemetaan kanal sesuai konfigurasi, tidak ada kebocoran kanal ke tujuan yang tidak dipilih, mute dan gain sesuai tes, mix-minus tidak mengirim Communication kembali, dan perilaku input/output hilang sesuai kebijakan.

Kestabilan: continuous routing 8 jam pada konfigurasi tervalidasi, UI dibuka/ditutup/diubah ukurannya tanpa menghentikan engine, cabut/pasang perangkat selama uji, ganti scene berulang, sleep/wake dengan hasil pemulihan tercatat, dan gangguan audio dicatat, bukan disembunyikan.

Performa: ukur dan publikasikan per konfigurasi: model perangkat, versi macOS, sample rate, buffer size, jumlah kanal, jumlah bus, pemrosesan aktif, latensi end-to-end, dan jumlah gangguan audio. Jangan memakai satu angka latensi untuk mewakili semua konfigurasi.

Pengujian ini dijalankan terhadap profil perangkat yang nyata, lihat [maono-ps22.md](../compatibility/maono-ps22.md).

## 13. Risiko

| Risiko | Mitigasi |
|---|---|
| Multi-device tidak stabil | Rilis awal fokus pada satu interface utama |
| Drift clock antar-perangkat | Kebijakan clock eksplisit, uji endurance |
| Virtual driver memperlambat MVP | Mulai dengan cable eksternal |
| Driver `coreaudiod` bermasalah | Validasi di Tahap 2 sebelum membangun driver sendiri |
| UI mengganggu engine | Pisahkan rendering UI dari pemrosesan audio |
| Feedback | Graph tanpa siklus, jalur monitor jelas, peringatan |
| Perangkat berubah identitas | Device binding dan pemetaan ulang eksplisit |
| Preset lama rusak | Schema version dan migrasi |
| Scope terlalu besar | Tunda DAW, plugin hosting, network audio, kontrol hardware |
| Klaim kompatibilitas berlebihan | Pisahkan status terdeteksi dan tervalidasi |
| Izin macOS (mikrofon, capture audio) | Onboarding dan pesan izin yang jelas |

## 14. Keputusan MVP

- macOS desktop, satu interface utama, virtual cable eksternal.
- Routing matrix per kanal, bus Voice/Desktop/Communication/Monitor/Stream/Record.
- Gain, mute, pan, send, meter, mix-minus.
- Scene dan device binding.
- Engine terpisah dari UI, satu proses pada MVP.
- Tanpa plugin hosting atau kontrol hardware khusus.

Catatan verifikasi: alur di bawah mengasumsikan dua input fisik terpisah. Pada Maono ProStudio 2x2 (PS22), pemisahan ini hanya terjadi dalam **mode USB stereo**. Dalam mode mono, kedua kanal berisi sinyal yang sama. Sudah terverifikasi: Input 1 → kanal 1, Input 2 → kanal 2 pada mode stereo. Lihat [maono-ps22.md](../compatibility/maono-ps22.md).

Alur teknis pertama:

```text
Mic fisik + audio dari virtual cable → Dua input channel → Mix bus
  → Headphone (monitor)
  → Virtual output → Aplikasi penerima
```
