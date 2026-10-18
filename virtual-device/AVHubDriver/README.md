# AVHub Driver v2

Driver audio virtual untuk macOS dengan dua perangkat loopback stereo:

- **avhub_virtual1**
- **avhub_virtual2**

Audio yang dikirim ke output salah satu perangkat bisa dibaca kembali dari input perangkat yang sama. Contoh: AudioHub mengirim audio ke `avhub_virtual2`, lalu OBS atau TikTok LIVE Studio membacanya dari `avhub_virtual2` sebagai mikrofon.

Ditulis dari nol berdasarkan antarmuka `AudioServerPlugIn.h` dari SDK Apple. Tidak ada kode dari driver pihak lain yang disalin.

## Fitur

| Fitur | Keterangan |
|---|---|
| Dua perangkat stereo | Setiap perangkat: 1 stream output + 1 stream input, 2 kanal, float32 interleaved |
| Sample rate | 13 rate: 8, 16, 24, 44,1, 48, 88,2, 96, 176,4, 192, 352,8, 384, 705,6, 768 kHz |
| Ukuran buffer | Default 512 frame, bisa diubah 16–4096 |
| Volume master | Kurva dB linear: skalar 0–1 = -64 sampai 0 dB. Input dan output memakai nilai yang sama |
| Mute | Kontrol mute input dan output, memakai nilai master yang sama; saat aktif, input membaca nol |
| Koreksi pitch | Kontrol level `Pitch` (0–1, 0,5 netral), mengubah clock ±1 % |
| Clock source | Selector `Clock Source` dengan dua item: 0 = Nominal (default), 1 = Pitch Adjusted |
| Objek Box | Satu Box (`com.avhub.driver.box`) yang memuat kedua perangkat, dengan properti identify |
| Tulis terlambat | Output yang sudah lewat dari waktu sekarang ditolak (status `'what'`), tidak ditulis ke ring |
| Loopback | Input membaca sample time yang sama dengan output; driver tidak menambah offset |

## Pasang

Prasyarat: Xcode Command Line Tools (`xcode-select --install`), dipakai untuk membangun bundle.

1. Klik dua kali **`Install AVHub Driver v2.command`**.
2. Masukkan password admin Mac saat diminta.
3. Tunggu dialog "Driver AVHub v2 terpasang".
4. Jika perangkat belum muncul, restart Mac.

Hapus: klik dua kali **`Uninstall AVHub Driver v2.command`**.

Cara manual di Terminal:

```sh
sh build.sh                 # hasil di build/AVHubDriver.driver
sudo sh install.sh
sudo sh uninstall.sh        # untuk menghapus
```

## Verifikasi

```sh
system_profiler SPAudioDataType | grep avhub
```

Kedua perangkat `avhub_virtual1` dan `avhub_virtual2` harus muncul.

## Spesifikasi

| Properti | Nilai |
|---|---|
| Bundle ID | `com.avhub.driver` |
| Nama dan UID perangkat | `avhub_virtual1`, `avhub_virtual2` |
| Pabrikan | AVHub |
| Sample rate | 13 rate, 8 kHz sampai 768 kHz (default 48 kHz) |
| Format | 2 kanal (stereo), float32 interleaved |
| Ukuran buffer | 512 frame default, rentang 16–4096 |
| Zero timestamp period | 16384 frame |
| Latensi yang dilaporkan | 0 frame |
| Kontrol | Volume (dua kontrol input, dua output, dB linear -64..0) dan mute (input dan output). Pitch dan clock source, sisi global |
| Box | `com.avhub.driver.box`, berisi `avhub_virtual1` dan `avhub_virtual2` |
| Pitch dan clock source | Control object: pitch (level, 0–1) dan clock source (selector clck). Keduanya di sisi global |
| Penandatanganan | ad-hoc secara default; Developer ID lewat `SIGN_IDENTITY` |
| Arsitektur | Intel (x86_64) dan Apple Silicon (arm64) |
| Sistem minimum | macOS 11.0 |

### Kontrol volume dan mute

Ada dua kontrol volume dan dua kontrol mute per perangkat: satu pasang untuk input, satu pasang untuk output. Keempatnya memakai nilai master yang sama, jadi mengubah salah satunya langsung terlihat di yang lain.

Kurva volume: skalar 0–1 dipetakan linear ke dB pada rentang -64 sampai 0. Skalar 0,5 berarti -32 dB, dan skalar 0 membungkam sepenuhnya.

Volume diterapkan saat input dibaca. Mute membuat input nol tanpa menghentikan loopback.

