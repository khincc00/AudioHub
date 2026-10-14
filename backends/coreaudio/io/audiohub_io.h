// Backend CoreAudio: satu perangkat duplex, dihubungkan ke engine lewat API C.
#ifndef AUDIOHUB_IO_H
#define AUDIOHUB_IO_H

#include <stdint.h>
#include "../../../engine/api/audiohub_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AHIO AHIO;

typedef struct AHIOStats {
    uint64_t callbacks;
    uint64_t dropouts;      // lompatan waktu sampel; proksi untuk xrun
    uint64_t missingFrames;
    int inputs;
    int outputs;
    uint32_t bufferFrames;
    double sampleRate;
} AHIOStats;

// Membuka perangkat yang namanya mengandung deviceNameContains. Mengembalikan NULL jika gagal;
// errBuf berisi pesan singkat.
AHIO* ah_io_open(const char* deviceNameContains, char* errBuf, int errLen);

// Membuat aggregate privat sementara: master = perangkat fisik, sub-device = perangkat virtual
// (dengan drift compensation). Aggregate dihapus saat ah_io_close.
AHIO* ah_io_open_with_virtual(const char* masterNameContains, const char* virtualUID, char* errBuf, int errLen);
// Sama seperti di atas, dengan dua virtual device. virtualUID2 boleh NULL.
AHIO* ah_io_open_with_virtuals(const char* masterNameContains, const char* virtualUID1, const char* virtualUID2, char* errBuf, int errLen);
void ah_io_close(AHIO* io);
int ah_io_start(AHIO* io);
void ah_io_stop(AHIO* io);

int ah_io_inputs(AHIO* io);
int ah_io_outputs(AHIO* io);

// Mengembalikan 0 jika berhasil, -1 jika ditolak. Route dari input virtual ke output virtual
// selalu ditolak: driver virtual meloopback output ke input, sehingga akan membentuk loop.
int ah_io_set_route(AHIO* io, int input, int output, int enabled, float sendDb);
// Jumlah kanal milik perangkat fisik (sisanya kanal virtual) di dalam aggregate.
int ah_io_physical_inputs(AHIO* io);
// 1 jika perangkat masih terpasang dan hidup di Core Audio, 0 jika sudah hilang.
int ah_io_is_alive(AHIO* io);
int ah_io_physical_outputs(AHIO* io);
void ah_io_set_channel(AHIO* io, int input, float gainDb, int mute);

void ah_io_set_output(AHIO* io, int output, float gainDb, int mute);

// Parameter DSP per input (gate, EQ, kompresor) dan limiter per output.
void ah_io_set_input_fx(AHIO* io, int input, const AHGateParams* gate, const AHEqParams* eq, const AHCompParams* comp);
void ah_io_set_output_limiter(AHIO* io, int output, const AHLimiterParams* limiter);
void ah_io_set_input_delay(AHIO* io, int input, const AHDelayParams* delay);
void ah_io_set_channel_pan(AHIO* io, int input, float pan);

// Insert Audio Unit per input. Hanya bisa dimuat/dilepas saat IO berhenti.
// Mengembalikan 0 jika berhasil, -1 jika gagal, -2 jika IO masih berjalan.
int ah_io_insert_load(AHIO* io, int input, const char* effectName);
int ah_io_insert_unload(AHIO* io, int input);
// Nama insert yang dimuat, atau string kosong.
void ah_io_insert_name(AHIO* io, int input, char* buf, int len);
// Daftar effect AU yang tersedia, dipisahkan '\n'. Mengembalikan jumlah byte yang ditulis.
int ah_io_list_effects(char* buf, int len);

// Merekam satu output (mono) ke berkas WAV float 32-bit. Penulisan dilakukan di thread terpisah.
// Mengembalikan 0 jika berhasil, -1 jika gagal.
int ah_io_record_start(AHIO* io, int output, const char* path);
void ah_io_record_stop(AHIO* io);
// Jumlah frame yang sudah ditulis dan jumlah frame yang terbuang karena penulis tertinggal.
void ah_io_record_stats(AHIO* io, uint64_t* written, uint64_t* dropped);

// Peak blok terakhir: inPeaks berukuran inputs, outPeaks berukuran outputs.
void ah_io_peaks(AHIO* io, float* inPeaks, float* outPeaks);
void ah_io_stats(AHIO* io, AHIOStats* out);

#ifdef __cplusplus
}
#endif

#endif
