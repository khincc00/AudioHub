# Driver perangkat virtual AVHub (Windows)

**Status: belum dikompilasi dengan WDK dan belum diuji di Windows.** Yang sudah diuji hanya logika FIFO loopback di macOS. Driver ini adalah kode kernel yang bisa membuat mesin pengguna tidak stabil. Jangan pasang di mesin utama. Uji hanya di mesin virtual.

## Apa ini

Driver dibuat dari sampel [Simple Audio Sample](https://github.com/microsoft/Windows-driver-samples/tree/main/audio/simpleaudiosample) (Microsoft Public License, salinan di `src/LICENSE-MS-PL.txt`). Sampel itu berisi satu endpoint speaker dan satu mic array per perangkat. Dibandingkan dengan sampel asli, AVHub menambahkan:

1. **Loopback render ke capture.** Audio yang diputar ke endpoint output AVHub muncul di endpoint input AVHub yang sama. Ini menggantikan tone generator di sampel asli. Lihat `src/Source/Main/avhub_loopback.cpp` dan `src/Source/Main/minwavertstream.cpp` (`WriteLoopbackBytes`, `ReadLoopbackBytes`).
2. **Tiga perangkat.** Tiga root device memakai satu binary driver: `ROOT\AVHUBVC12`, `ROOT\AVHUBVC34`, dan `ROOT\AVHUBLOOP`. Nama endpoint diatur di INF (`src/Source/Main/SimpleAudioSample.inx`, dibuat oleh `tools/gen_inx.py`).
3. **Format 16-bit untuk mic array** agar frame loopback cocok dengan speaker (`src/Source/Filters/micarraywavtable.h`).

Daftar lengkap perubahan ada di [CHANGES.md](CHANGES.md). Commit upstream yang dipakai ada di [UPSTREAM_COMMIT.txt](UPSTREAM_COMMIT.txt).

Nama endpoint harus sama dengan yang dipakai aplikasi (`MainWindow.xaml.cs`): `AVHub VC 1/2`, `AVHub VC 3/4`, dan `AVHub Loopback`.

## Yang sudah diuji

| Bagian | Cara | Hasil |
|---|---|---|
| FIFO loopback (`avhub_loopback.cpp`) | `sh windows/driver/tests/run.sh`, dengan shim API kernel | Lulus: uji tetap dan 400.000 operasi acak dibandingkan dengan model referensi |
| INF (`SimpleAudioSample.inx`) | `tools/gen_inx.py` dan pemeriksaan referensi string | Tidak ada referensi `%...%` yang hilang dan tidak ada section ganda. Belum diperiksa oleh `stampinf` atau `chkinf`. |

## Yang belum diuji

- Kompilasi dengan WDK dan MSBuild. Mungkin ada kesalahan tipe atau include yang hanya muncul di build sebenarnya.
- Pemuatan driver, pembuatan root device, dan nama endpoint di Pengaturan Suara.
- Latensi dan klik loopback. Ring buffer memakai timer masing-masing stream, jadi kedua clock tidak disinkronkan.
- Perilaku saat stream dimulai dan dihentikan berulang, serta saat driver dicopot.
- Pelepasan memori saat perangkat dicabut (Driver Verifier).

## Membangun

Butuh Visual Studio 2022 dengan workload "Desktop development with C++", dan Windows Driver Kit (WDK) yang sesuai dengan Windows SDK.

1. Buka `src/SimpleAudioSample.sln`.
2. Pilih konfigurasi **Debug** dan platform **x64**. Default sampel adalah Win32, dan itu tidak dipakai di sini.
3. Build solution.
4. Ambil berkas dari folder `x64\Debug\package\` (sesuaikan dengan hasil build):
   - `SimpleAudioSample.inf`
   - `simpleaudiosample.sys`
   - `SimpleAudioSample.cat`
5. Salin ketiga berkas itu ke `windows/driver/build/`, dan salin `devcon.exe` dari WDK (`Tools\x64\devcon.exe`) ke folder yang sama.

## Menguji di mesin virtual

1. Buat VM Windows 11 (atau 10 build 19041 ke atas). Simpan snapshot sebelum pemasangan.
2. Aktifkan test signing di VM: `bcdedit /set testsigning on`, lalu restart. Tandatangani `SimpleAudioSample.cat` dengan test certificate (`signtool sign /fd SHA256 /a /f <cert> ...`), atau pasang sertifikat itu di Trusted Root dan Trusted Publisher.
3. Jalankan `scripts\install-driver.ps1` sebagai administrator dengan `-DriverDir` menunjuk ke `windows/driver/build`.
4. Periksa di Pengaturan Suara: harus ada output dan input `AVHub VC 1/2`, `AVHub VC 3/4`, dan `AVHub Loopback`.
5. Uji loopback: putar musik ke output `AVHub VC 1/2`, lalu rekam dari input `AVHub VC 1/2` (dengan Sound Recorder atau aplikasi AudioHub). Rekaman harus berisi audio yang sama.
6. Restart VM dan ulangi langkah 4 dan 5.
7. Copot dengan `scripts\uninstall-driver.ps1`, lalu pastikan tidak ada endpoint yang tersisa.

## Distribusi

Driver yang dipasang di mesin pengguna biasa harus ditandatangani dengan **attestation signing** (akun Partner Center dan sertifikat EV). Sampel ini juga bukan hasil sertifikasi HLK. Keduanya perlu dikerjakan sebelum rilis publik.

## Keputusan terbuka

- Driver ini dibuat sendiri, sesuai permintaan. Untuk pengguna biasa, VB-Cable dengan lisensi pihak ketiga tetap bisa dipakai sebagai alternatif (lihat catatan di `.gitignore` tentang driver pihak ketiga).
