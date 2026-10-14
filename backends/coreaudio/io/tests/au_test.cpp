// Uji host AU: daftar effect, muat effect bawaan Apple, dan proses sinyal.
#include "../au_insert.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("GAGAL %d: %s\n", __LINE__, #c); fails++; } } while (0)

int main() {
    char names[8192] = {0};
    int n = audiohub::listEffectNames(names, sizeof names);
    std::printf("effect terdaftar: %d byte\n", n);
    CHECK(n > 0);
    CHECK(std::strstr(names, "Dynamics") != nullptr);
    std::printf("contoh: %.120s...\n", names);

    audiohub::AUInsert slot;
    bool ok = audiohub::loadInsert(slot, "Apple: AUDynamicsProcessor", 48000.0f, 512);
    std::printf("muat Apple: AUDynamicsProcessor: %s\n", ok ? "berhasil" : "gagal");
    CHECK(ok && slot.au != nullptr);
    if (ok) {
        const int frames = 512;
        const int blocks = 40;
        std::vector<float> buf(frames);
        double inRms = 0, outRms = 0;
        bool finite = true;
        for (int b = 0; b < blocks; b++) {
            for (int i = 0; i < frames; i++) {
                double t = (double)(b * frames + i) / 48000.0;
                buf[i] = 0.5f * (float)std::sin(2.0 * 3.14159265358979 * 1000.0 * t);
            }
            if (b >= blocks / 2) for (float v : buf) inRms += (double)v * v;
            audiohub::processInsert(slot, buf.data(), frames);
            if (b >= blocks / 2) for (float v : buf) { outRms += (double)v * v; if (!std::isfinite(v)) finite = false; }
        }
        double inDb = 10.0 * std::log10(inRms / ((blocks / 2) * frames));
        double outDb = 10.0 * std::log10(outRms / ((blocks / 2) * frames));
        std::printf("sinyal: masuk %.2f dB, keluar %.2f dB\n", inDb, outDb);
        CHECK(finite);
        CHECK(std::isfinite(outDb));
        CHECK(outDb < inDb + 0.5);   // dinamika tidak menaikkan level di atas masukan
        audiohub::unloadInsert(slot);
        CHECK(slot.au == nullptr);
    }
    std::printf("%s (%d gagal)\n", fails == 0 ? "semua lulus" : "ADA KEGAGALAN", fails);
    return fails != 0;
}
