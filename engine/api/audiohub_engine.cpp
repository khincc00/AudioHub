#include "audiohub_engine.h"

#include "../dsp/dsp.h"
#include "../graph/graph.h"

#include <atomic>
#include <cmath>
#include <memory>
#include <mutex>
#include <vector>

struct AHEngine {
    int nIn = 0;
    int nOut = 0;
    audiohub::Graph graph;

    // Pengaturan dari UI, dibaca di awal blok
    std::unique_ptr<std::atomic<int>[]> routeOn;
    std::unique_ptr<std::atomic<float>[]> routeDb;
    std::unique_ptr<std::atomic<float>[]> chanDb;
    std::unique_ptr<std::atomic<int>[]> chanMute;
    std::unique_ptr<std::atomic<float>[]> outDb;
    std::unique_ptr<std::atomic<int>[]> outMute;
    std::atomic<unsigned> version{1};
    unsigned applied = 0;

    // Insert DSP. Parameter baru ditulis di pending (di bawah mutex, dari UI),
    // lalu disalin ke active oleh thread audio dengan try_lock (tanpa menunggu).
    std::mutex fxMutex;
    bool fxDirty = false;
    std::vector<audiohub::GateParams> pendGate, actGate;
    std::vector<audiohub::EqParams> pendEq, actEq;
    std::vector<audiohub::CompParams> pendComp, actComp;
    std::vector<audiohub::LimiterParams> pendLim, actLim;
    std::vector<audiohub::Gate> gates;
    std::vector<audiohub::Equalizer> eqs;
    std::vector<audiohub::Compressor> comps;
    std::vector<audiohub::Delay> delays;
    std::vector<audiohub::DelayParams> pendDelay, actDelay;
    std::vector<int> pendSc, actSc;          // sumber sidechain per input, -1 = tanpa sidechain
    std::vector<float> pendPan, actPan;      // pan per input
    std::vector<audiohub::Limiter> lims;
    std::vector<std::vector<float>> scratch;   // salinan input untuk diproses insert
    int maxFrames = 0;
    float fs = 48000.0f;                       // sample rate untuk koefisien DSP
};

// Delay line dialokasikan untuk rate tertinggi yang didukung perangkat, supaya ganti rate tidak perlu alokasi.
constexpr int kMaxDelaySamples = 192000 + 16;   // 1 detik pada 192 kHz

AHEngine* ah_engine_create(int inputs, int outputs, int maxFrames) {
    return ah_engine_create_rate(inputs, outputs, maxFrames, 48000.0);
}

AHEngine* ah_engine_create_rate(int inputs, int outputs, int maxFrames, double sampleRate) {
    AHEngine* e = new AHEngine();
    e->nIn = inputs;
    e->nOut = outputs;
    e->fs = (float)sampleRate;
    for (int i = 0; i < inputs; i++) e->graph.addChannel("in-" + std::to_string(i + 1));
    for (int j = 0; j < outputs; j++) e->graph.addOutput("out-" + std::to_string(j + 1));
    int routes = inputs * outputs;
    e->routeOn.reset(new std::atomic<int>[routes]());
    e->routeDb.reset(new std::atomic<float>[routes]());
    e->chanDb.reset(new std::atomic<float>[inputs]());
    e->chanMute.reset(new std::atomic<int>[inputs]());
    e->outDb.reset(new std::atomic<float>[outputs]());
    e->outMute.reset(new std::atomic<int>[outputs]());
    for (int i = 0; i < inputs; i++) {
        for (int j = 0; j < outputs; j++) e->graph.connect(i, inputs + j, 0.0f, false);
    }
    e->graph.prepare(maxFrames);
    e->version.store(1);
    e->applied = 0;
    e->maxFrames = maxFrames;
    e->pendGate.resize(inputs);
    e->actGate.resize(inputs);
    e->pendEq.resize(inputs);
    e->actEq.resize(inputs);
    e->pendComp.resize(inputs);
    e->actComp.resize(inputs);
    e->pendLim.resize(outputs);
    e->actLim.resize(outputs);
    e->gates.resize(inputs);
    e->eqs.resize(inputs);
    e->comps.resize(inputs);
    e->delays.resize(inputs);
    e->pendSc.assign(inputs, -1);
    e->actSc.assign(inputs, -1);
    e->pendPan.assign(inputs, 0.0f);
    e->actPan.assign(inputs, 0.0f);
    e->pendDelay.resize(inputs);
    e->actDelay.resize(inputs);
    for (int i = 0; i < inputs; i++) e->delays[i].allocate(kMaxDelaySamples);
    e->lims.resize(outputs);
    for (int j = 0; j < outputs; j++) e->lims[j].allocate();
    e->scratch.assign(inputs, std::vector<float>(maxFrames, 0.0f));
    return e;
}

