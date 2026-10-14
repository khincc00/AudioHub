// Uji langsung: buka perangkat, atur route dan gain, jalankan beberapa detik, laporkan statistik.
// Pemakaian: ./smoke "Maono" [detik=10]
#include "../audiohub_io.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static double db(float v) { return v > 0 ? 20.0 * log10((double)v) : -999.0; }

int main(int argc, char** argv) {
    const char* name = argc > 1 ? argv[1] : "Maono";
    int seconds = argc > 2 ? atoi(argv[2]) : 10;
    const char* virtualUID = argc > 3 ? argv[3] : NULL;
    char err[256] = {0};
    AHIO* io = virtualUID ? ah_io_open_with_virtual(name, virtualUID, err, sizeof err) : ah_io_open(name, err, sizeof err);
    if (!io) { printf("gagal: %s\n", err); return 1; }
    int nIn = ah_io_inputs(io), nOut = ah_io_outputs(io);
    printf("perangkat %s: %d in, %d out\n", name, nIn, nOut);
    // Default: fisik → fisik saja (input i → output i). Route virtual → virtual ditolak backend.
    int pIn = ah_io_physical_inputs(io), pOut = ah_io_physical_outputs(io);
    printf("fisik: %d in, %d out\n", pIn, pOut);
    for (int i = 0; i < pIn && i < pOut; i++) ah_io_set_route(io, i, i, 1, 0.0f);
    // Uji tolak: virtual → virtual harus ditolak
    if (nIn > pIn && nOut > pOut) {
        int r = ah_io_set_route(io, nIn - 1, nOut - 1, 1, 0.0f);
        printf("route virtual→virtual: %s\n", r == -1 ? "ditolak (benar)" : "DITERIMA (salah!)");
    }
    // Mode receive: AudioHub in 1 (virtual, indeks pIn) → PS22 out 1
    if (argc > 4 && strcmp(argv[4], "receive") == 0 && nIn > pIn) {
        ah_io_set_route(io, pIn, 0, 1, 0.0f);
        printf("route virtual in1 → fisik out1 aktif\n");
    }
    if (ah_io_start(io) != 0) { printf("gagal start\n"); ah_io_close(io); return 1; }
    float inP[8] = {0}, outP[8] = {0};
    for (int s = 1; s <= seconds; s++) {
        sleep(1);
        if (s == seconds / 2) ah_io_set_channel(io, 0, -6.0f, 0);
        ah_io_peaks(io, inP, outP);
        AHIOStats st; ah_io_stats(io, &st);
        printf("t=%2d in1=%6.1f out1=%6.1f out2=%6.1f dBFS  cb=%llu drop=%llu hilang=%llu\n",
               s, db(inP[0]), db(outP[0]), nOut > 1 ? db(outP[1]) : -999.0,
               (unsigned long long)st.callbacks, (unsigned long long)st.dropouts, (unsigned long long)st.missingFrames);
    }
    ah_io_stop(io);
    AHIOStats st; ah_io_stats(io, &st);
    printf("ringkasan: buffer=%u rate=%.0f cb=%llu drop=%llu hilang=%llu\n", st.bufferFrames, st.sampleRate,
           (unsigned long long)st.callbacks, (unsigned long long)st.dropouts, (unsigned long long)st.missingFrames);
    ah_io_close(io);
    return st.dropouts == 0 ? 0 : 2;
}
