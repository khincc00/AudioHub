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
    uint32_t inputLatencyFrames;    // latensi input perangkat (latency + safety offset), dalam frame
    uint32_t outputLatencyFrames;   // latensi output perangkat, dalam frame
} AHIOStats;

// Membuka perangkat yang namanya mengandung deviceNameContains. Mengembalikan NULL jika gagal;
// errBuf berisi pesan singkat.
AHIO* ah_io_open(const char* deviceNameContains, char* errBuf, int errLen);

// Membuat aggregate sementara yang terlihat di Core Audio (UID tetap, DAW bisa memilihnya):
// master = perangkat fisik, sub-device = perangkat virtual (dengan drift compensation).
// Aggregate dihapus saat ah_io_close.
AHIO* ah_io_open_with_virtual(const char* masterNameContains, const char* virtualUID, char* errBuf, int errLen);
// Sama seperti di atas, dengan hingga tiga virtual device (virtual 1, virtual 2, loopback) dan tap aplikasi.
// tapUIDs berisi UID tap (CATapDescription) yang masuk sebagai kanal input; tapCount = jumlahnya. Yang tidak dipakai
// dikirim sebagai NULL atau string kosong; virtualUID1 harus terisi jika ada virtual device.
AHIO* ah_io_open_with_virtuals(const char* masterNameContains, const char* virtualUID1, const char* virtualUID2,
                               const char* virtualUID3, const char* const* tapUIDs, int tapCount,
                               char* errBuf, int errLen);
void ah_io_close(AHIO* io);

// Pilihan buffer dan sample rate untuk pembukaan berikutnya. bufferFrames 0 = default 512.
// sampleRate 0 = pakai rate yang sudah dipasang di perangkat. Tidak berpengaruh pada IO yang sudah berjalan.
void ah_io_set_preferences(uint32_t bufferFrames, double sampleRate);
// Rate nominal yang didukung perangkat dengan nama mengandung deviceNameContains. Mengembalikan jumlah rate,
// atau -1 jika perangkat tidak ditemukan. bufferMin/bufferMax berisi rentang buffer perangkat.
int ah_io_device_capabilities(const char* deviceNameContains, double* rates, int maxRates,
                              uint32_t* bufferMin, uint32_t* bufferMax);
int ah_io_start(AHIO* io);
void ah_io_stop(AHIO* io);

int ah_io_inputs(AHIO* io);
int ah_io_outputs(AHIO* io);

// Mengembalikan 0 jika berhasil, -1 jika indeks kanal tidak valid. Route dari virtual ke virtual dalam
// perangkat yang sama tidak mengirim audio apa pun; statusnya hanya menandai perangkat itu aktif.
int ah_io_set_route(AHIO* io, int input, int output, int enabled, float sendDb);
// 1 jika route boleh dibuat (indeks kanal valid), 0 jika tidak.
int ah_io_route_allowed(AHIO* io, int input, int output);
// Perangkat pemilik kanal: 0 = fisik, 1 = virtual 1, 2 = virtual 2, 3 = virtual 3 (loopback), -1 jika indeks di luar kanal.
// local = nomor kanal di dalam perangkat itu, mulai dari 1. output = 0 untuk input, 1 untuk output.
void ah_io_channel_owner(AHIO* io, int output, int channel, int* owner, int* local);// Jumlah kanal milik perangkat fisik (sisanya kanal virtual) di dalam aggregate.
int ah_io_physical_inputs(AHIO* io);
// 1 jika perangkat masih terpasang dan hidup di Core Audio, 0 jika sudah hilang.
int ah_io_is_alive(AHIO* io);
int ah_io_physical_outputs(AHIO* io);
void ah_io_set_channel(AHIO* io, int input, float gainDb, int mute);

// Kontrol hardware pada perangkat fisik, per kanal output (1-based; 0 = master). Terpisah dari gain software engine.
// get: 0 jika properti tersedia, -1 jika tidak. set: volume < 0 atau mute < 0 berarti tidak diubah.
int ah_io_hw_output_get(AHIO* io, int channel, float* volume, int* mute);
int ah_io_hw_output_set(AHIO* io, int channel, float volume, int mute);

