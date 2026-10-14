// Spike Tahap 1: input PS22 → routing graph (engine/graph) → output PS22.
// Input 1 → output 1 dan input 2 → output 2, dengan gain input 1 dari argumen.
// Pemakaian: ./passthrough_engine "Maono" [detik=60] [gainInput1Db=0] [buffer=512] [bus]
// Peringatan: spike ini MEMUTAR audio. Pakai headphone, mix penuh, kabel loopback dilepas.
#include "../../../engine/graph/graph.h"

#include <CoreAudio/CoreAudio.h>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>
#include <vector>

static AudioObjectPropertyAddress addr(AudioObjectPropertySelector sel) {
    return {sel, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
}

static std::string deviceName(AudioObjectID id) {
    auto a = addr(kAudioObjectPropertyName);
    CFStringRef s = nullptr;
    UInt32 size = sizeof(s);
    AudioObjectGetPropertyData(id, &a, 0, nullptr, &size, &s);
    char buf[256] = {0};
    if (s) { CFStringGetCString(s, buf, sizeof(buf), kCFStringEncodingUTF8); CFRelease(s); }
    return buf;
}

static AudioObjectID findDevice(const std::string& wanted) {
    auto a = addr(kAudioHardwarePropertyDevices);
    UInt32 size = 0;
    AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &a, 0, nullptr, &size);
    std::vector<AudioObjectID> ids(size / sizeof(AudioObjectID));
    AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, 0, nullptr, &size, ids.data());
    for (AudioObjectID id : ids) {
        if (deviceName(id).find(wanted) != std::string::npos) return id;
    }
    return kAudioObjectUnknown;
}

// Buffer statis: tidak ada alokasi di callback.
constexpr int kMaxFrames = 4096;
static audiohub::Graph graph;
static float inDeint[2][kMaxFrames];
static float outDeint[2][kMaxFrames];
static int inNode[2], outNode[2];

static std::atomic<uint64_t> callbacks{0}, dropouts{0}, missingFrames{0}, totalFrames{0};
static std::atomic<float> meterIn[2], meterOut[2], rawIn[2];
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
        if (now > expected + 0.5) {
            dropouts++;
            missingFrames += (uint64_t)(now - expected);
        }
    }
    started = true;
    lastSample = now;
    lastFrames = frames;

    // Deinterleave input ke dua channel global
    int g = 0;
    for (UInt32 b = 0; b < inData->mNumberBuffers && g < 2; b++) {
        const AudioBuffer& buf = inData->mBuffers[b];
        int nch = (int)buf.mNumberChannels;
        const float* src = (const float*)buf.mData;
        for (int c = 0; c < nch && g < 2; c++, g++) {
            for (uint32_t f = 0; f < frames; f++) inDeint[g][f] = src[f * nch + c];
        }
    }

    // Peak input mentah, sebelum graph, untuk memverifikasi gain node pada perangkat
    for (int i = 0; i < 2; i++) {
        float pk = 0;
        for (uint32_t f = 0; f < frames; f++) {
            float a = fabsf(inDeint[i][f]);
            if (a > pk) pk = a;
        }
        rawIn[i].store(pk);
    }

    const float* inPtr[2] = {inDeint[0], inDeint[1]};
    float* outPtr[2] = {outDeint[0], outDeint[1]};
    graph.process(inPtr, outPtr, (int)frames);

    // Interleave output dari dua output global
    g = 0;
    for (UInt32 b = 0; b < outData->mNumberBuffers; b++) {
        AudioBuffer& buf = outData->mBuffers[b];
        int nch = (int)buf.mNumberChannels;
        float* dst = (float*)buf.mData;
        memset(dst, 0, buf.mDataByteSize);
        for (int c = 0; c < nch && g < 2; c++, g++) {
            for (uint32_t f = 0; f < frames; f++) dst[f * nch + c] = outDeint[g][f];
        }
    }

    for (int i = 0; i < 2; i++) {
        meterIn[i].store(graph.nodes()[inNode[i]].peak);
        meterOut[i].store(graph.nodes()[outNode[i]].peak);
    }
    callbacks++;
    totalFrames += frames;
    return noErr;
}

static double db(float v) { return v > 0 ? 20.0 * log10((double)v) : -999.0; }

