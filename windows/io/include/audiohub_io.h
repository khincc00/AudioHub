// Backend Windows (WASAPI): satu sesi IO yang menghubungkan endpoint fisik, perangkat virtual AVHub,
// dan tap proses (tangkap audio per aplikasi) ke engine routing. Nama fungsi dan arti argumennya mengikuti
// backend CoreAudio (backends/coreaudio/io/audiohub_io.h) sejauh didukung. Yang belum ada di Windows
// (rantai FX Audio Unit/VST, hardware volume) tidak dideklarasikan di sini.
#ifndef AUDIOHUB_IO_WIN_H
#define AUDIOHUB_IO_WIN_H

#include <stdint.h>
#include "../../../engine/api/audiohub_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AHIO AHIO;

typedef struct AHIOStats {
    uint64_t callbacks;         // jumlah blok engine yang diproses
    uint64_t dropouts;          // total underrun dan overrun semua stream; proksi untuk xrun
    uint64_t missingFrames;     // frame yang diisi nol karena data belum tersedia
    int inputs;
    int outputs;
    uint32_t bufferFrames;      // periode endpoint fisik capture, dalam frame
    double sampleRate;
    uint32_t inputLatencyFrames;
    uint32_t outputLatencyFrames;
} AHIOStats;

// Konfigurasi pembukaan. Setiap nama adalah potongan teks yang dicocokkan (tidak peka huruf besar) dengan
// nama endpoint Windows. NULL atau string kosong = tidak dipakai.
// Perangkat virtual memiliki endpoint capture dan render; keduanya harus ditemukan.
typedef struct AHIOOpenConfig {
    const char* physicalName;   // wajib: capture dan render fisik
    const char* virt1Name;      // mis. "AVHub VC 1/2"
    const char* virt2Name;      // mis. "AVHub VC 3/4"
    const char* virt3Name;      // mis. "AVHub Loopback"
    const uint32_t* tapPids;    // PID proses yang audionya ditangkap
    int tapCount;
    int tapIsolate;             // 1 = bisukan sesi proses yang ditangkap saat IO berjalan
} AHIOOpenConfig;

// Membuka semua endpoint dan membuat engine. Mengembalikan NULL jika gagal; errBuf berisi pesan singkat.
AHIO* ah_io_open(const AHIOOpenConfig* cfg, char* errBuf, int errLen);
void ah_io_close(AHIO* io);
// 0 jika berhasil, -1 jika gagal. Setelah dipanggil, endpoint mulai menerima dan mengirim audio.
int ah_io_start(AHIO* io);
void ah_io_stop(AHIO* io);
// 1 jika semua endpoint masih hidup, 0 jika ada perangkat yang hilang (mis. dicabut).
int ah_io_is_alive(AHIO* io);

int ah_io_inputs(AHIO* io);
int ah_io_outputs(AHIO* io);
int ah_io_physical_inputs(AHIO* io);
int ah_io_physical_outputs(AHIO* io);

// 1 jika route boleh dibuat (indeks valid), 0 jika tidak.
int ah_io_route_allowed(AHIO* io, int input, int output);
// Pemilik kanal: 0 = fisik, 1 = virtual 1, 2 = virtual 2, 3 = virtual 3, 4 = tap (input saja), -1 jika di luar.
// local = nomor kanal di dalam pemiliknya, mulai dari 1. output = 0 untuk input, 1 untuk output.
void ah_io_channel_owner(AHIO* io, int output, int channel, int* owner, int* local);
// 0 jika berhasil, -1 jika indeks tidak valid. Route virtual ke virtual pada perangkat yang sama tidak
// mengirim audio (lihat route_policy.h).
int ah_io_set_route(AHIO* io, int input, int output, int enabled, float sendDb);

void ah_io_set_channel(AHIO* io, int input, float gainDb, int mute);
void ah_io_set_channel_pan(AHIO* io, int input, float pan);
void ah_io_set_output(AHIO* io, int output, float gainDb, int mute);
void ah_io_set_input_fx(AHIO* io, int input, const AHGateParams* gate, const AHEqParams* eq, const AHCompParams* comp);
void ah_io_set_input_delay(AHIO* io, int input, const AHDelayParams* delay);
void ah_io_set_output_limiter(AHIO* io, int output, const AHLimiterParams* limiter);

// Peak blok terakhir: inPeaks berukuran inputs, outPeaks berukuran outputs.
void ah_io_peaks(AHIO* io, float* inPeaks, float* outPeaks);
void ah_io_stats(AHIO* io, AHIOStats* out);

// Rekaman satu output (mono, WAV float 32-bit). Penulisan di thread terpisah. 0 jika berhasil, -1 jika gagal.
int ah_io_record_start(AHIO* io, int output, const char* utf8Path);
void ah_io_record_stop(AHIO* io);
void ah_io_record_stats(AHIO* io, uint64_t* written, uint64_t* dropped);

// Daftar endpoint aktif, satu baris per endpoint: "in<TAB>nama<TAB>sampleRate<TAB>kanal" atau "out...".
// Mengembalikan jumlah byte yang ditulis (tanpa terminator).
int ah_io_list_devices(char* buf, int len);
// Daftar proses yang punya sesi audio, satu baris per proses: "pid<TAB>nama".
int ah_io_list_processes(char* buf, int len);

#ifdef __cplusplus
}
#endif

#endif
