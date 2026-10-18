# AudioHub Studio: status implementasi terhadap spesifikasi

Dasar: `/Users/khincc/Documents/WEBSITE BUILD/AUDSTUDIO HUB/reaper-fx-analysis/SPEC-track-fx-setting.md` (acuan REAPER 7.82.0, bagian 7 R1–R18).

Ditulis untuk: pengembang dan peninjau proyek ini. Status: **prototipe**, belum diuji dengan perangkat audio fisik.

Legenda: **Ada** = diimplementasikan dan diuji; **Sebagian** = ada, dengan batasan; **Belum** = tidak diimplementasikan.

## Yang dibangun

| Bagian | Lokasi |
|---|---|
| Aplikasi baru (SwiftUI) | `apps/macos/AudioHubStudio/` |
| Spesifikasi proyek Xcode | `AudioHubStudio.yml` (terpisah dari `project.yml`) |
| Rantai FX per input, hingga 8 slot AU | `backends/coreaudio/io/audiohub_io.cpp`, `ah_io_fx_*` di `audiohub_io.h` |
| Parameter dan state AU | `backends/coreaudio/io/au_insert.cpp`, `paramInfo`, `getClassInfo`, `setClassInfo` |
| Aturan nama dan instantiate | `engine/fx/fx_rules.cpp` |
| Model rantai, routing, dan proyek | `apps/macos/AudioHubStudio/Sources/StudioChains.swift` |
| Tampilan | `apps/macos/AudioHubStudio/Sources/StudioViews.swift` |

Aplikasi baru memakai ulang `Model.swift` (`EngineController`) dan backend CoreAudio dari AudioHub. Perangkat fisik, virtual device, routing matrix, gain, dan mute semuanya dari engine yang sama. Aplikasi AudioHub yang sudah ada tidak berubah perilakunya.

## Status per butir spesifikasi

| Butir | Status | Catatan |
|---|---|---|
| R1 Model track | Sebagian | Track = input channel. FX chain utama per input ada. FX chain input rekaman dan FX monitoring di master belum ada. |
| R2 GUID FX | Belum | FX diidentifikasi dengan indeks track dan slot (0–7). Tidak ada GUID stabil. |
| R3 Tipe dari prefix | Sebagian | Prefix dikenali dan diuji (`fx_rules.cpp`). Hanya Audio Unit yang bisa dihosting. VST3, VST2, VST, JS, dan DX ditolak. |
| R4 Aturan `instantiate` | Sebagian | Diimplementasikan dan diuji di `resolveInsert`. Belum dipakai oleh UI. UI memasang FX di slot kosong pertama. |
| R5 Nilai gagal `-1` | Ada | `resolveInsert` dan `ah_io_fx_load` mengembalikan `-1` saat gagal. |
| R6 Enabled, offline, bypass | Ada | Enabled (bypass) bisa diubah saat berjalan. Offline = nama tersimpan tetapi AU gagal dimuat, ditandai di UI. |
| R7 PDC auto-bypass | Belum | Tidak ada kompensasi latency per FX. |
| R8 Muat dengan FX offline | Sebagian | Proyek dibuka dengan FX yang gagal dimuat sebagai offline. Tidak ada opsi global `fxoffline:`. |
| R9 Preset | Sebagian | State AU penuh disimpan dan dipulihkan di berkas proyek. Pemilihan preset pabrik berdasarkan nama belum ada. |
| R10 Parameter | Sebagian | Daftar nama, rentang, default, dan nilai; ubah nilai. Nilai ternormalisasi, string terformat, dan step size belum ada. |
| R11 Persistensi | Sebagian | Format JSON sendiri (`schemaVersion: 1`), bukan format `.RPP` REAPER. Berisi routing, gain, mute, dan chain. |
| R12 Kesalahan simpan | Sebagian | Kegagalan simpan proyek dilaporkan. State AU yang gagal diambil dilewati tanpa pesan. |
| R13 Registri plugin | Sebagian | Daftar diambil dari registri Audio Unit sistem. Direktori pemindaian, cache, dan aksi pindai ulang belum ada. |
| R14 Mode aman dan default per plugin | Belum | |
| R15 Undo dan latency | Belum | Tidak ada undo untuk FX. Latency AU tidak dihitung. |
| R16 Automation mode track | Belum | Di luar cakupan UI saat ini. |
| R17 Nomor track | Belum | Track ditampilkan dengan nama, tanpa nomor baca-saja. |
| R18 Lingkup konfigurasi | Belum | Tidak ada pemisahan preferensi global dan default proyek. |

