// Uji unit DSP: EQ, kompresor, limiter, dan noise gate. Dijalankan lewat tests/unit/run.sh.
#include "../../engine/dsp/dsp.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace audiohub;

static int checks = 0, failures = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { std::printf("GAGAL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static const float kFs = 48000.0f;

static double rmsDb(const std::vector<float>& v, size_t from, size_t to) {
    double s = 0;
    for (size_t i = from; i < to; i++) s += (double)v[i] * v[i];
    double r = std::sqrt(s / (double)(to - from));
    return r > 0 ? 20.0 * std::log10(r) : -200.0;
}

static std::vector<float> sine(float freq, float amp, int n) {
    std::vector<float> v(n);
    for (int i = 0; i < n; i++) v[i] = amp * std::sin(2.0 * 3.14159265358979 * freq * i / kFs);
    return v;
}

// EQ: peak +6 dB pada 1 kHz harus menghasilkan kira-kira +6 dB pada sinus 1 kHz
static void testEqPeakBoost() {
    Equalizer eq;
    EqParams p;
    p.enabled = true;
    p.midDb = 6.0f; p.midHz = 1000.0f; p.midQ = 1.0f;
    eq.configure(kFs, p);
    auto x = sine(1000.0f, 0.1f, 48000);
    std::vector<float> y = x;
    eq.process(y.data(), (int)y.size());
    double gainDb = rmsDb(y, 24000, 48000) - rmsDb(x, 24000, 48000);
    CHECK(std::fabs(gainDb - 6.0) < 0.3);
}

// EQ nonaktif harus bypass sempurna
static void testEqBypass() {
    Equalizer eq;
    EqParams p;
    p.enabled = false;
    p.midDb = 12.0f;
    eq.configure(kFs, p);
    auto x = sine(1000.0f, 0.1f, 4800);
    std::vector<float> y = x;
    eq.process(y.data(), (int)y.size());
    bool same = true;
    for (size_t i = 0; i < x.size(); i++) if (y[i] != x[i]) same = false;
    CHECK(same);
}

// Shelf: low shelf +6 dB harus memberi gain besar di 50 Hz dan kecil di 8 kHz
static void testLowShelf() {
    Equalizer eq;
    EqParams p;
    p.enabled = true;
    p.lowDb = 6.0f; p.lowHz = 120.0f;
    eq.configure(kFs, p);
    auto lo = sine(50.0f, 0.1f, 48000), hi = sine(8000.0f, 0.1f, 48000);
    auto ylo = lo, yhi = hi;
    eq.process(ylo.data(), (int)ylo.size());
    eq.reset();
    eq.process(yhi.data(), (int)yhi.size());
    double gLo = rmsDb(ylo, 24000, 48000) - rmsDb(lo, 24000, 48000);
    double gHi = rmsDb(yhi, 24000, 48000) - rmsDb(hi, 24000, 48000);
    CHECK(gLo > 4.5);
    CHECK(std::fabs(gHi) < 0.5);
}

// Kompresor: input −6 dBFS, threshold −20, rasio 4 → puncak keluaran ≈ −16,5 dBFS
static void testCompressor() {
    Compressor c;
    CompParams p;
    p.enabled = true;
    p.thresholdDb = -20.0f; p.ratio = 4.0f; p.attackMs = 1.0f; p.releaseMs = 50.0f; p.makeupDb = 0.0f;
    c.configure(kFs, p);
    auto x = sine(1000.0f, 0.5f, 48000);
    std::vector<float> y = x;
    c.process(y.data(), (int)y.size());
    double outDb = rmsDb(y, 24000, 48000);
    double inDb = rmsDb(x, 24000, 48000);
    // Puncak sinus = RMS + 3 dB; target keluaran puncak −16,5 dBFS → RMS ≈ −19,5 dB
    std::printf("  kompresor: selisih RMS %.2f dB (target -10,5 dB)\n", outDb - inDb);
    CHECK(std::fabs((outDb - inDb) + 10.5) < 1.0);
    CHECK(outDb < inDb);
}

// Kompresor dengan makeup 6 dB harus menaikkan level di atas threshold tanpa melewati batas
static void testCompressorMakeup() {
    Compressor c;
    CompParams p;
    p.enabled = true;
    p.thresholdDb = -20.0f; p.ratio = 1.0f; p.makeupDb = 6.0f;   // rasio 1 = tanpa kompresi
    c.configure(kFs, p);
    auto x = sine(1000.0f, 0.1f, 48000);
    std::vector<float> y = x;
    c.process(y.data(), (int)y.size());
    double gainDb = rmsDb(y, 24000, 48000) - rmsDb(x, 24000, 48000);
    CHECK(std::fabs(gainDb - 6.0) < 0.2);
}

// Limiter: input 0 dBFS dengan ceiling −6 dB → keluaran tidak boleh melewati 0,5012
static void testLimiter() {
    Limiter l;
    LimiterParams p;
    p.enabled = true;
    p.ceilingDb = -6.0f; p.releaseMs = 50.0f;
    l.configure(kFs, p);
    auto x = sine(1000.0f, 1.0f, 48000);
    std::vector<float> y = x;
    l.process(y.data(), (int)y.size());
    float peak = 0;
    for (float v : y) if (std::fabs(v) > peak) peak = std::fabs(v);
    CHECK(peak <= 0.5012f + 1e-4f);
    CHECK(peak > 0.45f);   // tetap menahan, bukan memotong habis
}

// Limiter di bawah ceiling tidak boleh mengubah sinyal
static void testLimiterTransparent() {
    Limiter l;
    LimiterParams p;
    p.enabled = true;
    p.ceilingDb = -1.0f;
    l.configure(kFs, p);
    auto x = sine(1000.0f, 0.1f, 4800);
    std::vector<float> y = x;
    l.process(y.data(), (int)y.size());
    double maxDiff = 0;
    for (size_t i = 0; i < x.size(); i++) maxDiff = std::fmax(maxDiff, std::fabs(y[i] - x[i]));
    CHECK(maxDiff < 1e-5);
}

// Noise gate: noise −60 dBFS di bawah threshold −40 → keluaran hampir nol setelah release
static void testGateClosesOnNoise() {
    Gate g;
    GateParams p;
    p.enabled = true;
    p.thresholdDb = -40.0f; p.attackMs = 2.0f; p.releaseMs = 50.0f; p.holdMs = 10.0f;
    g.configure(kFs, p);
    std::vector<float> y(48000, 0.001f);
    g.process(y.data(), (int)y.size());
    float peak = 0;
    for (size_t i = 24000; i < y.size(); i++) if (std::fabs(y[i]) > peak) peak = std::fabs(y[i]);
    CHECK(peak < 1e-5f);
}

// Noise gate: sinyal −20 dBFS di atas threshold −40 → lolos hampir utuh
static void testGateOpensOnSignal() {
    Gate g;
    GateParams p;
    p.enabled = true;
    p.thresholdDb = -40.0f; p.attackMs = 2.0f; p.releaseMs = 50.0f; p.holdMs = 10.0f;
    g.configure(kFs, p);
    auto x = sine(1000.0f, 0.1f, 48000);
    std::vector<float> y = x;
    g.process(y.data(), (int)y.size());
    double diffDb = rmsDb(y, 24000, 48000) - rmsDb(x, 24000, 48000);
    CHECK(std::fabs(diffDb) < 0.5);
}

// Noise gate nonaktif harus bypass: noise −60 dBFS tetap lewat utuh
static void testGateBypass() {
    Gate g;
    GateParams p;
    p.enabled = false;
    p.thresholdDb = -40.0f;
    g.configure(kFs, p);
    std::vector<float> y(4800, 0.001f);
    g.process(y.data(), (int)y.size());
    bool same = true;
    for (float v : y) if (v != 0.001f) same = false;
    CHECK(same);
}

// Delay 10 ms: impuls di indeks 0 harus muncul di indeks 480 (48 kHz)
static void testDelay() {
    Delay d;
    d.allocate(48000);
    DelayParams p;
    p.enabled = true;
    p.delayMs = 10.0f;
    d.configure(kFs, p);
    std::vector<float> y(2000, 0.0f);
    y[0] = 1.0f;
    d.process(y.data(), (int)y.size());
    int at = -1;
    for (int i = 0; i < (int)y.size(); i++) if (y[i] != 0.0f) { at = i; break; }
    CHECK(at == 480);
    CHECK(std::fabs(y[480] - 1.0f) < 1e-6f);
}

// Delay nonaktif harus bypass
static void testDelayBypass() {
    Delay d;
    d.allocate(48000);
    DelayParams p;
    p.enabled = false;
    p.delayMs = 10.0f;
    d.configure(kFs, p);
    std::vector<float> y(100, 0.0f);
    y[0] = 1.0f;
    d.process(y.data(), (int)y.size());
    CHECK(y[0] == 1.0f);
}

// Soft knee: pada +1 dB di atas threshold, kompresi soft knee lebih kuat daripada hard knee
static void testSoftKnee() {
    auto run = [](float knee) {
        Compressor c;
        CompParams p;
        p.enabled = true;
        p.thresholdDb = -21.0f; p.ratio = 4.0f; p.attackMs = 1.0f; p.releaseMs = 50.0f; p.kneeDb = knee;
        c.configure(kFs, p);
        auto x = sine(1000.0f, 0.1f, 48000);   // puncak −20 dBFS, 1 dB di atas threshold
        c.process(x.data(), (int)x.size());
        return rmsDb(x, 24000, 48000);
    };
    double hard = run(0.0f);
    double soft = run(6.0f);
    std::printf("  knee: hard %.2f dB, soft %.2f dB\n", hard, soft);
    CHECK(soft < hard - 0.15);
}

// Limiter lookahead: sinus 0 dBFS, ceiling −6 dB, lookahead 5 ms → puncak setelah masa isi ≤ batas
static void testLimiterLookahead() {
    Limiter l;
    l.allocate();
    LimiterParams p;
    p.enabled = true;
    p.ceilingDb = -6.0f; p.releaseMs = 50.0f; p.lookaheadMs = 5.0f;
    l.configure(kFs, p);
    auto x = sine(1000.0f, 1.0f, 48000);
    std::vector<float> y = x;
    l.process(y.data(), (int)y.size());
    float peak = 0;
    for (size_t i = 1024; i < y.size(); i++) if (std::fabs(y[i]) > peak) peak = std::fabs(y[i]);
    CHECK(peak <= 0.5012f + 1e-4f);
    CHECK(peak > 0.45f);
    CHECK(l.latencySamples() == 240);
}

// Limiter lookahead: impuls tunggal di atas batas harus tetap di bawah ceiling
static void testLimiterLookaheadImpulse() {
    Limiter l;
    l.allocate();
    LimiterParams p;
    p.enabled = true;
    p.ceilingDb = -6.0f; p.releaseMs = 50.0f; p.lookaheadMs = 5.0f;
    l.configure(kFs, p);
    std::vector<float> y(4000, 0.0f);
    y[1000] = 1.0f;
    l.process(y.data(), (int)y.size());
    float peak = 0;
    for (float v : y) if (std::fabs(v) > peak) peak = std::fabs(v);
    CHECK(peak <= 0.5012f + 1e-4f);
}

// Sidechain: sinyal lemah (−26 dB di bawah threshold) harus tetap dikompresi oleh key yang keras
static void testSidechain() {
    auto run = [](bool useKey) {
        Compressor c;
        CompParams p;
        p.enabled = true;
        p.thresholdDb = -20.0f; p.ratio = 10.0f; p.attackMs = 1.0f; p.releaseMs = 50.0f;
        p.useSidechain = useKey;
        c.configure(kFs, p);
        auto x = sine(1000.0f, 0.05f, 48000);
        auto key = sine(100.0f, 0.5f, 48000);
        c.process(x.data(), (int)x.size(), useKey ? key.data() : nullptr);
        return rmsDb(x, 24000, 48000);
    };
    double plain = run(false);
    double keyed = run(true);
    std::printf("  sidechain: tanpa key %.2f dB, dengan key %.2f dB\n", plain, keyed);
    CHECK(keyed < plain - 6.0);
}

int main() {
    testLimiterLookahead();
    testLimiterLookaheadImpulse();
    testSidechain();
    testDelay();
    testDelayBypass();
    testSoftKnee();
    testGateBypass();
    testEqPeakBoost();
    testEqBypass();
    testLowShelf();
    testCompressor();
    testCompressorMakeup();
    testLimiter();
    testLimiterTransparent();
    testGateClosesOnNoise();
    testGateOpensOnSignal();
    std::printf("DSP: %d cek, %d gagal\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
