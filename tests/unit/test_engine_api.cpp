// Uji API C engine: pan per input dan sumber sidechain lewat AHCompParams.
#include "../../engine/api/audiohub_engine.h"

#include <cmath>
#include <cstdio>
#include <vector>

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("GAGAL %d: %s\n", __LINE__, #c); fails++; } } while (0)

static void run(AHEngine* e, float in0, float in1, int frames, std::vector<float>& o0, std::vector<float>& o1) {
    std::vector<float> a(frames, in0), b(frames, in1), y0(frames, 0), y1(frames, 0);
    const float* ins[2] = {a.data(), b.data()};
    float* outs[2] = {y0.data(), y1.data()};
    ah_engine_process(e, ins, outs, frames);
    o0 = y0; o1 = y1;
}

int main() {
    // Pan: input 0 ke pasangan (out 0, out 1) dengan pan −1 → hanya kiri
    AHEngine* e = ah_engine_create(2, 2, 512);
    ah_engine_set_route(e, 0, 0, 1, 0.0f);
    ah_engine_set_route(e, 0, 1, 1, 0.0f);
    std::vector<float> o0, o1;
    ah_engine_set_channel_pan(e, 0, -1.0f);
    run(e, 0.5f, 0.0f, 256, o0, o1);
    run(e, 0.5f, 0.0f, 256, o0, o1);
    CHECK(std::fabs(o0[255] - 0.5f) < 1e-3f);
    CHECK(std::fabs(o1[255]) < 1e-3f);

    // Pan +1 → hanya kanan
    ah_engine_set_channel_pan(e, 0, 1.0f);
    run(e, 0.5f, 0.0f, 256, o0, o1);
    run(e, 0.5f, 0.0f, 256, o0, o1);
    CHECK(std::fabs(o0[255]) < 1e-3f);
    CHECK(std::fabs(o1[255] - 0.5f) < 1e-3f);

    // Pan tengah → kedua sisi 0 dB (tanpa perubahan)
    ah_engine_set_channel_pan(e, 0, 0.0f);
    run(e, 0.5f, 0.0f, 256, o0, o1);
    run(e, 0.5f, 0.0f, 256, o0, o1);
    CHECK(std::fabs(o0[255] - 0.5f) < 1e-3f);
    CHECK(std::fabs(o1[255] - 0.5f) < 1e-3f);
    ah_engine_destroy(e);

    // Sidechain: input 1 (lemah) dikompresi oleh key input 0 (keras)
    AHEngine* s = ah_engine_create(2, 1, 4096);
    ah_engine_set_route(s, 1, 0, 1, 0.0f);
    AHGateParams gate = {0, -80, 2, 100, 50};
    AHEqParams eq = {0, 0, 120, 0, 1000, 1, 0, 8000};
    AHCompParams comp = {1, -20.0f, 10.0f, 1.0f, 50.0f, 0.0f, 0.0f, 0};   // sidechain dari input 0
    ah_engine_set_input_fx(s, 1, &gate, &eq, &comp);
    AHLimiterParams lim = {0, -1.0f, 50.0f, 0.0f};
    ah_engine_set_output_limiter(s, 0, &lim);
    std::vector<float> key(4096), sig(4096), out(4096, 0.0f);
    for (int i = 0; i < 4096; i++) {
        key[i] = 0.5f * std::sin(2.0 * 3.14159265 * 100.0 * i / 48000.0);
        sig[i] = 0.05f * std::sin(2.0 * 3.14159265 * 1000.0 * i / 48000.0);
    }
    const float* ins[2] = {key.data(), sig.data()};
    float* outs[1] = {out.data()};
    ah_engine_process(s, ins, outs, 4096);
    double withKey = 0;
    for (int i = 2048; i < 4096; i++) withKey += out[i] * out[i];
    withKey = std::sqrt(withKey / 2048.0);

    // Pembanding: sidechain dimatikan (sumber −1)
    AHCompParams noKey = comp;
    noKey.sidechainInput = -1;
    ah_engine_set_input_fx(s, 1, &gate, &eq, &noKey);
    ah_engine_process(s, ins, outs, 4096);
    ah_engine_process(s, ins, outs, 4096);
    double withoutKey = 0;
    for (int i = 2048; i < 4096; i++) withoutKey += out[i] * out[i];
    withoutKey = std::sqrt(withoutKey / 2048.0);
    std::printf("  sidechain: RMS dengan key %.4f, tanpa key %.4f\n", withKey, withoutKey);
    CHECK(withKey < withoutKey * 0.7);
    ah_engine_destroy(s);

    std::printf("%s (%d gagal)\n", fails == 0 ? "API engine: semua lulus" : "API engine: ADA KEGAGALAN", fails);
    return fails != 0;
}
