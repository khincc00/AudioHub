#include "dsp.h"

#include <cmath>
#include <cstddef>
#include <vector>

namespace audiohub {

namespace {
constexpr double kPi = 3.14159265358979323846;

float coeffFromMs(float fs, float ms) {
    if (ms <= 0.0f) return 0.0f;
    return std::exp(-1.0f / (ms * 0.001f * fs));
}
}  // namespace

float dbToLinear(float db) { return std::pow(10.0f, db / 20.0f); }
float linearToDb(float v) { return v > 1e-9f ? 20.0f * std::log10(v) : -180.0f; }

// ---- Biquad ----

void Biquad::setBypass() {
    b0 = 1; b1 = 0; b2 = 0; a1 = 0; a2 = 0;
}

void Biquad::setPeak(float fs, float freq, float gainDb, float q) {
    double A = std::pow(10.0, gainDb / 40.0);
    double w0 = 2.0 * kPi * freq / fs;
    double alpha = std::sin(w0) / (2.0 * q);
    double cs = std::cos(w0);
    double a0 = 1.0 + alpha / A;
    b0 = (1.0 + alpha * A) / a0;
    b1 = (-2.0 * cs) / a0;
    b2 = (1.0 - alpha * A) / a0;
    a1 = (-2.0 * cs) / a0;
    a2 = (1.0 - alpha / A) / a0;
}

void Biquad::setLowShelf(float fs, float freq, float gainDb) {
    double A = std::pow(10.0, gainDb / 40.0);
    double w0 = 2.0 * kPi * freq / fs;
    double cs = std::cos(w0);
    double alpha = std::sin(w0) / 2.0 * std::sqrt(2.0);   // S = 1
    double sA = 2.0 * std::sqrt(A) * alpha;
    double a0 = (A + 1) + (A - 1) * cs + sA;
    b0 = A * ((A + 1) - (A - 1) * cs + sA) / a0;
    b1 = 2.0 * A * ((A - 1) - (A + 1) * cs) / a0;
    b2 = A * ((A + 1) - (A - 1) * cs - sA) / a0;
    a1 = -2.0 * ((A - 1) + (A + 1) * cs) / a0;
    a2 = ((A + 1) + (A - 1) * cs - sA) / a0;
}

void Biquad::setHighShelf(float fs, float freq, float gainDb) {
    double A = std::pow(10.0, gainDb / 40.0);
    double w0 = 2.0 * kPi * freq / fs;
    double cs = std::cos(w0);
    double alpha = std::sin(w0) / 2.0 * std::sqrt(2.0);
    double sA = 2.0 * std::sqrt(A) * alpha;
    double a0 = (A + 1) - (A - 1) * cs + sA;
    b0 = A * ((A + 1) + (A - 1) * cs + sA) / a0;
    b1 = -2.0 * A * ((A - 1) + (A + 1) * cs) / a0;
    b2 = A * ((A + 1) + (A - 1) * cs - sA) / a0;
    a1 = 2.0 * ((A - 1) - (A + 1) * cs) / a0;
    a2 = ((A + 1) - (A - 1) * cs - sA) / a0;
}

float Biquad::process(float x) {
    double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
    x2 = x1; x1 = x;
    y2 = y1; y1 = y;
    return static_cast<float>(y);
}

// ---- Gate ----

void Gate::configure(float sampleRate, const GateParams& p) {
    enabled = p.enabled;
    fs = sampleRate;
    thresh = p.thresholdDb;
    attackC = coeffFromMs(fs, p.attackMs);
    releaseC = coeffFromMs(fs, p.releaseMs);
    holdSamples = p.holdMs * 0.001f * fs;
    envRel = coeffFromMs(fs, 10.0f);
}

void Gate::process(float* buf, int n) {
    if (!enabled) return;
    for (int i = 0; i < n; i++) {
        float x = std::fabs(buf[i]);
        env = x > env ? x : env * envRel;
        float levelDb = linearToDb(env);
        if (levelDb > thresh) {
            open = true;
            holdCounter = holdSamples;
        } else if (holdCounter > 0) {
            holdCounter -= 1;
        } else {
            open = false;
        }
        float target = open ? 1.0f : 0.0f;
        if (target > gain) gain = attackC * gain + (1.0f - attackC) * target;
        else gain = releaseC * gain + (1.0f - releaseC) * target;
        buf[i] *= gain;
    }
}

void Gate::reset() { env = 0; gain = 0; open = false; holdCounter = 0; }

// ---- EQ ----

void Equalizer::configure(float fs, const EqParams& p) {
    enabled = p.enabled;
    if (!enabled) {
        low.setBypass(); mid.setBypass(); high.setBypass();
        return;
    }
    low.setLowShelf(fs, p.lowHz, p.lowDb);
    mid.setPeak(fs, p.midHz, p.midDb, p.midQ);
    high.setHighShelf(fs, p.highHz, p.highDb);
}

void Equalizer::process(float* buf, int n) {
    if (!enabled) return;
    for (int i = 0; i < n; i++) {
        buf[i] = high.process(mid.process(low.process(buf[i])));
    }
}

void Equalizer::reset() { low.reset(); mid.reset(); high.reset(); }

// ---- Kompresor ----

void Compressor::configure(float fs, const CompParams& p) {
    enabled = p.enabled;
    thresh = p.thresholdDb;
    ratio = p.ratio < 1.0f ? 1.0f : p.ratio;
    attackC = coeffFromMs(fs, p.attackMs);
    releaseC = coeffFromMs(fs, p.releaseMs);
    makeup = p.makeupDb;
    knee = p.kneeDb > 0.0f ? p.kneeDb : 0.0f;
}

void Compressor::process(float* buf, int n, const float* key) {
    if (!enabled) return;
    for (int i = 0; i < n; i++) {
        float x = std::fabs(key ? key[i] : buf[i]);
        // Pendeteksi level: attack saat naik, release saat turun
        env = x > env ? attackC * env + (1.0f - attackC) * x : releaseC * env + (1.0f - releaseC) * x;
        float levelDb = linearToDb(env);
        float over = levelDb - thresh;
        float slope = 1.0f - 1.0f / ratio;
        float grTarget = 0.0f;
        if (knee > 0.0f && over > -knee * 0.5f && over < knee * 0.5f) {
            // Soft knee: transisi kuadratik di sekitar threshold
            float t = over + knee * 0.5f;
            grTarget = t * t / (2.0f * knee) * slope;
        } else if (over >= knee * 0.5f) {
            grTarget = over * slope;
        }
        if (grTarget > gr) gr = attackC * gr + (1.0f - attackC) * grTarget;
        else gr = releaseC * gr + (1.0f - releaseC) * grTarget;
        buf[i] *= dbToLinear(makeup - gr);
    }
}

void Compressor::reset() { env = 0; gr = 0; }

// ---- Delay ----

void Delay::allocate(int maxSamples) {
    line.assign(static_cast<size_t>(maxSamples > 1 ? maxSamples : 1), 0.0f);
    w = 0;
    d = 0;
}

void Delay::configure(float fs, const DelayParams& p) {
    enabled = p.enabled;
    float ms = p.delayMs < 0.0f ? 0.0f : (p.delayMs > kMaxDelayMs ? kMaxDelayMs : p.delayMs);
    int samples = static_cast<int>(ms * 0.001f * fs + 0.5f);
    int cap = static_cast<int>(line.size());
    if (samples >= cap) samples = cap - 1;
    d = samples;
}

void Delay::process(float* buf, int n) {
    if (!enabled || line.empty()) return;
    int cap = static_cast<int>(line.size());
    for (int i = 0; i < n; i++) {
        float x = buf[i];
        if (d == 0) {
            buf[i] = x;
            continue;
        }
        int r = w - d;
        if (r < 0) r += cap;
        buf[i] = line[static_cast<size_t>(r)];
        line[static_cast<size_t>(w)] = x;
        w = (w + 1 == cap) ? 0 : w + 1;
    }
}

void Delay::reset() {
    for (auto& v : line) v = 0.0f;
    w = 0;
}

// ---- Limiter ----

void Limiter::allocate() {
    cap = kMaxLookaheadSamples + 1;
    xr.assign(cap, 0.0f);
    tr.assign(cap, 1.0f);
    dq.assign(cap, 0);
    dqHead = dqTail = 0;
    pushed = 0;
}

void Limiter::configure(float fs, const LimiterParams& p) {
    enabled = p.enabled;
    ceiling = dbToLinear(p.ceilingDb);
    releaseC = coeffFromMs(fs, p.releaseMs);
    int la = static_cast<int>(p.lookaheadMs * 0.001f * fs + 0.5f);
    if (la < 0) la = 0;
    if (la > kMaxLookaheadSamples) la = kMaxLookaheadSamples;
    if (la != lookahead) {
        lookahead = la;
        reset();
    }
}

void Limiter::process(float* buf, int n) {
    if (!enabled) return;
    if (lookahead == 0 || cap == 0) {
        for (int i = 0; i < n; i++) {
            float x = std::fabs(buf[i]);
            // Gain harus cukup kecil untuk sampel ini (attack instan, tanpa lookahead)
            float target = x > ceiling ? ceiling / x : 1.0f;
            float recover = gain + (1.0f - releaseC) * (1.0f - gain);
            gain = recover < target ? recover : target;
            buf[i] *= gain;
        }
        return;
    }
    for (int i = 0; i < n; i++) {
        float x = buf[i];
        float ax = std::fabs(x);
        float target = ax > ceiling ? ceiling / ax : 1.0f;
        // Masukkan sampel baru ke ring dan deque minimum (window = lookahead + 1)
        long long t = pushed;
        int slot = static_cast<int>(t % cap);
        xr[slot] = x;
        tr[slot] = target;
        while (dqTail > dqHead && tr[static_cast<int>(dq[(dqTail - 1) % cap] % cap)] >= target) dqTail--;
        dq[dqTail % cap] = t;
        dqTail++;
        pushed++;
        // Sampel keluaran: t - lookahead. Buang indeks yang sudah di luar window.
        long long o = t - lookahead;
        if (o < 0) {
            buf[i] = 0.0f;   // masa pengisian awal (latensi)
            continue;
        }
        while (dqTail > dqHead && dq[dqHead % cap] < o) dqHead++;
        float need = tr[static_cast<int>(dq[dqHead % cap] % cap)];
        float recover = gain + (1.0f - releaseC) * (1.0f - gain);
        gain = recover < need ? recover : need;
        buf[i] = xr[static_cast<int>(o % cap)] * gain;
    }
}

void Limiter::reset() {
    gain = 1.0f;
    pushed = 0;
    dqHead = dqTail = 0;
    for (auto& v : xr) v = 0.0f;
    for (auto& v : tr) v = 1.0f;
}

}  // namespace audiohub
