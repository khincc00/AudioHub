// DSP insert untuk channel dan output: noise gate, EQ 3 band, kompresor, dan limiter.
// Semua proses in-place, tanpa alokasi memori. Koefisien dihitung di setParams, bukan di proses.
#ifndef AUDIOHUB_DSP_H
#define AUDIOHUB_DSP_H

#include <vector>

namespace audiohub {

struct GateParams {
    bool enabled = false;
    float thresholdDb = -50.0f;
    float attackMs = 2.0f;
    float releaseMs = 100.0f;
    float holdMs = 50.0f;
};

struct EqParams {
    bool enabled = false;
    float lowDb = 0.0f, lowHz = 120.0f;     // low shelf
    float midDb = 0.0f, midHz = 1000.0f, midQ = 1.0f;  // peaking
    float highDb = 0.0f, highHz = 8000.0f;  // high shelf
};

struct CompParams {
    bool enabled = false;
    float thresholdDb = -20.0f;
    float ratio = 4.0f;
    float attackMs = 10.0f;
    float releaseMs = 150.0f;
    float makeupDb = 0.0f;
    float kneeDb = 0.0f;     // 0 = hard knee
    bool useSidechain = false;  // detektor memakai sinyal lain (key), bukan input sendiri
};

struct DelayParams {
    bool enabled = false;
    float delayMs = 0.0f;    // 0 .. kMaxDelayMs
};

struct LimiterParams {
    bool enabled = false;
    float ceilingDb = -1.0f;
    float releaseMs = 50.0f;
    float lookaheadMs = 0.0f;   // 0 = tanpa lookahead; maks kMaxLookaheadMs
};

// Biquad (RBJ cookbook), direct form I dengan state double
class Biquad {
public:
    void setLowShelf(float fs, float freq, float gainDb);
    void setPeak(float fs, float freq, float gainDb, float q);
    void setHighShelf(float fs, float freq, float gainDb);
    void setBypass();
    void reset() { x1 = x2 = y1 = y2 = 0; }
    float process(float x);
private:
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
};

class Gate {
public:
    void configure(float fs, const GateParams& p);
    void process(float* buf, int n);
    void reset();
private:
    float fs = 48000, thresh = 0, env = 0, gain = 0, attackC = 0, releaseC = 0;
    float holdSamples = 0, holdCounter = 0;
    float envRel = 0;
    bool open = false;
    bool enabled = false;
};

class Equalizer {
public:
    void configure(float fs, const EqParams& p);
    void process(float* buf, int n);
    void reset();
private:
    Biquad low, mid, high;
    bool enabled = false;
};

class Compressor {
public:
    void configure(float fs, const CompParams& p);
    // key: sinyal untuk detektor (sidechain). nullptr = pakai buf sendiri.
    void process(float* buf, int n, const float* key = nullptr);
    void reset();
private:
    float attackC = 0, releaseC = 0, thresh = 0, ratio = 1, makeup = 1;
    float env = 0, gr = 0;   // env: level dB, gr: gain reduction dB (positif)
    float knee = 0;
    bool enabled = false;
};

// Delay line dengan buffer tetap. allocate() dipanggil saat engine dibuat, bukan di jalur audio.
class Delay {
public:
    static constexpr float kMaxDelayMs = 1000.0f;
    void allocate(int maxSamples);
    void configure(float fs, const DelayParams& p);
    void process(float* buf, int n);
    void reset();
private:
    std::vector<float> line;
    int w = 0;
    int d = 0;
    bool enabled = false;
};

class Limiter {
public:
    static constexpr int kMaxLookaheadSamples = 1024;   // sekitar 21 ms pada 48 kHz
    void allocate();
    void configure(float fs, const LimiterParams& p);
    void process(float* buf, int n);
    void reset();
    int latencySamples() const { return lookahead; }
private:
    float ceiling = 1.0f, releaseC = 0;
    float gain = 1.0f;
    bool enabled = false;
    // Lookahead: ring berisi sampel dan target gain, serta deque minimum sliding
    int lookahead = 0;
    int cap = 0;
    long long pushed = 0;
    std::vector<float> xr, tr;
    std::vector<long long> dq;
    int dqHead = 0, dqTail = 0;
};

float dbToLinear(float db);
float linearToDb(float v);

}  // namespace audiohub

#endif
