// Probe AU insert: memuat efek AU bawaan Apple, memproses buffer, membaca dan mengubah parameter,
// serta menyimpan lalu memulihkan state (ClassInfo). Tanpa perangkat audio. Build dan jalankan:
//   sh tests/integration/run_au_probe.sh
#include "../../backends/coreaudio/io/au_insert.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace audiohub;

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("GAGAL %d: %s\n", __LINE__, #c); fails++; } } while (0)

int main() {
    // Ambil nama persis dari daftar (mis. "Apple: AUDelay"). loadInsert mencocokkan nama persis.
    char names[8192] = {0};
    listEffectNames(names, sizeof names);
    char wanted[256] = {0};
    const char* hit = std::strstr(names, "AUDelay");
    CHECK(hit != nullptr);
    if (!hit) {
        std::printf("GAGAL: AUDelay tidak ada di daftar efek\n");
        return 1;
    }
    const char* lineStart = hit;
    while (lineStart > names && lineStart[-1] != '\n') lineStart--;
    const char* lineEnd = std::strchr(hit, '\n');
    size_t len = (size_t)(lineEnd - lineStart);
    std::memcpy(wanted, lineStart, len < sizeof wanted - 1 ? len : sizeof wanted - 1);

    AUInsert slot;
    const float sr = 48000.0f;
    const int frames = 512;
    CHECK(loadInsert(slot, wanted, sr, frames));
    CHECK(slot.au != nullptr);
    std::printf("AU dimuat: %s\n", slot.name);
    if (!slot.au) return 1;

    // Parameter
    int n = paramCount(slot);
    std::printf("jumlah parameter: %d\n", n);
    CHECK(n > 0);
    ParamInfo p0;
    CHECK(paramInfo(slot, 0, p0));
    CHECK(std::strlen(p0.name) > 0);
    CHECK(p0.minValue <= p0.maxValue);
    std::printf("param[0]: %s  min %.3f  max %.3f  nilai %.3f\n", p0.name, p0.minValue, p0.maxValue, p0.value);
    CHECK(!paramInfo(slot, n, p0));  // di luar rentang

    // Ubah parameter 0 ke titik tengah rentang, lalu baca kembali
    float target = p0.minValue + 0.5f * (p0.maxValue - p0.minValue);
    CHECK(setParamValue(slot, p0.id, target));
    ParamInfo p1;
    CHECK(paramInfo(slot, 0, p1));
    CHECK(std::fabs(p1.value - target) < 1e-3f * (1.0f + std::fabs(target)));

    // Proses buffer: impuls di awal, sisanya nol. Keluaran harus terbatas (tidak NaN/inf).
    std::vector<float> buf(frames, 0.0f);
    buf[0] = 1.0f;
    processInsert(slot, buf.data(), frames);
    bool finite = true;
    for (float v : buf) if (!std::isfinite(v)) finite = false;
    CHECK(finite);

    // State: simpan, ubah parameter, pulihkan, dan pastikan nilainya kembali
    int size = getClassInfo(slot, nullptr, 0);
    std::printf("ukuran state: %d byte\n", size);
    CHECK(size > 0);
    if (size <= 0) {
        unloadInsert(slot);
        std::printf("GAGAL: state tidak bisa diambil\n");
        return 1;
    }
    std::vector<unsigned char> saved((size_t)size);
    CHECK(getClassInfo(slot, saved.data(), size) == size);

    float other = p0.minValue + 0.1f * (p0.maxValue - p0.minValue);
    CHECK(setParamValue(slot, p0.id, other));
    ParamInfo changed;
    CHECK(paramInfo(slot, 0, changed));
    CHECK(std::fabs(changed.value - other) < 1e-3f * (1.0f + std::fabs(other)));

    CHECK(setClassInfo(slot, saved.data(), size));
    ParamInfo restored;
    CHECK(paramInfo(slot, 0, restored));
    CHECK(std::fabs(restored.value - target) < 1e-2f * (1.0f + std::fabs(target)));
    std::printf("setelah pulih: %.3f (target %.3f)\n", restored.value, target);

    unloadInsert(slot);
    CHECK(slot.au == nullptr);
    CHECK(paramCount(slot) == 0);

    if (fails == 0) std::printf("OK: au_insert_probe\n");
    return fails == 0 ? 0 : 1;
}
