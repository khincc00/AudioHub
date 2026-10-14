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

}  // namespace audiohub

#endif