## Perilaku yang diimplementasikan

- **Urutan proses:** chain FX diproses sebelum engine routing, berurutan sesuai urutan yang dipilih pengguna. Urutan dan enabled diubah lewat array atomik, sehingga aman saat IO berjalan.
- **Muat, lepas, dan state:** hanya saat IO berhenti. Engine dihentikan sebentar lalu dijalankan lagi.
- **Parameter:** diubah saat IO berjalan. Nilai terakhir disimpan di model dan diterapkan ulang setelah perangkat dibuka ulang.
- **Muat ulang otomatis:** setiap perangkat dibuka (termasuk sambung ulang dan perubahan sample rate), chain dimuat ulang dari model. Ini memakai `EngineController.ioGeneration`.
- **Pelepasan AU saat tutup:** `ah_io_close` sekarang melepas semua AU. Sebelumnya AU bocor, karena `AUInsert` tidak punya destruktor.
- **Kompatibilitas AudioHub:** API insert lama (`ah_io_insert_*`) sekarang sama dengan slot 0 chain. Perilakunya tidak berubah.

## Verifikasi yang sudah dijalankan

| Pemeriksaan | Hasil |
|---|---|
| `sh tests/unit/run.sh` (termasuk `test_fx_rules`) | Lulus, kode keluar 0 |
| Kompilasi backend dengan `-Wall -Wextra -Werror` | Lulus |
| Build target `AudioHubStudio` (Debug, tanpa tanda tangan) | Berhasil. Satu peringatan lama di `Model.swift:212`, bukan dari perubahan ini |
| Build target `AudioHub` (Debug, tanpa tanda tangan) | Berhasil |
| `sh tests/integration/run_au_probe.sh` (AU bawaan `Apple: AUDelay`) | Lulus: muat, enumerasi parameter, ubah dan baca nilai, proses buffer, simpan dan pulihkan state |

## Belum diverifikasi

- Chain FX di callback IO dengan perangkat audio fisik atau virtual. Tidak ada perangkat di lingkungan build.
- Aplikasi tidak dijalankan. Tampilan, interaksi, dan dialog simpan atau buka belum diuji langsung.
- Kestabilan real-time: pemanggilan AU di jalur audio memakai pola yang sama dengan insert AudioHub yang sudah ada, tetapi belum diuji dengan beban nyata.
- Plugin AU pihak ketiga. Hanya AU bawaan Apple yang diuji.
- Pengembalian state AU pihak ketiga yang tidak mengikuti `ClassInfo` standar.

## Batasan yang sudah diketahui

- Hanya Audio Unit (`aumu`/`aufx`) yang dihosting. VST3 dan VST2 belum didukung oleh backend.
- Setelah perangkat dibuka ulang, nilai parameter dipulihkan dari model. State penuh AU (preset internal plugin) hanya dipulihkan dari saat proyek terakhir disimpan. Perubahan di UI bawaan plugin yang belum disimpan ke proyek bisa hilang saat sambung ulang.
- Target `AudioHub` yang dibangun dengan `project.yml` bergantung pada `virtual-device/AudioHubDriver/` yang tidak ada di repo. Build target AudioHub saja (`-target AudioHub`) tetap berhasil.
