#include "audiohub_io.h"

#include "../../../engine/api/audiohub_engine.h"
#include "au_insert.h"

#include <CoreAudio/CoreAudio.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <unistd.h>

namespace {

constexpr int kMaxCh = 8;
constexpr int kMaxFrames = 4096;

AudioObjectPropertyAddress addr(AudioObjectPropertySelector sel, AudioObjectPropertyScope scope = kAudioObjectPropertyScopeGlobal) {
    return {sel, scope, kAudioObjectPropertyElementMain};
}

std::string nameOf(AudioObjectID id) {
    auto a = addr(kAudioObjectPropertyName);
    CFStringRef s = nullptr;
    UInt32 size = sizeof(s);
    AudioObjectGetPropertyData(id, &a, 0, nullptr, &size, &s);
    char buf[256] = {0};
    if (s) { CFStringGetCString(s, buf, sizeof(buf), kCFStringEncodingUTF8); CFRelease(s); }
    return buf;
}

int channelsOf(AudioObjectID dev, AudioObjectPropertyScope scope) {
    auto a = addr(kAudioDevicePropertyStreamConfiguration, scope);
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(dev, &a, 0, nullptr, &size) != noErr || size == 0) return 0;
    std::unique_ptr<unsigned char[]> raw(new unsigned char[size]);
    AudioBufferList* list = reinterpret_cast<AudioBufferList*>(raw.get());
    AudioObjectGetPropertyData(dev, &a, 0, nullptr, &size, list);
    int n = 0;
    for (UInt32 i = 0; i < list->mNumberBuffers; i++) n += (int)list->mBuffers[i].mNumberChannels;
    return n;
}

}  // namespace

struct AHIO {
    AudioObjectID dev = kAudioObjectUnknown;
    AudioObjectID aggregate = kAudioObjectUnknown;
    AudioDeviceIOProcID proc = nullptr;
    AHEngine* engine = nullptr;
    int nIn = 0, nOut = 0;
    int physIn = 0, physOut = 0;
    uint32_t bufferFrames = 0;
    double sampleRate = 0;

    // Buffer deinterleave, dialokasikan sekali di open
    float inDeint[kMaxCh][kMaxFrames];
    float outDeint[kMaxCh][kMaxFrames];
    const float* inPtr[kMaxCh];
    float* outPtr[kMaxCh];

    std::atomic<uint64_t> callbacks{0}, dropouts{0}, missing{0};
    double lastSample = 0;
    uint32_t lastFrames = 0;
    bool started = false;
    std::atomic<bool> running{false};
    audiohub::AUInsert insert[kMaxCh];

    // Perekam: ring SPSC (penulis = IO callback, pembaca = thread penulisan berkas)
    static constexpr uint32_t kRecCap = 1u << 19;
    float* recRing = nullptr;
    std::atomic<uint64_t> recWriteIdx{0}, recReadIdx{0}, recDropped{0};
    std::atomic<bool> recActive{false};
    int recOutput = -1;
    FILE* recFile = nullptr;
    std::thread recThread;
    uint64_t recFrames = 0;
    uint32_t recDataBytes = 0;
};

