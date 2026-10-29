# Brief Video Iklan Animasi: AudioHub (2 video)

Status: **draf untuk ditinjau. Belum ada render.**
Tanggal: 19 Oktober 2026
Versi aplikasi yang difilmkan: AudioHub 0.3.0 (build 3)
Tujuan tayang: demo. Paket sudah dinotarisasi dan dianggap prototipe yang final.
Situs resmi: https://audiohub.khincreator.com
Tagline di situs: "Routing audio profesional untuk macOS."

## 1. Ringkasan

Dua video animasi 2D, masing-masing tepat 60 detik:

| Video | Judul | Isi |
|---|---|---|
| 1 | Perkenalan | Apa itu AudioHub, cara pemasangan, dan setting perangkat |
| 2 | Fitur | Slider, routing matriks, efek dan limiter, serta preset dan template |

Kedua video memakai gaya visual yang sama dan bisa ditonton terpisah.

## 2. Tujuan dan pesan utama

- **Video 1:** penonton mengerti apa itu AudioHub dan bisa memasang serta menyiapkan perangkat sendiri.
- **Video 2:** penonton tahu cara memakai kontrol utama dan terdorong mengunduh aplikasi.
- **Pesan utama keseluruhan:** "Atur semua jalur audio Mac dari satu jendela."

## 3. Audiens

Kreator konten, podcaster, dan pengguna Mac yang sering memakai mikrofon, aplikasi musik, dan aplikasi panggilan bersamaan. Mereka cukup paham audio, tetapi belum tentu paham Core Audio.

**Written for:** animator atau produser video yang akan mengerjakan storyboard dan animasi.

## 4. Format teknis

| Item | Usulan |
|---|---|
| Durasi | 60 detik per video, tepat |
| Rasio dan resolusi | 16:9, 1920 × 1080 px, 30 fps |
| Bahasa | Bahasa Indonesia. Teks layar dalam Bahasa Indonesia. Tanpa subtitle Inggris |
| Narasi | Untuk sementara teks saja. Narasi akan direkam sendiri oleh pemilik proyek. Naskah narasi di bagian 6 dan 7 dipakai sebagai skrip rekaman, tanpa TTS |
| Musik | Instrumental elektronik ringan, bebas royalti, tanpa vokal, volume di bawah narasi |
| Gaya visual | Animasi motion graphics 2D, flat, dengan elemen UI asli dari aplikasi |
| CTA | https://audiohub.khincreator.com (tombol "Unduh untuk macOS (.dmg)" mengarah ke `/downloads/AudioHub-0.3.0.dmg`, 1,7 MB) |
| Aset dari situs | Logo: `https://audiohub.khincreator.com/audiohub/img/logo.png`. Screenshot tema gelap: `/audiohub/img/i-dark.png`. Screenshot tema terang: `/audiohub/img/i-light.png` |

## 5. Identitas visual

Diambil dari palet di [apps/macos/AudioHub/Sources/Model.swift](apps/macos/AudioHub/Sources/Model.swift):

- **Aksen (teal):** `#4DD1BD`. Dipakai untuk status aktif, koneksi routing, dan fader.
- **Peringatan (amber):** `#FAB84D`. Dipakai untuk meter di atas −12 dB dan tombol solo.
- **Bahaya (merah):** `#F25959`. Dipakai untuk clip, mute, dan tombol berhenti.
- **Latar:** abu-abu gelap netral, misalnya `#1C1C1E`, dengan teks putih. Tampilan terang tidak perlu ditampilkan.
- **Font:** SF Pro, agar cocok dengan tampilan macOS.
- **Ikon:** waveform (ikon SF Symbol `waveform`), sama dengan logo di bilah atas aplikasi.

## 6. Video 1: Perkenalan (60 detik)

### Scene 1. Pembuka (0:00–0:08)
- **Visual:** latar gelap. Tiga garis gelombang berwarna (mikrofon, musik, aplikasi panggilan) bergerak kacau dan saling bertabrakan.
- **Teks layar:** "Mikrofon, musik, dan aplikasi panggilan. Semuanya ingin masuk ke satu tempat."
- **Narasi:** "Mikrofon, musik, dan aplikasi panggilan, semuanya perlu jalurnya sendiri."

### Scene 2. Perkenalan AudioHub (0:08–0:18)
- **Visual:** garis-garis tadi tertata rapi dan menyatu menjadi ikon waveform. Nama "AudioHub" muncul, lalu jendela aplikasi masuk dengan animasi scale-in.
- **Teks layar:** "AudioHub. Routing audio profesional untuk macOS." Di bawahnya, keterangan kecil: "macOS 13 atau lebih baru · Apple Silicon dan Intel".
- **Narasi:** "Kenalkan AudioHub, pengatur routing audio profesional untuk Mac."