void ah_engine_destroy(AHEngine* e) { delete e; }

void ah_engine_set_route(AHEngine* e, int input, int output, int enabled, float sendDb) {
    if (!e || input < 0 || input >= e->nIn || output < 0 || output >= e->nOut) return;
    int k = input * e->nOut + output;
    e->routeOn[k].store(enabled ? 1 : 0);
    e->routeDb[k].store(sendDb);
    e->version.fetch_add(1);
}

void ah_engine_set_channel(AHEngine* e, int input, float gainDb, int mute) {
    if (!e || input < 0 || input >= e->nIn) return;
    e->chanDb[input].store(gainDb);
    e->chanMute[input].store(mute ? 1 : 0);
    e->version.fetch_add(1);
}

void ah_engine_set_output(AHEngine* e, int output, float gainDb, int mute) {
    if (!e || output < 0 || output >= e->nOut) return;
    e->outDb[output].store(gainDb);
    e->outMute[output].store(mute ? 1 : 0);
    e->version.fetch_add(1);
}

// Pan: untuk output genap (kiri) dan ganjil (kanan) dalam pasangan.
// Di tengah keduanya 0 dB. Gain linear: kiri = min(1, 1 − pan), kanan = min(1, 1 + pan).
static float panDbFor(float pan, int output) {
    if (pan == 0.0f) return 0.0f;
    float g = 1.0f;
    if (output % 2 == 0) g = 1.0f - pan;
    else g = 1.0f + pan;
    if (g > 1.0f) g = 1.0f;
    if (g < 1e-6f) return -120.0f;
    return 20.0f * std::log10(g);
}

static void applySettings(AHEngine* e) {
    for (int i = 0; i < e->nIn; i++) {
        e->graph.setNodeGain(i, e->chanDb[i].load());
        e->graph.setNodeMute(i, e->chanMute[i].load() != 0);
    }
    for (int j = 0; j < e->nOut; j++) {
        e->graph.setNodeGain(e->nIn + j, e->outDb[j].load());
        e->graph.setNodeMute(e->nIn + j, e->outMute[j].load() != 0);
    }
    for (int i = 0; i < e->nIn; i++) {
        for (int j = 0; j < e->nOut; j++) {
            int k = i * e->nOut + j;
            e->graph.setRouteSend(k, e->routeDb[k].load() + panDbFor(e->actPan[i], j));
            e->graph.setRouteEnabled(k, e->routeOn[k].load() != 0);
        }
    }
}

void ah_engine_set_input_fx(AHEngine* e, int input, const AHGateParams* gate, const AHEqParams* eq, const AHCompParams* comp) {
    if (!e || input < 0 || input >= e->nIn) return;
    std::lock_guard<std::mutex> lock(e->fxMutex);
    if (gate) {
        audiohub::GateParams& g = e->pendGate[input];
        g.enabled = gate->enabled != 0;
        g.thresholdDb = gate->thresholdDb; g.attackMs = gate->attackMs;
        g.releaseMs = gate->releaseMs; g.holdMs = gate->holdMs;
    }
    if (eq) {
        audiohub::EqParams& q = e->pendEq[input];
        q.enabled = eq->enabled != 0;
        q.lowDb = eq->lowDb; q.lowHz = eq->lowHz;
        q.midDb = eq->midDb; q.midHz = eq->midHz; q.midQ = eq->midQ;
        q.highDb = eq->highDb; q.highHz = eq->highHz;
    }
    if (comp) {
        audiohub::CompParams& c = e->pendComp[input];
        c.enabled = comp->enabled != 0;
        c.thresholdDb = comp->thresholdDb; c.ratio = comp->ratio;
        c.attackMs = comp->attackMs; c.releaseMs = comp->releaseMs; c.makeupDb = comp->makeupDb;
        c.kneeDb = comp->kneeDb;
        e->pendSc[input] = (comp->sidechainInput >= 0 && comp->sidechainInput < e->nIn && comp->sidechainInput != input) ? comp->sidechainInput : -1;
    }
    e->fxDirty = true;
}