int main(int argc, char** argv) {
    std::string wanted = argc > 1 ? argv[1] : "Maono";
    int seconds = argc > 2 ? atoi(argv[2]) : 60;
    float gainIn1 = argc > 3 ? (float)atof(argv[3]) : 0.0f;
    UInt32 requestedBuffer = argc > 4 ? (UInt32)atoi(argv[4]) : 0;

    // Mode "bus": input 1 → bus A (0 dB) → output 1, dan input 1 → bus B (−6 dB) → output 2.
    // Mode default: input 1 → output 1, input 2 → output 2.
    bool busMode = argc > 5 && std::string(argv[5]) == "bus";
    inNode[0] = graph.addChannel("input-1");
    inNode[1] = graph.addChannel("input-2");
    outNode[0] = graph.addOutput("output-1");
    outNode[1] = graph.addOutput("output-2");
    if (busMode) {
        int busA = graph.addBus("bus-A");
        int busB = graph.addBus("bus-B");
        graph.connect(inNode[0], busA);
        graph.connect(busA, outNode[0]);
        graph.connect(inNode[0], busB);
        graph.connect(busB, outNode[1]);
        graph.setNodeGain(busB, -6.0f);
    } else {
        graph.connect(inNode[0], outNode[0]);
        graph.connect(inNode[1], outNode[1]);
    }
    graph.setNodeGain(inNode[0], gainIn1);
    std::string err = graph.prepare(kMaxFrames);
    if (!err.empty()) { printf("Graph ditolak: %s\n", err.c_str()); return 1; }

    AudioObjectID dev = findDevice(wanted);
    if (dev == kAudioObjectUnknown) { printf("Perangkat \"%s\" tidak ditemukan\n", wanted.c_str()); return 1; }
    printf("Spike engine pada: %s, %d detik, gain input 1 = %.1f dB\n", deviceName(dev).c_str(), seconds, gainIn1);

    auto bufAddr = addr(kAudioDevicePropertyBufferFrameSize);
    UInt32 bufSize = sizeof(UInt32), originalBuffer = 0;
    AudioObjectGetPropertyData(dev, &bufAddr, 0, nullptr, &bufSize, &originalBuffer);
    if (requestedBuffer > 0) {
        AudioObjectSetPropertyData(dev, &bufAddr, 0, nullptr, bufSize, &requestedBuffer);
        usleep(500000);
    }
    UInt32 buffer = 0;
    AudioObjectGetPropertyData(dev, &bufAddr, 0, nullptr, &bufSize, &buffer);
    printf("Buffer aktif: %u frame (asal: %u)\n", buffer, originalBuffer);

    AudioDeviceIOProcID proc = nullptr;
    OSStatus st = AudioDeviceCreateIOProcID(dev, ioProc, nullptr, &proc);
    if (st != noErr || !proc) { printf("Gagal membuat IOProc: %d\n", (int)st); return 1; }
    AudioDeviceStart(dev, proc);

    uint64_t lastCb = 0, lastDr = 0, lastMf = 0;
    for (int s = 1; s <= seconds; s++) {
        usleep(1000000);
        uint64_t cb = callbacks.load(), dr = dropouts.load(), mf = missingFrames.load();
        printf("t=%3ds  callback=%llu  dropout=%llu  frame_hilang=%llu  ",
               s, (unsigned long long)(cb - lastCb), (unsigned long long)(dr - lastDr),
               (unsigned long long)(mf - lastMf));
        printf("raw1=%.1f post1=%.1f selisih=%.2f dB | in2=%.1f out1=%.1f out2=%.1f dBFS\n",
               db(rawIn[0].load()), db(meterIn[0].load()), db(meterIn[0].load()) - db(rawIn[0].load()),
               db(meterIn[1].load()), db(meterOut[0].load()), db(meterOut[1].load()));
        lastCb = cb; lastDr = dr; lastMf = mf;
    }

    AudioDeviceStop(dev, proc);
    AudioDeviceDestroyIOProcID(dev, proc);

    printf("\nRingkasan: callback=%llu, dropout=%llu, frame hilang=%llu\n",
           (unsigned long long)callbacks.load(), (unsigned long long)dropouts.load(),
           (unsigned long long)missingFrames.load());
    if (requestedBuffer > 0) {
        UInt32 restore = originalBuffer;
        AudioObjectSetPropertyData(dev, &bufAddr, 0, nullptr, bufSize, &restore);
        printf("Buffer dikembalikan ke %u frame\n", originalBuffer);
    }
    return 0;
}