static OSStatus ioProc(AudioObjectID, const AudioTimeStamp*,
                       const AudioBufferList* inData, const AudioTimeStamp* inTime,
                       AudioBufferList* outData, const AudioTimeStamp*, void* client) {
    AHIO* io = static_cast<AHIO*>(client);
    uint32_t frames = outData->mBuffers[0].mDataByteSize / (sizeof(float) * outData->mBuffers[0].mNumberChannels);
    if (frames > (uint32_t)kMaxFrames) frames = kMaxFrames;

    double now = inTime->mSampleTime;
    if (io->started) {
        double expected = io->lastSample + io->lastFrames;
        if (now > expected + 0.5) {
            io->dropouts++;
            io->missing += (uint64_t)(now - expected);
        }
    }
    io->started = true;
    io->lastSample = now;
    io->lastFrames = frames;

    int g = 0;
    for (UInt32 b = 0; b < inData->mNumberBuffers && g < io->nIn; b++) {
        const AudioBuffer& buf = inData->mBuffers[b];
        int nch = (int)buf.mNumberChannels;
        const float* src = static_cast<const float*>(buf.mData);
        for (int c = 0; c < nch && g < io->nIn; c++, g++) {
            for (uint32_t f = 0; f < frames; f++) io->inDeint[g][f] = src[f * nch + c];
        }
    }
    for (int i = g; i < io->nIn; i++) std::memset(io->inDeint[i], 0, sizeof(float) * frames);

    // Insert Audio Unit per input, sebelum engine
    for (int i = 0; i < io->nIn; i++) {
        if (io->insert[i].au) audiohub::processInsert(io->insert[i], io->inDeint[i], (int)frames);
    }

    ah_engine_process(io->engine, io->inPtr, io->outPtr, (int)frames);

    g = 0;
    for (UInt32 b = 0; b < outData->mNumberBuffers; b++) {
        AudioBuffer& buf = outData->mBuffers[b];
        int nch = (int)buf.mNumberChannels;
        float* dst = static_cast<float*>(buf.mData);
        std::memset(dst, 0, buf.mDataByteSize);
        for (int c = 0; c < nch && g < io->nOut; c++, g++) {
            for (uint32_t f = 0; f < frames; f++) dst[f * nch + c] = io->outDeint[g][f];
        }
    }
    if (io->recActive.load(std::memory_order_acquire) && io->recRing) {
        uint64_t w = io->recWriteIdx.load(std::memory_order_relaxed);
        uint64_t r = io->recReadIdx.load(std::memory_order_acquire);
        const float* src = io->outDeint[io->recOutput];
        for (UInt32 f = 0; f < frames; f++) {
            if (w - r >= io->kRecCap) { io->recDropped++; continue; }
            io->recRing[w % io->kRecCap] = src[f];
            w++;
        }
        io->recWriteIdx.store(w, std::memory_order_release);
    }
    io->callbacks++;
    return noErr;
}

static AudioObjectID findDevice(const char* contains) {
    auto a = addr(kAudioHardwarePropertyDevices);
    UInt32 size = 0;
    AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &a, 0, nullptr, &size);
    std::unique_ptr<AudioObjectID[]> ids(new AudioObjectID[size / sizeof(AudioObjectID)]);
    AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, 0, nullptr, &size, ids.get());
    for (UInt32 i = 0; i < size / sizeof(AudioObjectID); i++) {
        if (nameOf(ids[i]).find(contains) != std::string::npos) return ids[i];
    }
    return kAudioObjectUnknown;
}

