# AudioHub untuk Windows

Port AudioHub ke Windows 10 (build 19041 ke atas) dan Windows 11, dengan engine routing yang sama dengan versi macOS.

Dikerjakan di Mac, jadi status tiap bagian dibedakan menurut apa yang sudah benar-benar diuji.

## Status

| Bagian | Lokasi | Status |
|---|---|---|
| Engine routing, DSP, graph | `engine/` (bersama macOS, tanpa perubahan) | Dipakai apa adanya. Uji unit lulus di macOS (`sh tests/unit/run.sh`). Belum dikompilasi dengan MSVC. |
| Ring buffer SPSC | `io/src/ring.h` | **Diuji**: uji unit dan ThreadSanitizer lulus di macOS (`io/tests/test_ring.cpp`). |
| Backend WASAPI | `io/src/ah_io.cpp`, `io/src/devices.cpp` | **Belum dikompilasi dan belum diuji di perangkat.** Ditulis dengan API Windows dari dokumentasi. Lihat "Yang belum diuji" di bawah. |
| Tangkap audio per aplikasi | `io/src/devices.cpp` (process loopback) | **Belum diuji.** Membutuhkan Windows 10 build 19041 ke atas. |
| Library Core (P/Invoke, parser, scene) | `app/AudioHub.Core/` | **Diuji**: 18 tes xUnit lulus di macOS (`dotnet test`). |
| Aplikasi WinUI 3 | `app/AudioHub.Windows/` | **Belum dibangun.** Hanya bisa dibangun di Windows. |
| Driver perangkat virtual AVHub | `driver/` | **Dibuat dari Simple Audio Sample** (lihat `driver/CHANGES.md`). Logika FIFO loopback diuji di macOS. Belum dikompilasi dengan WDK dan belum diuji di Windows. |
| Installer | `installer/AudioHub.iss` | Belum diuji. Butuh driver dan publish aplikasi. |
| CI Windows | `.github/workflows/windows.yml` | Belum pernah dijalankan. Akan berjalan saat branch didorong ke GitHub. |

## Struktur

```text
windows/
  CMakeLists.txt             build engine + backend (audiohub_io.dll)
  io/include/audiohub_io.h   API C (subset dari backends/coreaudio, plus daftar perangkat dan proses)
  io/src/                    WASAPI, process loopback, ring buffer
  io/tests/                  uji ring buffer (berjalan di macOS dan Windows)
  app/AudioHub.Core/         P/Invoke, wrapper engine, parser, format scene v1 (net10.0)
  app/AudioHub.Core.Tests/   xUnit
  app/AudioHub.Windows/      WinUI 3 (net10.0-windows10.0.19041.0)
  driver/                    driver AVHub (dari Simple Audio Sample), skrip pemasangan, dan uji FIFO
  installer/                 skrip Inno Setup
```

## Cara build

Di Windows dengan Visual Studio 2022 (C++ desktop), CMake, .NET 10 SDK, dan Inno Setup 6:

```powershell
# 1. Backend (DLL)
cmake -S windows -B windows\build -A x64
cmake --build windows\build --config Release

# 2. Tes
dotnet test windows\app\AudioHub.Core.Tests\AudioHub.Core.Tests.csproj

# 3. Aplikasi
dotnet publish windows\app\AudioHub.Windows\AudioHub.Windows.csproj -c Release -r win-x64 -p:Platform=x64 -o windows\build\publish

# 4. Installer (setelah driver dibuat; tanpa driver, pakai /DDriverDir= kosong)
iscc /DAppVersion=0.3.0 /DPublishDir=..\build\publish windows\installer\AudioHub.iss
```

Di macOS, hanya bagian yang bisa dibangun di sana yang bisa dicek:

```sh
sh tests/unit/run.sh                                   # engine
clang++ -std=c++17 -pthread windows/io/tests/test_ring.cpp -o /tmp/r && /tmp/r   # ring buffer
dotnet test windows/app/AudioHub.Core.Tests/AudioHub.Core.Tests.csproj            # library Core
```

## Perbedaan dengan macOS

- **Perangkat:** Windows tidak punya agregat. Engine membaca endpoint input dan output sebagai aliran terpisah, lalu ring buffer menyesuaikan clock masing-masing. Ini menambah latensi dibanding Core Audio dan bisa menimbulkan klik kecil saat clock tidak sama.
- **Sample rate:** semua endpoint memakai sample rate capture fisik. Endpoint dengan rate lain dikonversi oleh WASAPI (`AUTOCONVERTPCM`).
- **Buffer:** ukuran buffer diatur Windows, tidak bisa dipilih dari aplikasi.
- **Isolasi tangkap aplikasi:** di macOS "Muted When Tapped" membisukan output aplikasi saat ditangkap. Di Windows, aplikasi membisukan sesi audio proses tersebut (`ISimpleAudioVolume`). **Belum diketahui** apakah mute sesi juga membisukan hasil process loopback. Ini harus diuji di perangkat.
- **Plugin:** VST3 belum dihosting (lihat di bawah). Audio Unit tidak ada di Windows. Rantai FX per input di aplikasi Windows baru terbatas pada dinamika engine (gate, EQ, kompresor, delay, limiter).
- **Scene:** format scene v1 sama dengan macOS. Perangkat disimpan lewat nama endpoint, bukan UID Core Audio.
- **Daftar aplikasi:** hanya proses dengan sesi audio di output default yang muncul. Aplikasi yang mengirim audio ke output lain tidak terdaftar.
- **Tangkap audio per proses:** proses anak (helper) ikut lewat mode "include process tree", tetapi sesi mute hanya dicari pada PID yang dipilih.

## Yang belum diuji (wajib diuji di Windows)

1. Kompilasi `io/src/*.cpp` dengan MSVC, termasuk peringatan `/W4`.
2. Pembukaan endpoint, format float32, dan `AUTOCONVERTPCM` pada perangkat nyata (PS22 atau sejenisnya).
3. Process loopback: `ActivateAudioInterfaceAsync` dengan `PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE`, serta apakah `AUDCLNT_STREAMFLAGS_LOOPBACK` dibutuhkan atau justru menghalangi pada versi Windows tertentu.
4. Perilaku mute sesi saat tap aktif (lihat catatan di atas).
5. Drift antara endpoint fisik dan virtual: jumlah underrun dan overrun setelah beberapa menit.
6. Driver: kompilasi WDK, pemuatan, nama endpoint, latensi loopback, dan pencopotan. Lihat `driver/README.md`.
7. Versi paket Windows App SDK di `AudioHub.Windows.csproj` (`1.8.250916003`). Sesuaikan jika restore gagal.
8. Plugin: VST3 hosting belum ada. Perlu SDK Steinberg dan keputusan lisensi.
9. Izin mikrofon di Pengaturan Windows (Privasi > Mikrofon) harus mengizinkan aplikasi desktop.

## Yang belum dikerjakan

- **VST3.** Host plugin VST3 dan VST2 di Windows.
- **AudioHub Studio** (versi Windows).
- **Hardware volume** per perangkat fisik (`IAudioEndpointVolume`), yang ada di macOS.
- **Preset dan template.** Aplikasi Windows baru menyimpan dan memuat scene.
- **Ubah nama kanal.** Nama kanal masih otomatis dari posisi endpoint.
