// Host plugin untuk insert per input: Audio Unit (AU) dan VST 2.4 (file .vst / .dylib).
// Pemakaian: insert dimuat dan dilepas hanya saat IO berhenti.
#ifndef AUDIOHUB_AU_INSERT_H
#define AUDIOHUB_AU_INSERT_H

#include <AudioToolbox/AudioToolbox.h>

namespace audiohub {

// Slot VST2 disimpan di sisi .cpp; header hanya memegang pointer-nya.
struct Vst2Slot;

struct AUInsert {
    AudioUnit au = nullptr;
    // Slot VST2. Jika tidak null, slot ini memakai VST2 dan au diabaikan.
    Vst2Slot* vst = nullptr;
    float inScratch[4096];
    float* source = nullptr;
    AudioTimeStamp ts{};
    char name[128] = {0};
};

// Apakah slot berisi plugin (AU atau VST2).
inline bool isLoaded(const AUInsert& slot) { return slot.au != nullptr || slot.vst != nullptr; }

// Daftar nama effect yang terpasang (AU bawaan dan AU sistem, lalu VST2 yang didaftarkan), dipisahkan '\n'.
int listEffectNames(char* buf, int len);
// Mendaftarkan file VST2 yang boleh dimuat. paths berisi path dipisahkan '\n'. Nama effect-nya "VST2: <nama file>".
void setVst2Paths(const char* paths);
// Memuat effect dengan nama tersebut. Mengembalikan true jika berhasil.
bool loadInsert(AUInsert& slot, const char* name, float sampleRate, int maxFrames);
void unloadInsert(AUInsert& slot);
// Memproses buffer mono float in-place. Tidak mengalokasikan memori.
void processInsert(AUInsert& slot, float* buf, int frames);

// Parameter plugin (AU dan VST2). Dipanggil dari thread UI, bukan dari jalur audio.
struct ParamInfo {
    unsigned int id = 0;
    char name[96] = {0};
    float minValue = 0, maxValue = 0, defaultValue = 0, value = 0;
};
// Jumlah parameter, atau 0 jika slot kosong.
int paramCount(const AUInsert& slot);
// Parameter ke-index dari daftar parameter. false jika di luar rentang atau gagal dibaca.
bool paramInfo(const AUInsert& slot, int index, ParamInfo& out);
bool setParamValue(AUInsert& slot, unsigned int id, float value);
// Teks tampilan parameter (mis. "-6.0 dB"). Hanya VST2. false jika plugin tidak menyediakannya.
bool paramText(const AUInsert& slot, int index, char* buf, int len);

// State plugin sebagai blob biner. AU: property list (kAudioUnitProperty_ClassInfo). VST2: nilai semua parameter.
// Mengembalikan ukuran data, atau -1 jika gagal. Data hanya disalin jika len cukup; buf NULL dipakai untuk mengukur.
int getClassInfo(const AUInsert& slot, unsigned char* buf, int len);
// Memulihkan state dari data hasil getClassInfo. Hanya untuk slot yang sudah dimuat.
bool setClassInfo(AUInsert& slot, const unsigned char* data, int len);

}  // namespace audiohub

#endif