static AudioObjectID createAggregate(AudioObjectID master, const char* virtualUID1, const char* virtualUID2) {
    auto masterUIDAddr = addr(kAudioDevicePropertyDeviceUID);
    CFStringRef masterUID = nullptr;
    UInt32 sz = sizeof(masterUID);
    AudioObjectGetPropertyData(master, &masterUIDAddr, 0, nullptr, &sz, &masterUID);
    if (!masterUID) return kAudioObjectUnknown;

    auto makeSub = [](CFStringRef uid, bool drift) {
        const void* keys[] = {CFSTR("uid"), CFSTR("drift")};
        const void* vals[] = {uid, drift ? kCFBooleanTrue : kCFBooleanFalse};
        return CFDictionaryCreate(nullptr, keys, vals, 2, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    };
    CFMutableArrayRef subList = CFArrayCreateMutable(nullptr, 3, &kCFTypeArrayCallBacks);
    CFDictionaryRef ps = makeSub(masterUID, false);
    CFArrayAppendValue(subList, ps);
    CFStringRef vUID1 = CFStringCreateWithCString(nullptr, virtualUID1, kCFStringEncodingUTF8);
    CFDictionaryRef vd1 = makeSub(vUID1, true);
    CFArrayAppendValue(subList, vd1);
    CFStringRef vUID2 = nullptr;
    CFDictionaryRef vd2 = nullptr;
    if (virtualUID2 && virtualUID2[0]) {
        vUID2 = CFStringCreateWithCString(nullptr, virtualUID2, kCFStringEncodingUTF8);
        vd2 = makeSub(vUID2, true);
        CFArrayAppendValue(subList, vd2);
    }
    CFArrayRef subs = subList;
    const void* keys[] = {CFSTR("name"), CFSTR("uid"), CFSTR("private"), CFSTR("subdevices"), CFSTR("master")};
    CFStringRef aggUID = CFSTR("com.audiohub.app.aggregate");
    const void* vals[] = {CFSTR("AudioHub Aggregate"), aggUID, kCFBooleanTrue, subs, masterUID};
    CFDictionaryRef desc = CFDictionaryCreate(nullptr, keys, vals, 5, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    AudioObjectID agg = kAudioObjectUnknown;
    OSStatus st = AudioHardwareCreateAggregateDevice(desc, &agg);
    CFRelease(desc);
    CFRelease(subs);
    CFRelease(ps);
    CFRelease(vd1);
    CFRelease(vUID1);
    if (vd2) { CFRelease(vd2); CFRelease(vUID2); }
    CFRelease(masterUID);
    return st == noErr ? agg : kAudioObjectUnknown;
}

static AHIO* openDevice(AudioObjectID dev, AudioObjectID aggregate, char* errBuf, int errLen);

AHIO* ah_io_open(const char* deviceNameContains, char* errBuf, int errLen) {
    AudioObjectID dev = findDevice(deviceNameContains);
    if (dev == kAudioObjectUnknown) {
        if (errBuf && errLen > 0) snprintf(errBuf, errLen, "%s", "Perangkat tidak ditemukan");
        return nullptr;
    }
    return openDevice(dev, kAudioObjectUnknown, errBuf, errLen);
}

AHIO* ah_io_open_with_virtual(const char* masterNameContains, const char* virtualUID, char* errBuf, int errLen) {
    return ah_io_open_with_virtuals(masterNameContains, virtualUID, nullptr, errBuf, errLen);
}

AHIO* ah_io_open_with_virtuals(const char* masterNameContains, const char* virtualUID1, const char* virtualUID2, char* errBuf, int errLen) {
    AudioObjectID master = findDevice(masterNameContains);
    if (master == kAudioObjectUnknown) {
        if (errBuf && errLen > 0) snprintf(errBuf, errLen, "%s", "Perangkat fisik tidak ditemukan");
        return nullptr;
    }
    AudioObjectID agg = createAggregate(master, virtualUID1, virtualUID2);
    if (agg == kAudioObjectUnknown) {
        if (errBuf && errLen > 0) snprintf(errBuf, errLen, "%s", "Gagal membuat aggregate (cek UID virtual device)");
        return nullptr;
    }
    usleep(800000);  // beri waktu HAL menerbitkan aggregate
    AHIO* io = openDevice(agg, agg, errBuf, errLen);
    if (io) {
        // Kanal fisik = kanal master; sisanya dari sub-device virtual
        io->physIn = channelsOf(master, kAudioObjectPropertyScopeInput);
        io->physOut = channelsOf(master, kAudioObjectPropertyScopeOutput);
    }
    return io;
}

static AHIO* openDevice(AudioObjectID dev, AudioObjectID aggregate, char* errBuf, int errLen) {
    auto fail = [&](const char* msg) -> AHIO* {
        if (errBuf && errLen > 0) snprintf(errBuf, errLen, "%s", msg);
        if (aggregate != kAudioObjectUnknown) AudioHardwareDestroyAggregateDevice(aggregate);
        return nullptr;
    };

    int nIn = channelsOf(dev, kAudioObjectPropertyScopeInput);
    int nOut = channelsOf(dev, kAudioObjectPropertyScopeOutput);
    if (nIn < 1 || nOut < 1) return fail("Perangkat tidak punya input dan output sekaligus");
    if (nIn > kMaxCh || nOut > kMaxCh) return fail("Jumlah kanal melebihi batas");

    AHIO* io = new AHIO();
    io->dev = dev;
    io->aggregate = aggregate;
    io->physIn = nIn;
    io->physOut = nOut;
    io->nIn = nIn;
    io->nOut = nOut;
    io->engine = ah_engine_create(nIn, nOut, kMaxFrames);
    for (int i = 0; i < nIn; i++) io->inPtr[i] = io->inDeint[i];
    for (int j = 0; j < nOut; j++) io->outPtr[j] = io->outDeint[j];

    auto bufA = addr(kAudioDevicePropertyBufferFrameSize);
    UInt32 bs = sizeof(UInt32);
    AudioObjectGetPropertyData(dev, &bufA, 0, nullptr, &bs, &io->bufferFrames);
    auto rateA = addr(kAudioDevicePropertyNominalSampleRate);
    bs = sizeof(Float64);
    AudioObjectGetPropertyData(dev, &rateA, 0, nullptr, &bs, &io->sampleRate);

    OSStatus st = AudioDeviceCreateIOProcID(dev, ioProc, io, &io->proc);
    if (st != noErr || !io->proc) {
        ah_engine_destroy(io->engine);
        delete io;
        return fail("Gagal membuat IOProc");
    }
    return io;
}

void ah_io_close(AHIO* io) {
    if (!io) return;
    ah_io_record_stop(io);
    ah_io_stop(io);
    delete[] io->recRing;
    io->recRing = nullptr;
    if (io->proc) AudioDeviceDestroyIOProcID(io->dev, io->proc);
    if (io->aggregate != kAudioObjectUnknown) AudioHardwareDestroyAggregateDevice(io->aggregate);
    ah_engine_destroy(io->engine);
    delete io;
}

int ah_io_start(AHIO* io) {
    if (!io) return -1;
    io->started = false;
    if (AudioDeviceStart(io->dev, io->proc) != noErr) return -1;
    io->running = true;
    return 0;
}

void ah_io_stop(AHIO* io) {
    if (!io) return;
    AudioDeviceStop(io->dev, io->proc);
    io->running = false;
}

void ah_io_set_input_fx(AHIO* io, int input, const AHGateParams* gate, const AHEqParams* eq, const AHCompParams* comp) {
    if (io) ah_engine_set_input_fx(io->engine, input, gate, eq, comp);
}

void ah_io_set_output_limiter(AHIO* io, int output, const AHLimiterParams* limiter) {
    if (io) ah_engine_set_output_limiter(io->engine, output, limiter);
}

void ah_io_set_input_delay(AHIO* io, int input, const AHDelayParams* delay) {
    if (io) ah_engine_set_input_delay(io->engine, input, delay);
}

void ah_io_set_channel_pan(AHIO* io, int input, float pan) {
    if (io) ah_engine_set_channel_pan(io->engine, input, pan);
}

int ah_io_insert_load(AHIO* io, int input, const char* effectName) {
    if (!io || input < 0 || input >= io->nIn) return -1;
    if (io->running) return -2;
    bool ok = audiohub::loadInsert(io->insert[input], effectName, (float)io->sampleRate, kMaxFrames);
    return ok ? 0 : -1;
}

int ah_io_insert_unload(AHIO* io, int input) {
    if (!io || input < 0 || input >= io->nIn) return -1;
    if (io->running) return -2;
    audiohub::unloadInsert(io->insert[input]);
    return 0;
}

void ah_io_insert_name(AHIO* io, int input, char* buf, int len) {
    if (!buf || len <= 0) return;
    buf[0] = 0;
    if (!io || input < 0 || input >= io->nIn) return;
    std::snprintf(buf, len, "%s", io->insert[input].name);
}

int ah_io_list_effects(char* buf, int len) {
    return audiohub::listEffectNames(buf, len);
}

static void writeWavHeader(FILE* f, uint32_t sampleRate, uint32_t dataBytes) {
    uint8_t h[44];
    auto u32 = [](uint8_t* p, uint32_t v) { p[0] = v & 0xff; p[1] = (v >> 8) & 0xff; p[2] = (v >> 16) & 0xff; p[3] = (v >> 24) & 0xff; };
    auto u16 = [](uint8_t* p, uint16_t v) { p[0] = v & 0xff; p[1] = (v >> 8) & 0xff; };
    std::memcpy(h, "RIFF", 4); u32(h + 4, 36 + dataBytes);
    std::memcpy(h + 8, "WAVE", 4);
    std::memcpy(h + 12, "fmt ", 4); u32(h + 16, 16);
    u16(h + 20, 3);                    // IEEE float
    u16(h + 22, 1);                    // mono
    u32(h + 24, sampleRate);
    u32(h + 28, sampleRate * 4);
    u16(h + 32, 4);
    u16(h + 34, 32);
    std::memcpy(h + 36, "data", 4); u32(h + 40, dataBytes);
    std::fwrite(h, 1, 44, f);
}

static void recordWriterLoop(AHIO* io) {
    std::vector<float> chunk(4096);
    while (io->recActive.load(std::memory_order_acquire) || io->recWriteIdx.load() != io->recReadIdx.load()) {
        uint64_t w = io->recWriteIdx.load(std::memory_order_acquire);
        uint64_t r = io->recReadIdx.load(std::memory_order_relaxed);
        uint64_t avail = w - r;
        if (avail == 0) {
            if (!io->recActive.load()) break;
            usleep(5000);
            continue;
        }
        uint64_t n = avail < chunk.size() ? avail : chunk.size();
        for (uint64_t k = 0; k < n; k++) chunk[k] = io->recRing[(r + k) % AHIO::kRecCap];
        std::fwrite(chunk.data(), sizeof(float), n, io->recFile);
        io->recReadIdx.store(r + n, std::memory_order_release);
        io->recFrames += n;
        io->recDataBytes += (uint32_t)(n * sizeof(float));
    }
}

int ah_io_record_start(AHIO* io, int output, const char* path) {
    if (!io || !path || output < 0 || output >= io->nOut) return -1;
    if (io->recActive.load()) return -1;
    if (!io->recRing) io->recRing = new float[AHIO::kRecCap];
    io->recFile = std::fopen(path, "wb");
    if (!io->recFile) return -1;
    writeWavHeader(io->recFile, (uint32_t)io->sampleRate, 0);
    io->recWriteIdx = 0;
    io->recReadIdx = 0;
    io->recDropped = 0;
    io->recFrames = 0;
    io->recDataBytes = 0;
    io->recOutput = output;
    io->recActive.store(true, std::memory_order_release);
    io->recThread = std::thread(recordWriterLoop, io);
    return 0;
}

void ah_io_record_stop(AHIO* io) {
    if (!io || !io->recActive.load()) return;
    io->recActive.store(false, std::memory_order_release);
    if (io->recThread.joinable()) io->recThread.join();
    if (io->recFile) {
        // Tulis ulang header dengan ukuran data yang sebenarnya
        std::fseek(io->recFile, 0, SEEK_SET);
        writeWavHeader(io->recFile, (uint32_t)io->sampleRate, io->recDataBytes);
        std::fclose(io->recFile);
        io->recFile = nullptr;
    }
}

void ah_io_record_stats(AHIO* io, uint64_t* written, uint64_t* dropped) {
    if (!io) return;
    if (written) *written = io->recFrames;
    if (dropped) *dropped = io->recDropped.load();
}

int ah_io_inputs(AHIO* io) { return io ? io->nIn : 0; }
int ah_io_outputs(AHIO* io) { return io ? io->nOut : 0; }

int ah_io_set_route(AHIO* io, int input, int output, int enabled, float sendDb) {
    if (!io) return -1;
    if (input >= io->physIn && output >= io->physOut) return -1;  // virtual → virtual ditolak
    ah_engine_set_route(io->engine, input, output, enabled, sendDb);
    return 0;
}

void ah_io_set_output(AHIO* io, int output, float gainDb, int mute) {
    if (io) ah_engine_set_output(io->engine, output, gainDb, mute);
}

int ah_io_physical_inputs(AHIO* io) { return io ? io->physIn : 0; }

int ah_io_is_alive(AHIO* io) {
    if (!io) return 0;
    auto a = addr(kAudioDevicePropertyDeviceIsAlive);
    UInt32 alive = 0;
    UInt32 size = sizeof(alive);
    if (AudioObjectGetPropertyData(io->dev, &a, 0, nullptr, &size, &alive) != noErr) return 0;
    return alive ? 1 : 0;
}
int ah_io_physical_outputs(AHIO* io) { return io ? io->physOut : 0; }

void ah_io_set_channel(AHIO* io, int input, float gainDb, int mute) {
    if (io) ah_engine_set_channel(io->engine, input, gainDb, mute);
}

void ah_io_peaks(AHIO* io, float* inPeaks, float* outPeaks) {
    if (io) ah_engine_peaks(io->engine, inPeaks, outPeaks);
}

void ah_io_stats(AHIO* io, AHIOStats* out) {
    if (!io || !out) return;
    out->callbacks = io->callbacks.load();
    out->dropouts = io->dropouts.load();
    out->missingFrames = io->missing.load();
    out->inputs = io->nIn;
    out->outputs = io->nOut;
    out->bufferFrames = io->bufferFrames;
    out->sampleRate = io->sampleRate;
}