### Scene 3. Pemasangan (0:18–0:32)
- **Visual:** jendela DMG terbuka, lalu ikon "Install AudioHub.pkg" diklik. Muncul dialog kata sandi administrator (gambar dibuat ulang, bukan tangkapan layar asli). Ikon driver "AVHub Driver" lalu muncul di daftar perangkat audio.
- **Opsional:** sisipkan satu frame Terminal yang menampilkan `avhub_virtual1` dan `avhub_virtual2`.
- **Teks layar:** "Pasang sekali. Driver AVHub ikut terpasang."
- **Narasi:** "Buka DMG, jalankan installer, lalu masukkan kata sandi administrator. Driver AVHub ikut terpasang."

### Scene 4. Setting perangkat (0:32–0:50)
- **Visual:** sorot bilah atas, dari kiri ke kanan:
  1. Pilih perangkat fisik di menu "Perangkat".
  2. Buka menu Virtual dan pilih Virtual 1 dan Virtual 2 (`avhub_virtual1`, `avhub_virtual2`).
  3. Tekan **Buka**.
  4. Tekan **Mulai**. Status berubah menjadi "Berjalan" dengan titik teal.
- **Ikon roda gigi:** buka menu, tunjukkan sample rate 48,0 kHz, buffer 512 frame, dan pilihan "Sambung ulang otomatis".
- **Teks layar:** "Pilih perangkat. Buka. Mulai."
- **Narasi:** "Pilih perangkat fisik dan perangkat virtual, lalu tekan Buka dan Mulai. Sample rate dan buffer diatur dari menu pengaturan."

### Scene 5. Penutup (0:50–1:00)
- **Visual:** logo waveform dan nama AudioHub. Di bawahnya ada keterangan "Versi 0.3.0 · macOS 13 atau lebih baru".
- **Teks layar:** "AudioHub. Routing audio profesional untuk macOS." Lalu CTA: "audiohub.khincreator.com".
- **Keterangan kecil:** "Hak Cipta © 2026 taufiq sholikhin. Seluruh hak dilindungi." (dari footer situs)
- **Narasi:** "AudioHub. Atur audio Mac Anda dengan lebih mudah."

## 7. Video 2: Fitur (60 detik)

### Scene 1. Pembuka (0:00–0:06)
- **Visual:** jendela utama muncul dengan tiga kolom (Input, Routing, Samping) dan bar Output di bawah. Setiap bagian disorot bergantian.
- **Teks layar:** "Satu jendela untuk semua kanal."
- **Narasi:** "Semua input dan output ada dalam satu jendela."

### Scene 2. Slider (0:06–0:18)
- **Visual:** close-up satu kanal input, dengan animasi bergantian:
  1. **Gain:** slider bergerak dari −60 ke +12 dB. Angka dB berubah dan meter horizontal ikut bergerak.
  2. **Pan:** slider bergerak dari L ke C ke R. Label berubah sesuai posisi.
  3. **M dan S:** tombol bisu dan solo diklik. Solo hanya memengaruhi monitor.
- **Output fader:** fader vertikal di strip output bergerak. Tombol **0 dB** mengembalikan nilai ke 0.
- **Teks layar:** "Gain. Pan. Mute. Solo."
- **Narasi:** "Geser gain untuk mengatur level tiap input. Pan menempatkan suara di kiri atau kanan. Bisukan kanal dengan satu klik."

### Scene 3. Routing matriks (0:18–0:30)
- **Visual:** kamera mengarah ke matriks. Klik sebuah sel dan sel itu menyala teal. Klik kanan menampilkan menu "Send 0 dB / −6 dB / −12 dB". Lalu tombol Graph ditekan dan garis koneksi tampil sebagai kurva.
- **Catatan:** Graph hanya menampilkan. Koneksi diubah di tampilan Matriks, jadi visual Graph jangan dibuat seolah bisa diedit.
- **Teks layar:** "Klik untuk menghubungkan. Klik kanan untuk mengatur send."
- **Narasi:** "Klik kotak persimpangan untuk menghubungkan input ke output. Klik kanan untuk mengatur send."

