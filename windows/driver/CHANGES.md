# Perubahan terhadap Simple Audio Sample

Basis: `microsoft/Windows-driver-samples`, `audio/simpleaudiosample`, commit tercantum di
[UPSTREAM_COMMIT.txt](UPSTREAM_COMMIT.txt). Lisensi: Microsoft Public License (MS-PL).

Semua berkas upstream tetap ada, kecuali yang tercantum di bawah. Berkas dengan akhiran `.vcxproj` diubah hanya
di satu baris.

## Berkas baru

| Berkas | Isi |
|---|---|
| `src/Source/Inc/avhub_loopback.h` | Struktur `AVHUB_FIFO` dan prototipe fungsi FIFO. |
| `src/Source/Main/avhub_loopback.cpp` | Implementasi FIFO: `AvhubFifoInit`, `AvhubFifoFree`, `AvhubFifoWrite`, `AvhubFifoRead`. Kapasitas 0,2 detik (38400 byte, 16-bit stereo 48 kHz). |

## Berkas yang diubah

| Berkas | Perubahan |
|---|---|
| `src/Source/Inc/common.h` | `#include "avhub_loopback.h"`, dan method `GetLoopbackFifo` ditambahkan ke `IAdapterCommon`. |
| `src/Source/Main/common.cpp` | `CAdapterCommon` memegang satu `AVHUB_FIFO` (per perangkat). `Init` mengalokasikannya, destruktor membebaskannya, dan `GetLoopbackFifo` mengembalikannya. |
| `src/Source/Main/minwavertstream.h` | Deklarasi `WriteLoopbackBytes` dan `ReadLoopbackBytes`. |
| `src/Source/Main/minwavertstream.cpp` | Di `UpdatePosition`: stream capture memanggil `ReadLoopbackBytes` (menggantikan `WriteBytes`, yaitu tone generator). Stream render memanggil `WriteLoopbackBytes` sebelum `ReadBytes`. Kedua fungsi baru ditambahkan di akhir berkas. |
| `src/Source/Filters/micarraywavtable.h` | Format mic array dari 32-bit menjadi 16-bit, sama dengan speaker, agar frame loopback cocok. Empat tempat diubah: konstanta, komentar, format wave (`nBlockAlign`, `nAvgBytesPerSec`, `wBitsPerSample`, `wValidBitsPerSample`), dan rentang data. |
| `src/Source/Main/Main.vcxproj` | Menambahkan `avhub_loopback.cpp` ke daftar kompilasi. |
| `src/Source/Main/SimpleAudioSample.inx` | Dibuat ulang oleh `tools/gen_inx.py`. Tiga root device (`ROOT\AVHUBVC12`, `ROOT\AVHUBVC34`, `ROOT\AVHUBLOOP`), masing-masing dengan section instalasi sendiri agar endpoint punya nama sendiri. Nama layanan dan binary tetap `SimpleAudioSample`. |

## Yang tidak diubah

- Tone generator (`ToneGenerator.cpp`) masih ada, tetapi tidak lagi dipanggil dari jalur capture.
- Topologi, tabel pin, dan format speaker tidak berubah.
- Penyimpanan data debug (`savedata.cpp`) tetap sama. Ia hanya aktif bila `g_DoNotCreateDataFiles` salah.
- Nama dan GUID interface KS tetap sama dengan upstream, karena dipakai oleh INF.

## Batasan desain

- FIFO hanya bekerja dengan format yang sama di kedua sisi. Format tabel speaker dan mic array sekarang sama (2 kanal, 16-bit, 48 kHz). Jika Windows meminta format lain, capture menerima diam.
- FIFO menyimpan 0,2 detik. Jika capture lebih lambat dari render, data lama dibuang, sehingga latensi tetap terbatas. Jika tidak ada capture yang berjalan, FIFO tetap penuh dengan data terakhir.
- Clock render dan capture berasal dari QPC yang sama, tetapi keduanya tetap berjalan terpisah. Selisih kecil akan terlihat sebagai pembuangan atau pengisian diam di FIFO.
