// Spike Tahap 2: routing lewat virtual cable.
// Membuat aggregate device privat (PS22 sebagai master, EMEET Virtual Audio sebagai sub-device
// dengan drift compensation), menjalankan engine di atasnya, lalu menghapus aggregate.
// Mode:
//   receive : audio yang diputar ke EMEET output 1 (seolah dari aplikasi) → EMEET input 1 → engine → PS22 output 1 dan 2
//   send    : input 1 PS22 (mic) → engine → EMEET output 1 (virtual cable) → EMEET input 1 (diukur kembali)
// Pemakaian: ./virtual_cable [receive|send|mixminus] [detik=10] [UID virtual device]
// mixminus: ps-out1 = monitor, ps-out2 = virtual mic (pengukuran), ps-in1 = Voice, emeet-in1 = Desktop, emeet-in2 = Communication
// Default UID: EMEET. Untuk AudioHub: com.audiohub.virtual.device
// Peringatan: mode receive dan send MEMUTAR audio. Pakai headphone. Jangan pasang kabel loopback.
#include "../../../engine/graph/graph.h"

#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>

static const char* kPS22UID = "AppleUSBAudioEngine:Maono ProStudio 2x2:Maono ProStudio 2x2:100000:1,2";
static const char* kVirtualUID = "EMEET_VirtualAudio_UID";   // bisa diganti argumen ketiga
static const char* kAggUID = "com.audiohub.spike.tahap2";

static AudioObjectPropertyAddress addr(AudioObjectPropertySelector sel) {
    return {sel, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
}

static int channelsOf(AudioObjectID dev, AudioObjectPropertyScope scope) {
    auto a = addr(kAudioDevicePropertyStreamConfiguration);
    a.mScope = scope;
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(dev, &a, 0, nullptr, &size) != noErr || size == 0) return 0;
    std::vector<unsigned char> raw(size);
    AudioBufferList* list = (AudioBufferList*)raw.data();
    AudioObjectGetPropertyData(dev, &a, 0, nullptr, &size, list);
    int n = 0;
    for (UInt32 i = 0; i < list->mNumberBuffers; i++) n += (int)list->mBuffers[i].mNumberChannels;
    return n;
}

