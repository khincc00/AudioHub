// Host Audio Unit (AU) untuk insert per input. Plugin VST3 belum didukung.
// Pemakaian: insert dimuat dan dilepas hanya saat IO berhenti.
#ifndef AUDIOHUB_AU_INSERT_H
#define AUDIOHUB_AU_INSERT_H

#include <AudioToolbox/AudioToolbox.h>

namespace audiohub {

struct AUInsert {
    AudioUnit au = nullptr;
    float inScratch[4096];
    float* source = nullptr;
    AudioTimeStamp ts{};
    char name[128] = {0};
};

// Daftar nama effect AU yang terpasang (termasuk bawaan Apple), dipisahkan '\n'.
int listEffectNames(char* buf, int len);
// Memuat effect dengan nama tersebut. Mengembalikan true jika berhasil.
bool loadInsert(AUInsert& slot, const char* name, float sampleRate, int maxFrames);
void unloadInsert(AUInsert& slot);
// Memproses buffer mono float in-place. Tidak mengalokasikan memori.
void processInsert(AUInsert& slot, float* buf, int frames);

// Parameter AU (scope global). Dipanggil dari thread UI, bukan dari jalur audio.
struct ParamInfo {
    unsigned int id = 0;
    char name[96] = {0};
    float minValue = 0, maxValue = 0, defaultValue = 0, value = 0;
};
// Jumlah parameter, atau 0 jika slot kosong.
int paramCount(const AUInsert& slot);
// Parameter ke-index dari daftar parameter AU. false jika di luar rentang atau gagal dibaca.
bool paramInfo(const AUInsert& slot, int index, ParamInfo& out);
bool setParamValue(AUInsert& slot, unsigned int id, float value);

// State AU (kAudioUnitProperty_ClassInfo) sebagai property list biner. Mengembalikan ukuran data,
// atau -1 jika gagal. Data hanya disalin jika len cukup; buf NULL dipakai untuk mengukur.
int getClassInfo(const AUInsert& slot, unsigned char* buf, int len);
// Memulihkan state dari data hasil getClassInfo. Hanya untuk slot yang sudah dimuat.
bool setClassInfo(AUInsert& slot, const unsigned char* data, int len);

}  // namespace audiohub

#endif
