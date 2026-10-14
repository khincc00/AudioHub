# Virtual Device AudioHub

Driver virtual sendiri (AudioServerPlugIn / HAL) untuk menggantikan EMEET Virtual Audio di jalur virtual cable.

Status: **dua perangkat virtual sudah dibangun dan lulus uji offline. Pemasangan ulang di mesin uji masih perlu dilakukan.** Versi sebelumnya (satu perangkat) sudah terpasang dan berfungsi.

Perangkat:

- `AudioHub Virtual` (UID `com.audiohub.virtual.device`)
- `AudioHub Virtual 2` (UID `com.audiohub.virtual.device2`)

Masing-masing 2 in, 2 out, 48 kHz, dengan ring buffer dan clock sendiri. Uji offline: `sh tests/run.sh`.

Hasil uji pada mesin uji:

- Loopback: tone −30 dBFS ke output 1 muncul di input 1 pada −33 dBFS (RMS), input 2 diam.
- Receive lewat aggregate (PS22 + AudioHub Virtual): −30,0 dBFS sampai PS22 output 1, output 2 diam, 0 dropout dalam 9 detik.
- Send lewat aggregate: level yang dikirim sama dengan input mic, dan kembali dari virtual cable dengan selisih kecil, 0 dropout dalam 8 detik.

Temuan terbuka:

- Core Audio melaporkan buffer 192 frame dengan rentang 15–192, padahal fungsi driver yang dipanggil langsung mengembalikan 512. Penyebabnya ada di sisi HAL dan belum ditemukan. Tidak menghalangi fungsi audio.
- Latensi loopback 29,42 ms didominasi lag input 1024 frame yang tetap di driver. Perbaikan sudah ditulis: lag input sekarang 2 × buffer aktif, dan logikanya lulus uji ring buffer offline. Sudah dipasang ulang dan diukur: latensi loopback turun dari 29,42 ms ke 16,08 ms.

Perubahan ketiga: penulisan dan pembacaan ring buffer dipisah dengan fence memori (`atomic_thread_fence`), bukan lock.

## Isi

- `AudioHubDriver/AudioHubDriver.c`: driver. Satu perangkat `AudioHub Virtual`, 2 in / 2 out, 48 kHz, float32 interleaved, ring buffer 16.384 frame.
- `AudioHubDriver/Info.plist`: pendaftaran plugin ke Core Audio (`CFPlugInTypes` dan `CFPlugInFactories`).
- `AudioHubDriver/build.sh`: membangun `build/AudioHub.driver`. Tidak memasang apa pun.
- `AudioHubDriver/install.sh`: menyalin ke `/Library/Audio/Plug-Ins/HAL` dan me-restart `coreaudiod`. Butuh password admin.
- `AudioHubDriver/uninstall.sh`: menghapus driver dan me-restart `coreaudiod`.

## Perilaku

- Output yang ditulis aplikasi masuk ke ring buffer pada posisi `mOutputTime`.
- Input dibaca dari posisi `mInputTime` dikurangi 1024 frame (dua buffer). Kompensasi ini memastikan data sudah tertulis sebelum dibaca. Latensi loopback diperkirakan sekitar 2 × buffer ditambah overhead.
- Clock dibuat dari `mach_absolute_time` dengan periode 512 frame.
- UID perangkat: `com.audiohub.virtual.device`. Nama tampilan: `AudioHub Virtual`.

## Keterbatasan prototipe

- Belum memakai lock atau memory barrier antara thread IO. Penulisan dan pembacaan ring buffer bisa tumpang-tindih pada kondisi tertentu.
- Tidak ada perubahan sample rate atau buffer size dari luar. Properti hanya baca.
- Tidak menangani sleep/wake, pemasangan ulang, atau `PerformDeviceConfigurationChange`.
- `CreateDevice` dan `DestroyDevice` tidak didukung. Hanya satu perangkat statis.
- Belum ditandatangani dengan identitas developer. Tanda tangan ad-hoc hanya cukup untuk pengujian lokal.

## Risiko

Driver ini berjalan di dalam `coreaudiod`. Crash di sana akan memutus semua audio sampai service restart. Pasang hanya di mesin uji, dan siapkan `uninstall.sh` sebelum memasang.

## Rencana uji

1. `sh AudioHubDriver/build.sh`
2. `sh AudioHubDriver/install.sh` (akan meminta password admin, lalu me-restart `coreaudiod`)
3. Buka Audio MIDI Setup. Pastikan muncul perangkat `AudioHub Virtual` dengan 2 input dan 2 output.
4. Jalankan tone ke `AudioHub Virtual` output 1, lalu baca input dari perangkat yang sama dengan `capture`. Level harus sama, dan kanal tidak boleh tercampur.
5. Ulangi uji receive dan send Tahap 2, dengan UID `com.audiohub.virtual.device` menggantikan EMEET.
6. Ukur latensi loopback dengan `loopback`. Targetnya dibandingkan dengan 29 ms dari PS22.
7. Jika ada masalah, jalankan `sh AudioHubDriver/uninstall.sh`.