Cara tampilannya di macOS (misalnya di Pengaturan Suara) belum diuji.

### Clock source dan koreksi pitch

Keduanya adalah control object standar, jadi bisa dibaca dan diubah lewat API CoreAudio biasa (`AudioObjectSetPropertyData`) tanpa selector khusus.

- **Pitch** (`kAudioLevelControlClassID`, skala 0–1): default 0,5 (netral).
  - `0`: clock 1 % lebih lambat.
  - `1`: clock 1 % lebih cepat.
  - Nilai di luar 0–1 dijepit. NaN ditolak.
- **Clock source** (`kAudioClockSourceControlClassID`, selector dengan item 0 dan 1): default item 0.
  - Item 0 (`Nominal`): clock nominal, pitch diabaikan.
  - Item 1 (`Pitch Adjusted`): clock memakai koreksi dari pitch.
  - Item lain ditolak.

Mengubah keduanya tidak memulai timeline baru, jadi tidak ada lompatan waktu. Koreksi pitch dan pilihan clock adalah dua hal yang terpisah.

### Box dan identify

Kedua perangkat dikelompokkan dalam satu Box. Box memiliki properti identify (`kAudioObjectPropertyIdentify`) yang bisa diset. Driver menerimanya tanpa efek tampilan, karena tidak ada lampu atau panel yang bisa dinyalakan.

### Tulis terlambat

Jika `outputTime + frames` lebih kecil dari `currentTime`, driver menolak operasi tulis dan mengembalikan status `'what'`. Output yang terlambat tidak pernah masuk ke ring.

## Perbedaan dengan AudioHub Virtual Driver (v1)

| | v1 (`audiohub_virtual1/2`) | v2 (`avhub_virtual1/2`) |
|---|---|---|
| Rate perangkat 1 | 6 rate (44,1–192 kHz) | 13 rate (8–768 kHz) |
| Rate perangkat 2 | 13 rate | 13 rate |
| Offset loopback | 2 periode tambahan di driver | Tidak ada |
| Volume dan mute | Tidak ada | Ada, input dan output, dua volume input |
| Koreksi pitch | Tidak ada | Ada, sebagai control (`Pitch`), dengan clock source (`Clock Source`) |
| Box dan identify | Tidak ada | Ada |
| Tulis terlambat | Ditulis ke ring | Ditolak |
| Sinkronisasi clock | Atomik | Mutex |
| Log diagnostik | Ada di jalur properti | Tidak ada |

## Pengujian

```sh
sh tests/run_tests.sh
```

Ada dua jenis uji, dan keduanya tidak memasang apa pun ke sistem:

- `tests/loopback_test.c` memanggil driver lewat vtable yang sama dengan yang dipakai `coreaudiod`. Ia mensimulasikan 300 siklus IO di 48 kHz, 96 kHz, 256 frame, dan memeriksa:
  - input siklus c = output siklus c-2, bit demi bit, dengan jeda dua siklus dari HAL;
  - volume (kurva dB, input dan output), mute, koreksi pitch, dan clock source (selector);
  - Box dan identify, serta penolakan tulis yang terlambat;
  - kedua perangkat terisolasi;
  - penggantian rate dan buffer lewat host, termasuk format stream;
  - penolakan nilai tidak valid.
- `tests/plugin_load_test.c` memuat bundle lewat CFPlugIn, sama seperti `coreaudiod` memuatnya.

Yang **belum** diuji: pemasangan ke `/Library/Audio/Plug-Ins/HAL/` dan pemuatan oleh `coreaudiod` yang sebenarnya. Latensi nyata dan perilaku di aplikasi juga baru bisa dipastikan setelah pemasangan di Mac ini.

## Isi folder

| Berkas | Fungsi |
|---|---|
| `src/AVHubDriver.c` | Kode driver |
| `src/Info.plist` | Metadata bundle dan factory UUID |
| `build.sh` | Membangun bundle universal dan menandatanganinya |
| `install.sh` / `uninstall.sh` | Pasang dan hapus dari HAL (perlu sudo) |
| `*.command` | Versi satu klik dari skrip di atas |
| `tests/` | Uji logika dan uji pemuatan bundle |

## Lisensi

Hak Cipta © 2026 taufiq sholikhin. Seluruh hak dilindungi. Situs: audiohub.khincreator.com

Driver ini tercakup oleh lisensi lengkap di berkas [LICENSE](LICENSE) pada folder ini, yang sama dengan lisensi AudioHub.