### Scene 4. Efek dan limiter (0:30–0:42)
- **Visual:**
  1. Ikon slider di kanal input dibuka. Popover menampilkan Gate, EQ tiga pita, Kompresor, dan Delay satu per satu. Ikon berubah teal saat ada efek aktif.
  2. Di strip output, tombol "Limiter" dibuka. Slider Ceiling dan Lookahead bergerak.
  3. Lampu clip menyala merah saat level mencapai batas, lalu diklik untuk menghapus.
- **Catatan:** jangan tampilkan pengaturan plugin Audio Unit. Situs menyebut parameternya belum bisa diatur dari aplikasi.
- **Teks layar:** "Gate, EQ, kompresor, delay. Limiter untuk output."
- **Narasi:** "Setiap input punya gate, EQ, kompresor, dan delay. Setiap output punya limiter untuk menjaga level tetap di bawah batas."

### Scene 5. Preset dan template (0:42–0:52)
- **Visual:** panel samping. Dropdown preset, tombol Simpan, lalu empat tombol template (Podcast, Streaming, Online Meeting, Recording) bergantian disorot.
- **Teks layar:** "Simpan preset. Mulai dari template."
- **Narasi:** "Simpan konfigurasi sebagai preset, atau mulai dari template untuk podcast, streaming, rapat online, atau rekaman."

### Scene 6. Penutup (0:52–1:00)
- **Visual:** logo waveform dan nama AudioHub, sama dengan Video 1.
- **Teks layar:** "Coba AudioHub." Lalu CTA: "audiohub.khincreator.com".
- **Keterangan kecil:** "Hak Cipta © 2026 taufiq sholikhin. Seluruh hak dilindungi."
- **Narasi:** "Unduh AudioHub dan coba sendiri."

## 8. Catatan produksi

Hal-hal ini perlu dipegang di kedua video:

1. **Status rilis.** Paket sudah dinotarisasi dan dianggap prototipe final untuk demo. README masih menulis "belum dinotarisasi" dan "belum siap rilis publik", jadi README perlu diperbarui.
2. **Fitur di README tidak sesuai kode.** README menulis bahwa pan, solo, dan graph view "belum ada", tetapi kode di `Sources/AudioHubApp.swift` dan `Sources/Model.swift` sudah memilikinya. Brief ini mengikuti kode.
3. **Nama perangkat default.** Nama "Maono ProStudio 2x2" dipakai apa adanya untuk demo, sesuai keputusan Anda.
4. **Angka latensi.** Panel Perangkat menampilkan "29 ms" sebagai hasil tes sebelumnya, bukan pengukuran sesi itu. Jangan pakai angka itu di iklan kecuali sudah diukur ulang.
5. **Klaim yang tidak dipakai.** Iklan tidak menyebut kompatibilitas merek hardware tertentu, dan tidak mengklaim kualitas audio atau performa tanpa data.
6. **Sumber tangkapan layar.** Disarankan memakai rekaman layar asli dari aplikasi, bukan UI tiruan. Mode `--snapshot` di aplikasi bisa dipakai untuk gambar diam, tetapi tampilannya tetap statis.
7. **Angka sample rate.** Driver mendukung 8 kHz sampai 768 kHz, tetapi aplikasi hanya menawarkan rate yang didukung perangkat. Jangan tampilkan 768 kHz sebagai angka utama tanpa keterangan bahwa itu batas driver.
8. **Tidak ada parameter plugin.** Situs menulis bahwa parameter Audio Unit belum bisa diatur dari aplikasi. Jangan tampilkan pengaturan plugin di iklan.
9. **Merek pihak ketiga.** Lisensi di situs menyebut Apple, OBS, TikTok, dan Maono. Di iklan, sebut OBS atau TikTok hanya jika memang diperlukan. Tanpa itu, cukup sebut perangkat sebagai "perangkat audio" atau "mikrofon".

## 9. Yang belum masuk kedua video

Fitur-fitur ini ada di build 0.3.0 dan tercantum di situs, tetapi tidak masuk storyboard karena durasi tiap video tetap 60 detik. Bisa dipakai untuk video lanjutan atau versi pendek.

- **Tema tampilan.** Menu pengaturan memiliki pilihan Sistem, Terang, dan Gelap.
- **Kontrol volume hardware.** Panel Perangkat menampilkan slider volume dan bisu untuk output fisik, jika perangkat mendukungnya.
- **Perekaman output.** Tombol record di strip output menulis output ke berkas WAV.

## 10. Pertanyaan terbuka

Tidak ada. Dua video 60 detik, tanpa subtitle Inggris, dan narasi direkam sendiri oleh pemilik proyek.