void ah_engine_set_channel_pan(AHEngine* e, int input, float pan) {
    if (!e || input < 0 || input >= e->nIn) return;
    std::lock_guard<std::mutex> lock(e->fxMutex);
    e->pendPan[input] = pan < -1.0f ? -1.0f : (pan > 1.0f ? 1.0f : pan);
    e->fxDirty = true;
}

void ah_engine_set_input_delay(AHEngine* e, int input, const AHDelayParams* delay) {
    if (!e || input < 0 || input >= e->nIn || !delay) return;
    std::lock_guard<std::mutex> lock(e->fxMutex);
    e->pendDelay[input].enabled = delay->enabled != 0;
    e->pendDelay[input].delayMs = delay->delayMs;
    e->fxDirty = true;
}

void ah_engine_set_output_limiter(AHEngine* e, int output, const AHLimiterParams* lim) {
    if (!e || output < 0 || output >= e->nOut || !lim) return;
    std::lock_guard<std::mutex> lock(e->fxMutex);
    audiohub::LimiterParams& l = e->pendLim[output];
    l.enabled = lim->enabled != 0;
    l.ceilingDb = lim->ceilingDb;
    l.releaseMs = lim->releaseMs;
    l.lookaheadMs = lim->lookaheadMs;
    e->fxDirty = true;
}

// Dipanggil dari thread audio. Tidak menunggu: jika UI sedang menulis, perubahan menunggu blok berikutnya.
static void applyFxIfChanged(AHEngine* e) {
    std::unique_lock<std::mutex> lock(e->fxMutex, std::try_to_lock);
    if (!lock.owns_lock() || !e->fxDirty) return;
    float fs = e->fs;
    for (int i = 0; i < e->nIn; i++) {
        e->actGate[i] = e->pendGate[i];
        e->actEq[i] = e->pendEq[i];
        e->actComp[i] = e->pendComp[i];
        e->actDelay[i] = e->pendDelay[i];
        e->actSc[i] = e->pendSc[i];
        e->actPan[i] = e->pendPan[i];
        e->gates[i].configure(fs, e->actGate[i]);
        e->eqs[i].configure(fs, e->actEq[i]);
        e->comps[i].configure(fs, e->actComp[i]);
        e->delays[i].configure(fs, e->actDelay[i]);
    }
    for (int j = 0; j < e->nOut; j++) {
        e->actLim[j] = e->pendLim[j];
        e->lims[j].configure(fs, e->actLim[j]);
    }
    e->fxDirty = false;
    // Pan dan sumber sidechain ikut ke route dan proses, jadi paksa applySettings di blok ini
    e->version.fetch_add(1);
}

void ah_engine_process(AHEngine* e, const float* const* inputs, float* const* outputs, int frames) {
    if (!e) return;
    if (frames > e->maxFrames) frames = e->maxFrames;
    applyFxIfChanged(e);
    unsigned v = e->version.load();
    if (v != e->applied) {
        applySettings(e);
        e->applied = v;
    }
    // Insert per input: gate → EQ → kompresor, di salinan supaya input asli tidak berubah
    const float* routed[64];
    for (int i = 0; i < e->nIn; i++) {
        float* buf = e->scratch[i].data();
        for (int f = 0; f < frames; f++) buf[f] = inputs[i][f];
        e->gates[i].process(buf, frames);
        e->eqs[i].process(buf, frames);
        const float* key = e->actSc[i] >= 0 ? inputs[e->actSc[i]] : nullptr;
        e->comps[i].process(buf, frames, key);
        e->delays[i].process(buf, frames);
        routed[i] = buf;
    }
    e->graph.process(routed, outputs, frames);
    // Limiter per output
    for (int j = 0; j < e->nOut; j++) e->lims[j].process(outputs[j], frames);
}

void ah_engine_peaks(AHEngine* e, float* inPeaks, float* outPeaks) {
    if (!e) return;
    const auto& nodes = e->graph.nodes();
    for (int i = 0; i < e->nIn; i++) inPeaks[i] = nodes[i].peak;
    for (int j = 0; j < e->nOut; j++) outPeaks[j] = nodes[e->nIn + j].peak;
}
