// API C untuk engine routing AudioHub. Dipanggil dari UI (SwiftUI) dan dari backend CoreAudio.
// Topologi tetap: N channel input, M output, satu route langsung per pasangan (input, output).
// Perubahan route dan gain dilakukan lewat atomic dan diterapkan di awal blok proses.
#ifndef AUDIOHUB_ENGINE_H
#define AUDIOHUB_ENGINE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AHEngine AHEngine;

AHEngine* ah_engine_create(int inputs, int outputs, int maxFrames);
// Sama seperti di atas, dengan sample rate. Koefisien DSP (gate, EQ, kompresor, limiter, delay) mengikuti rate ini.
AHEngine* ah_engine_create_rate(int inputs, int outputs, int maxFrames, double sampleRate);
void ah_engine_destroy(AHEngine* engine);

// Aman dipanggil dari thread UI. Route dinonaktifkan secara default.
void ah_engine_set_route(AHEngine* engine, int input, int output, int enabled, float sendDb);
// Aman dipanggil dari thread UI. mute = 1 membisukan channel.
void ah_engine_set_channel(AHEngine* engine, int input, float gainDb, int mute);

// Real-time: tidak mengalokasikan memori. frames dibatasi maxFrames dari create.
void ah_engine_process(AHEngine* engine, const float* const* inputs, float* const* outputs, int frames);

// Gain dan mute per output (fader output). Aman dipanggil dari thread UI.
void ah_engine_set_output(AHEngine* engine, int output, float gainDb, int mute);

// Parameter insert. Dipanggil dari UI; berlaku di blok berikutnya. enabled = 0 membypass.
typedef struct AHGateParams { int enabled; float thresholdDb, attackMs, releaseMs, holdMs; } AHGateParams;
typedef struct AHEqParams { int enabled; float lowDb, lowHz, midDb, midHz, midQ, highDb, highHz; } AHEqParams;
typedef struct AHCompParams { int enabled; float thresholdDb, ratio, attackMs, releaseMs, makeupDb, kneeDb; int sidechainInput; } AHCompParams;
typedef struct AHDelayParams { int enabled; float delayMs; } AHDelayParams;
typedef struct AHLimiterParams { int enabled; float ceilingDb, releaseMs, lookaheadMs; } AHLimiterParams;

// Urutan per input: noise gate → EQ → kompresor → delay. Input sebelum routing.
void ah_engine_set_input_delay(AHEngine* engine, int input, const AHDelayParams* delay);
void ah_engine_set_input_fx(AHEngine* engine, int input, const AHGateParams* gate, const AHEqParams* eq, const AHCompParams* comp);
// Limiter per output, setelah mixing.
void ah_engine_set_output_limiter(AHEngine* engine, int output, const AHLimiterParams* limiter);

// Pan per input untuk pasangan output (0,1), (2,3), ...: −1 = kiri penuh, 0 = tengah, +1 = kanan penuh.
void ah_engine_set_channel_pan(AHEngine* engine, int input, float pan);

// Peak blok terakhir (linear), untuk meter. inPeaks: N nilai, outPeaks: M nilai.
void ah_engine_peaks(AHEngine* engine, float* inPeaks, float* outPeaks);

#ifdef __cplusplus
}
#endif

#endif