static AudioObjectID createAggregate() {
    CFMutableArrayRef subs = CFArrayCreateMutable(nullptr, 2, &kCFTypeArrayCallBacks);
    auto makeSub = [](const char* uid, bool drift) {
        CFStringRef u = CFStringCreateWithCString(nullptr, uid, kCFStringEncodingUTF8);
        const void* keys[] = {CFSTR("uid"), CFSTR("drift")};
        const void* vals[] = {u, drift ? kCFBooleanTrue : kCFBooleanFalse};
        CFDictionaryRef d = CFDictionaryCreate(nullptr, keys, vals, 2, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        CFRelease(u);
        return d;
    };
    CFDictionaryRef ps = makeSub(kPS22UID, false);      // master: tanpa drift compensation
    CFDictionaryRef em = makeSub(kVirtualUID, true);    // sub-device: drift compensation
    CFArrayAppendValue(subs, ps);
    CFArrayAppendValue(subs, em);
    CFRelease(ps);
    CFRelease(em);

    CFStringRef name = CFSTR("AudioHub Tahap 2 (spike)");
    CFStringRef uid = CFSTR("com.audiohub.spike.tahap2");
    CFStringRef masterUID = CFStringCreateWithCString(nullptr, kPS22UID, kCFStringEncodingUTF8);
    const void* keys[] = {CFSTR("name"), CFSTR("uid"), CFSTR("private"), CFSTR("subdevices"), CFSTR("master")};
    const void* vals[] = {name, uid, kCFBooleanTrue, subs, masterUID};
    CFDictionaryRef desc = CFDictionaryCreate(nullptr, keys, vals, 5, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    AudioObjectID agg = kAudioObjectUnknown;
    OSStatus st = AudioHardwareCreateAggregateDevice(desc, &agg);
    CFRelease(desc);
    CFRelease(masterUID);
    CFRelease(subs);
    if (st != noErr) { printf("Gagal membuat aggregate: %d\n", (int)st); return kAudioObjectUnknown; }
    return agg;
}

static void destroyAggregate(AudioObjectID agg) {
    if (agg != kAudioObjectUnknown) AudioHardwareDestroyAggregateDevice(agg);
}

// Buffer statis, tidak ada alokasi di callback. Empat input: PS22 1–2, EMEET 1–2.
constexpr int kMaxFrames = 4096;
constexpr int kIn = 4;
constexpr int kOut = 4;
static audiohub::Graph graph;
static float inDeint[kIn][kMaxFrames];
static float outDeint[kOut][kMaxFrames];
static int inNode[kIn], outNode[kOut], meterNodeMic = 0;

static std::atomic<uint64_t> callbacks{0}, dropouts{0}, missingFrames{0};
static std::atomic<float> meterIn[kIn], meterOut[kOut], rawIn[kIn];
static double lastSample = 0;
static uint32_t lastFrames = 0;
static bool started = false;

static OSStatus ioProc(AudioObjectID, const AudioTimeStamp*,
                       const AudioBufferList* inData, const AudioTimeStamp* inTime,
                       AudioBufferList* outData, const AudioTimeStamp*, void*) {
    uint32_t frames = outData->mBuffers[0].mDataByteSize / (sizeof(float) * outData->mBuffers[0].mNumberChannels);
    if (frames > (uint32_t)kMaxFrames) frames = kMaxFrames;

    double now = inTime->mSampleTime;
    if (started) {
        double expected = lastSample + lastFrames;
        if (now > expected + 0.5) { dropouts++; missingFrames += (uint64_t)(now - expected); }
    }
    started = true;
    lastSample = now;
    lastFrames = frames;

    int g = 0;
    for (UInt32 b = 0; b < inData->mNumberBuffers && g < kIn; b++) {
        const AudioBuffer& buf = inData->mBuffers[b];
        int nch = (int)buf.mNumberChannels;
        const float* src = (const float*)buf.mData;
        for (int c = 0; c < nch && g < kIn; c++, g++) {
            float pk = 0;
            for (uint32_t f = 0; f < frames; f++) {
                float v = src[f * nch + c];
                inDeint[g][f] = v;
                float a = fabsf(v);
                if (a > pk) pk = a;
            }
            rawIn[g].store(pk);
        }
    }
    for (int i = g; i < kIn; i++) std::memset(inDeint[i], 0, sizeof(float) * frames);

    const float* inPtr[kIn];
    float* outPtr[kOut];
    for (int i = 0; i < kIn; i++) inPtr[i] = inDeint[i];
    for (int o = 0; o < kOut; o++) outPtr[o] = outDeint[o];
    graph.process(inPtr, outPtr, (int)frames);

    g = 0;
    for (UInt32 b = 0; b < outData->mNumberBuffers; b++) {
        AudioBuffer& buf = outData->mBuffers[b];
        int nch = (int)buf.mNumberChannels;
        float* dst = (float*)buf.mData;
        std::memset(dst, 0, buf.mDataByteSize);
        for (int c = 0; c < nch && g < kOut; c++, g++) {
            for (uint32_t f = 0; f < frames; f++) dst[f * nch + c] = outDeint[g][f];
        }
    }

    for (int i = 0; i < kIn; i++) meterIn[i].store(graph.nodes()[inNode[i]].peak);
    for (int o = 0; o < kOut; o++) meterOut[o].store(graph.nodes()[outNode[o]].peak);
    callbacks++;
    return noErr;
}

static double db(float v) { return v > 0 ? 20.0 * log10((double)v) : -999.0; }

int main(int argc, char** argv) {
    std::string mode = argc > 1 ? argv[1] : "receive";
    int seconds = argc > 2 ? atoi(argv[2]) : 10;
    if (argc > 3) kVirtualUID = argv[3];
    bool send = mode == "send";
    bool mixminus = mode == "mixminus";

    // Node channel berurutan: PS22 1, PS22 2, EMEET 1, EMEET 2
    for (int i = 0; i < kIn; i++) inNode[i] = graph.addChannel(i < 2 ? "ps-in" : "emeet-in");
    for (int o = 0; o < kOut; o++) outNode[o] = graph.addOutput(o < 2 ? "ps-out" : "emeet-out");
    int busL = graph.addBus("desktop-L");
    int busR = graph.addBus("desktop-R");
    int busVoice = graph.addBus("voice");

    if (mixminus) {
        // Voice = PS22 in 1, Desktop = AudioHub in 1, Communication = AudioHub in 2.
        // Monitor (PS22 out 1) = Voice + Desktop + Communication.
        // Virtual mic (PS22 out 2, untuk pengukuran) = Voice + Desktop, tanpa Communication.
        int busDesk = busL;
        int busComm = busR;
        int monitor = graph.addBus("monitor");
        int vmic = graph.addBus("vmic");
        graph.connect(inNode[0], busVoice);
        graph.connect(inNode[2], busDesk);
        graph.connect(inNode[3], busComm);
        graph.connect(busVoice, monitor);
        graph.connect(busDesk, monitor);
        graph.connect(busComm, monitor);
        graph.connect(busVoice, vmic);
        graph.connect(busDesk, vmic);
        graph.connect(monitor, outNode[0]);
        graph.connect(vmic, outNode[1]);
    } else if (send) {
        graph.connect(inNode[0], busVoice);
        graph.connect(busVoice, outNode[2]);     // ke EMEET output 1 (virtual cable)
    } else {
        graph.connect(inNode[2], busL);          // EMEET in 1 (app) → Desktop L
        graph.connect(inNode[3], busR);          // EMEET in 2 (app) → Desktop R
        graph.connect(busL, outNode[0]);         // → PS22 output 1
        graph.connect(busR, outNode[1]);         // → PS22 output 2
    }
    std::string err = graph.prepare(kMaxFrames);
    if (!err.empty()) { printf("Graph ditolak: %s\n", err.c_str()); return 1; }

    AudioObjectID agg = createAggregate();
    if (agg == kAudioObjectUnknown) return 1;
    sleep(1);
    int inCh = channelsOf(agg, kAudioObjectPropertyScopeInput);
    int outCh = channelsOf(agg, kAudioObjectPropertyScopeOutput);
    printf("Aggregate dibuat: input %d kanal, output %d kanal (mode %s, %d detik)\n", inCh, outCh, mode.c_str(), seconds);
    if (inCh < kIn || outCh < kOut) {
        printf("Jumlah kanal aggregate tidak sesuai, dibatalkan\n");
        destroyAggregate(agg);
        return 1;
    }

    AudioDeviceIOProcID proc = nullptr;
    OSStatus st = AudioDeviceCreateIOProcID(agg, ioProc, nullptr, &proc);
    if (st != noErr || !proc) { printf("Gagal membuat IOProc: %d\n", (int)st); destroyAggregate(agg); return 1; }
    AudioDeviceStart(agg, proc);

    uint64_t lastCb = 0, lastDr = 0, lastMf = 0;
    for (int s = 1; s <= seconds; s++) {
        usleep(1000000);
        uint64_t cb = callbacks.load(), dr = dropouts.load(), mf = missingFrames.load();
        printf("t=%3ds  cb=%llu drop=%llu hilang=%llu | ps-in1=%.1f emeet-in1=%.1f | ps-out1=%.1f ps-out2=%.1f emeet-out1=%.1f dBFS\n",
               s, (unsigned long long)(cb - lastCb), (unsigned long long)(dr - lastDr),
               (unsigned long long)(mf - lastMf),
               db(meterIn[0].load()), db(meterIn[2].load()),
               db(meterOut[0].load()), db(meterOut[1].load()), db(meterOut[2].load()));
        lastCb = cb; lastDr = dr; lastMf = mf;
    }

    AudioDeviceStop(agg, proc);
    AudioDeviceDestroyIOProcID(agg, proc);
    destroyAggregate(agg);
    printf("Ringkasan: cb=%llu drop=%llu hilang=%llu. Aggregate dihapus.\n",
           (unsigned long long)callbacks.load(), (unsigned long long)dropouts.load(), (unsigned long long)missingFrames.load());
    return 0;
}
