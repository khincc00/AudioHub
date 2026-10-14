// Technical spike Tahap 0: input fisik → engine → output fisik pada satu perangkat.
// Satu IOProc duplex, gain 0 dB, tanpa alokasi di callback.
// Dropout dicatat dari kontinuitas mSampleTime; ini proksi untuk xrun, bukan pengukuran langsung.
// Pemakaian: ./passthrough "Maono" [detik=60] [buffer=512]
// Peringatan: spike ini MEMUTAR audio dari input ke output. Pakai headphone dan mix penuh.
#include <CoreAudio/CoreAudio.h>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <unistd.h>

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
    UInt32 n = size / sizeof(AudioObjectID);
    std::vector<AudioObjectID> ids(n);
    AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, 0, nullptr, &size, ids.data());
    for (UInt32 i = 0; i < n; i++) {
        if (deviceName(ids[i]).find(wanted) != std::string::npos) return ids[i];
    }
    return kAudioObjectUnknown;
}

// Status yang dibagi dengan thread utama. Hanya IOProc yang menulis.
struct State {
    std::atomic<uint64_t> callbacks{0};
    std::atomic<uint64_t> dropouts{0};
    std::atomic<uint64_t> missingFrames{0};
    std::atomic<uint64_t> totalFrames{0};
    std::atomic<float> peakIn[8];
    std::atomic<float> peakOut[8];
    double lastSample = 0;
    uint32_t lastFrames = 0;
    bool started = false;
    int channels = 0;
};
static State state;

static OSStatus ioProc(AudioObjectID, const AudioTimeStamp*,
                       const AudioBufferList* inData, const AudioTimeStamp* inTime,
                       AudioBufferList* outData, const AudioTimeStamp*, void*) {
    // Pemetaan kanal global → pointer dan stride, tanpa alokasi
    const float* inPtr[8] = {nullptr};
    int inStride[8] = {1, 1, 1, 1, 1, 1, 1, 1};
    float* outPtr[8] = {nullptr};
    int outStride[8] = {1, 1, 1, 1, 1, 1, 1, 1};
    int inCh = 0, outCh = 0;
    for (UInt32 b = 0; b < inData->mNumberBuffers; b++) {
        const AudioBuffer& buf = inData->mBuffers[b];
        int nch = (int)buf.mNumberChannels;
        for (int c = 0; c < nch && inCh < 8; c++, inCh++) {
            inPtr[inCh] = (const float*)buf.mData + c;
            inStride[inCh] = nch;
        }
    }
    for (UInt32 b = 0; b < outData->mNumberBuffers; b++) {
        AudioBuffer& buf = outData->mBuffers[b];
        int nch = (int)buf.mNumberChannels;
        memset(buf.mData, 0, buf.mDataByteSize);
        for (int c = 0; c < nch && outCh < 8; c++, outCh++) {
            outPtr[outCh] = (float*)buf.mData + c;
            outStride[outCh] = nch;
        }
    }
    uint32_t frames = outData->mNumberBuffers > 0
        ? outData->mBuffers[0].mDataByteSize / (sizeof(float) * outData->mBuffers[0].mNumberChannels)
        : 0;

    // Kontinuitas waktu sampel: lompatan berarti ada frame yang terlewat
    double now = inTime->mSampleTime;
    if (state.started) {
        double expected = state.lastSample + state.lastFrames;
        if (now > expected + 0.5) {
            state.dropouts++;
            state.missingFrames += (uint64_t)(now - expected);
        }
    }
    state.started = true;
    state.lastSample = now;
    state.lastFrames = frames;

    // Routing: input kanal k → output kanal k, gain 0 dB
    int n = inCh < outCh ? inCh : outCh;
    for (int k = 0; k < n; k++) {
        float pk = 0, pko = 0;
        for (uint32_t f = 0; f < frames; f++) {
            float v = inPtr[k][f * inStride[k]];
            outPtr[k][f * outStride[k]] = v;
            float a = fabsf(v);
            if (a > pk) pk = a;
        }
        if (k < 8) {
            if (pk > state.peakIn[k].load()) state.peakIn[k].store(pk);
            (void)pko;
        }
    }
    for (int k = 0; k < n && k < 8; k++) {
        float pko = 0;
        for (uint32_t f = 0; f < frames; f++) {
            float a = fabsf(outPtr[k][f * outStride[k]]);
            if (a > pko) pko = a;
        }
        if (pko > state.peakOut[k].load()) state.peakOut[k].store(pko);
    }
    state.callbacks++;
    state.totalFrames += frames;
    return noErr;
}

static double db(float v) { return v > 0 ? 20.0 * log10((double)v) : -999.0; }

int main(int argc, char** argv) {
    std::string wanted = argc > 1 ? argv[1] : "Maono";
    int seconds = argc > 2 ? atoi(argv[2]) : 60;
    UInt32 requestedBuffer = argc > 3 ? (UInt32)atoi(argv[3]) : 0;

    AudioObjectID dev = findDevice(wanted);
    if (dev == kAudioObjectUnknown) { printf("Perangkat \"%s\" tidak ditemukan\n", wanted.c_str()); return 1; }
    printf("Spike passthrough pada: %s, %d detik\n", deviceName(dev).c_str(), seconds);

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

    uint64_t lastCallbacks = 0, lastDropouts = 0, lastMissing = 0;
    for (int s = 1; s <= seconds; s++) {
        usleep(1000000);
        uint64_t cb = state.callbacks.load(), dr = state.dropouts.load(), mf = state.missingFrames.load();
        printf("t=%3ds  callback=%llu  dropout=%llu  frame_hilang=%llu  ",
               s, (unsigned long long)(cb - lastCallbacks),
               (unsigned long long)(dr - lastDropouts), (unsigned long long)(mf - lastMissing));
        printf("in1=%.1f in2=%.1f out1=%.1f out2=%.1f dBFS\n",
               db(state.peakIn[0].exchange(0)), db(state.peakIn[1].exchange(0)),
               db(state.peakOut[0].exchange(0)), db(state.peakOut[1].exchange(0)));
        lastCallbacks = cb; lastDropouts = dr; lastMissing = mf;
    }

    AudioDeviceStop(dev, proc);
    AudioDeviceDestroyIOProcID(dev, proc);

    uint64_t cb = state.callbacks.load();
    uint64_t frames = state.totalFrames.load();
    double expectedCb = buffer > 0 ? (double)frames / buffer : 0;
    printf("\nRingkasan: callback=%llu (perkiraan %.0f), dropout=%llu, frame hilang=%llu\n",
           (unsigned long long)cb, expectedCb,
           (unsigned long long)state.dropouts.load(), (unsigned long long)state.missingFrames.load());

    if (requestedBuffer > 0) {
        UInt32 restore = originalBuffer;
        AudioObjectSetPropertyData(dev, &bufAddr, 0, nullptr, bufSize, &restore);
        printf("Buffer dikembalikan ke %u frame\n", originalBuffer);
    }
    return 0;
}