void ah_io_set_output(AHIO* io, int output, float gainDb, int mute);

// Parameter DSP per input (gate, EQ, kompresor) dan limiter per output.
void ah_io_set_input_fx(AHIO* io, int input, const AHGateParams* gate, const AHEqParams* eq, const AHCompParams* comp);
void ah_io_set_output_limiter(AHIO* io, int output, const AHLimiterParams* limiter);
void ah_io_set_input_delay(AHIO* io, int input, const AHDelayParams* delay);
void ah_io_set_channel_pan(AHIO* io, int input, float pan);

// Insert Audio Unit per input. Hanya bisa dimuat/dilepas saat IO berhenti.
// Mengembalikan 0 jika berhasil, -1 jika gagal, -2 jika IO masih berjalan.
// Sama dengan slot 0 pada rantai FX di bawah.
int ah_io_insert_load(AHIO* io, int input, const char* effectName);
int ah_io_insert_unload(AHIO* io, int input);
// Nama insert yang dimuat, atau string kosong.
void ah_io_insert_name(AHIO* io, int input, char* buf, int len);
// Daftar effect AU yang tersedia, dipisahkan '\n'. Mengembalikan jumlah byte yang ditulis.
int ah_io_list_effects(char* buf, int len);
// Mendaftarkan file VST2 (.vst / .dylib) yang boleh dimuat, dipisahkan '\n'. Muncul di daftar sebagai "VST2: <nama file>".
// Dipanggil sebelum ah_io_list_effects agar daftar ikut berubah.
void ah_io_vst2_set_paths(const char* paths);

// Rantai FX per input: hingga AH_FX_SLOTS slot AU, diproses berurutan sebelum engine.
// Slot dimuat dan dilepas hanya saat IO berhenti (-2 jika berjalan).
// Enabled, urutan, dan parameter boleh diubah saat IO berjalan.
#define AH_FX_SLOTS 8

// Muat effect AU ke slot. Mengembalikan 0 jika berhasil, -1 jika gagal, -2 jika IO masih berjalan.
int ah_io_fx_load(AHIO* io, int input, int slot, const char* effectName);
int ah_io_fx_unload(AHIO* io, int input, int slot);
// Mengembalikan 1 jika slot berisi AU, 0 jika kosong, -1 jika indeks salah.
// name diisi nama AU (kosong jika slot kosong). enabled diisi 1 atau 0.
int ah_io_fx_info(AHIO* io, int input, int slot, char* name, int nameLen, int* enabled);
// enabled = 0 melewati slot (bypass). Slot kosong tidak berpengaruh.
int ah_io_fx_set_enabled(AHIO* io, int input, int slot, int enabled);
// order berisi permutasi AH_FX_SLOTS slot: order[k] = slot yang diproses ke-k. -1 jika bukan permutasi.
int ah_io_fx_set_order(AHIO* io, int input, const int* order, int count);

typedef struct AHFxParam {
    unsigned int id;
    char name[96];
    float minValue, maxValue, defaultValue, value;
} AHFxParam;

// Jumlah parameter slot, atau -1 jika slot kosong/indeks salah.
int ah_io_fx_param_count(AHIO* io, int input, int slot);
// Parameter ke-index. 0 jika berhasil, -1 jika tidak.
int ah_io_fx_param_get(AHIO* io, int input, int slot, int index, AHFxParam* out);
// Mengubah nilai parameter (id dari AHFxParam). 0 jika berhasil, -1 jika tidak.
int ah_io_fx_param_set(AHIO* io, int input, int slot, unsigned int paramId, float value);
// Teks tampilan parameter, mis. "-6.0 dB" (hanya VST2). 0 jika ada, -1 jika tidak.
int ah_io_fx_param_text(AHIO* io, int input, int slot, int index, char* buf, int len);

// State AU slot (preset penuh). get: mengembalikan ukuran data, -1 jika gagal; buf NULL untuk mengukur.
// set: 0 jika berhasil, -1 jika gagal, -2 jika IO masih berjalan.
int ah_io_fx_state_get(AHIO* io, int input, int slot, unsigned char* buf, int len);
int ah_io_fx_state_set(AHIO* io, int input, int slot, const unsigned char* data, int len);

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
