#include "audiohub_io.h"

#include "../../../engine/api/audiohub_engine.h"
#include "au_insert.h"
#include "route_policy.h"

#include <CoreAudio/CoreAudio.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <unistd.h>

namespace {

constexpr int kMaxCh = 8;
constexpr int kMaxFx = AH_FX_SLOTS;
constexpr int kMaxFrames = 4096;
// Buffer yang harus sama di semua sub-device agregat. PS22 dan perangkat virtual berjalan di 512 frame.
// Jika agregat (mis. 192) tidak cocok dengan sub-device (512), HAL menyisipkan klik periodik saat ada klien kedua.
constexpr UInt32 kPreferredBufferFrames = 512;
// UID agregat yang terlihat di Core Audio. Dipakai tetap supaya DAW bisa mengingat pilihannya.
// Agregat dengan UID berawalan ini dianggap milik AudioHub dan dihapus saat dibuka atau ditutup.
constexpr const char* kAggregateUID = "com.audiohub.app.aggregate";

AudioObjectPropertyAddress addr(AudioObjectPropertySelector sel, AudioObjectPropertyScope scope = kAudioObjectPropertyScopeGlobal) {
    return {sel, scope, kAudioObjectPropertyElementMain};
}

// Pilihan dari UI. Dibaca saat perangkat dibuka; ah_io_set_preferences yang menulisnya.
UInt32 gPrefBufferFrames = kPreferredBufferFrames;
double gPrefSampleRate = 0;   // 0 = pakai sample rate yang sudah dipasang di perangkat

// Latensi perangkat (properti latency + safety offset) dalam frame, per sisi.
uint32_t deviceLatency(AudioObjectID dev, AudioObjectPropertyScope scope) {
    uint32_t total = 0;
    for (AudioObjectPropertySelector sel : {kAudioDevicePropertyLatency, kAudioDevicePropertySafetyOffset}) {
        auto a = addr(sel, scope);
        UInt32 v = 0, sz = sizeof v;
        if (dev != kAudioObjectUnknown && AudioObjectGetPropertyData(dev, &a, 0, nullptr, &sz, &v) == noErr) total += v;
    }
    return total;
}

OSStatus setBufferFrames(AudioObjectID dev, UInt32 frames) {
    auto a = addr(kAudioDevicePropertyBufferFrameSize);
    return AudioObjectSetPropertyData(dev, &a, 0, nullptr, sizeof frames, &frames);
}

// Daftar sample rate nominal yang didukung perangkat. Mengembalikan jumlah rate yang ditulis.
int supportedRates(AudioObjectID dev, double* rates, int maxRates) {
    auto a = addr(kAudioDevicePropertyAvailableNominalSampleRates);
    UInt32 size = 0;
    if (dev == kAudioObjectUnknown || AudioObjectGetPropertyDataSize(dev, &a, 0, nullptr, &size) != noErr || size == 0) return 0;
    std::unique_ptr<AudioValueRange[]> ranges(new AudioValueRange[size / sizeof(AudioValueRange)]);
    if (AudioObjectGetPropertyData(dev, &a, 0, nullptr, &size, ranges.get()) != noErr) return 0;
    int n = 0;
    for (UInt32 i = 0; i < size / sizeof(AudioValueRange); i++) {
        // Rentang biasanya satu titik (min = max). Rate diskret dimasukkan satu per satu.
        if (ranges[i].mMinimum == ranges[i].mMaximum && n < maxRates && rates) rates[n++] = ranges[i].mMinimum;
    }
    return n;
}

bool supportsRate(AudioObjectID dev, double rate) {
    auto a = addr(kAudioDevicePropertyAvailableNominalSampleRates);
    UInt32 size = 0;
    if (dev == kAudioObjectUnknown || AudioObjectGetPropertyDataSize(dev, &a, 0, nullptr, &size) != noErr || size == 0) return false;
    std::unique_ptr<AudioValueRange[]> ranges(new AudioValueRange[size / sizeof(AudioValueRange)]);
    if (AudioObjectGetPropertyData(dev, &a, 0, nullptr, &size, ranges.get()) != noErr) return false;
    for (UInt32 i = 0; i < size / sizeof(AudioValueRange); i++) {
        if (rate >= ranges[i].mMinimum - 0.5 && rate <= ranges[i].mMaximum + 0.5) return true;
    }
    return false;
}

// Memasang sample rate di perangkat. rate <= 0 tidak mengubah apa pun. Gagal jika perangkat tidak mendukung rate itu.
bool applyRate(AudioObjectID dev, double rate, char* errBuf, int errLen) {
    if (rate <= 0) return true;
    if (dev == kAudioObjectUnknown) {
        if (errBuf && errLen > 0) snprintf(errBuf, errLen, "%s", "Perangkat tidak ditemukan untuk mengatur sample rate");
        return false;
    }
    if (!supportsRate(dev, rate)) {
        if (errBuf && errLen > 0) snprintf(errBuf, errLen, "Perangkat tidak mendukung %.0f Hz", rate);
        return false;
    }
    auto a = addr(kAudioDevicePropertyNominalSampleRate);
    Float64 want = rate;
    OSStatus st = AudioObjectSetPropertyData(dev, &a, 0, nullptr, sizeof want, &want);
    if (st != noErr) {
        if (errBuf && errLen > 0) snprintf(errBuf, errLen, "Gagal mengatur sample rate %.0f Hz (OSStatus %d)", rate, (int)st);
        return false;
    }
    // Perubahan rate berlaku secara asinkron. Tunggu sampai perangkat melaporkan rate baru, maksimal 2 detik.
    for (int i = 0; i < 40; i++) {
        Float64 now = 0;
        UInt32 sz = sizeof now;
        if (AudioObjectGetPropertyData(dev, &a, 0, nullptr, &sz, &now) == noErr && std::fabs(now - rate) < 0.5) return true;
        usleep(50000);
    }
    if (errBuf && errLen > 0) snprintf(errBuf, errLen, "Perangkat tidak berpindah ke %.0f Hz", rate);
    return false;
}

// Rentang buffer yang dilaporkan perangkat. False jika properti tidak tersedia.
bool bufferRangeOf(AudioObjectID dev, UInt32* lo, UInt32* hi) {
    auto a = addr(kAudioDevicePropertyBufferFrameSizeRange);
    AudioValueRange r{};
    UInt32 sz = sizeof r;
    if (dev == kAudioObjectUnknown || AudioObjectGetPropertyData(dev, &a, 0, nullptr, &sz, &r) != noErr) return false;
    *lo = (UInt32)r.mMinimum;
    *hi = (UInt32)r.mMaximum;
    return true;
}

// Perangkat dari UID; kAudioObjectUnknown jika tidak ditemukan.
AudioObjectID deviceForUID(const char* uid) {
    if (!uid || !uid[0]) return kAudioObjectUnknown;
    auto a = addr(kAudioHardwarePropertyTranslateUIDToDevice);
    CFStringRef cf = CFStringCreateWithCString(nullptr, uid, kCFStringEncodingUTF8);
    AudioObjectID dev = kAudioObjectUnknown;
    UInt32 sz = sizeof dev;
    AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, sizeof cf, &cf, &sz, &dev);
    CFRelease(cf);
    return dev;
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
    // Jumlah kanal tiap virtual device, per sisi. Nol jika device tidak dipakai.
    int virt1In = 0, virt1Out = 0, virt2In = 0, virt2Out = 0;
    uint32_t bufferFrames = 0;
    double sampleRate = 0;
    // Perangkat fisik utama (bukan agregat). Dipakai untuk latensi dan kontrol hardware.
    AudioObjectID master = kAudioObjectUnknown;
    uint32_t inLatency = 0, outLatency = 0;   // latensi perangkat dalam frame (latency + safety offset)

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
    // Rantai FX: insert[input][slot]. fxOrder[input][k] = slot yang diproses ke-k.
    // fxOn = 0 melewati slot. Urutan dan fxOn dibaca di jalur audio, diubah dari UI (atomic).
    audiohub::AUInsert insert[kMaxCh][kMaxFx];
    std::atomic<bool> fxOn[kMaxCh][kMaxFx];
    std::atomic<int> fxOrder[kMaxCh][kMaxFx];

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

    // Rantai FX per input, sebelum engine: berurutan sesuai fxOrder, hanya slot berisi AU yang aktif
    for (int i = 0; i < io->nIn; i++) {
        for (int k = 0; k < kMaxFx; k++) {
            int s = io->fxOrder[i][k].load(std::memory_order_relaxed);
            if (io->fxOn[i][s].load(std::memory_order_relaxed) && io->insert[i][s].au)
                audiohub::processInsert(io->insert[i][s], io->inDeint[i], (int)frames);
        }
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

// Menghapus agregat AudioHub yang masih tertinggal, misalnya setelah crash. Hanya UID berawalan
// kAggregateUID yang disentuh; perangkat lain tidak diubah.
static void destroyStaleAggregates() {
    auto a = addr(kAudioHardwarePropertyDevices);
    UInt32 size = 0;
    AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &a, 0, nullptr, &size);
    std::unique_ptr<AudioObjectID[]> ids(new AudioObjectID[size / sizeof(AudioObjectID)]);
    AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, 0, nullptr, &size, ids.get());
    for (UInt32 i = 0; i < size / sizeof(AudioObjectID); i++) {
        auto uidAddr = addr(kAudioDevicePropertyDeviceUID);
        CFStringRef uid = nullptr;
        UInt32 sz = sizeof(uid);
        if (AudioObjectGetPropertyData(ids[i], &uidAddr, 0, nullptr, &sz, &uid) != noErr || !uid) continue;
        char buf[256] = {0};
        CFStringGetCString(uid, buf, sizeof(buf), kCFStringEncodingUTF8);
        CFRelease(uid);
        if (strncmp(buf, kAggregateUID, strlen(kAggregateUID)) == 0) AudioHardwareDestroyAggregateDevice(ids[i]);
    }
}

static AudioObjectID createAggregate(AudioObjectID master, const char* virtualUID1, const char* virtualUID2) {
    destroyStaleAggregates();
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
    // Clock master = perangkat virtual 1, supaya clock agregat tidak mengikuti PS22.
    // PS22 tetap sub-device pertama agar urutan kanal tidak berubah,
    // dengan drift compensation karena clock-nya tidak lagi menjadi master.
    CFMutableArrayRef subList = CFArrayCreateMutable(nullptr, 3, &kCFTypeArrayCallBacks);
    CFDictionaryRef ps = makeSub(masterUID, true);
    CFArrayAppendValue(subList, ps);
    CFStringRef vUID1 = CFStringCreateWithCString(nullptr, virtualUID1, kCFStringEncodingUTF8);
    CFDictionaryRef vd1 = makeSub(vUID1, false);
    CFArrayAppendValue(subList, vd1);
    CFStringRef vUID2 = nullptr;
    CFDictionaryRef vd2 = nullptr;
    if (virtualUID2 && virtualUID2[0]) {
        vUID2 = CFStringCreateWithCString(nullptr, virtualUID2, kCFStringEncodingUTF8);
        vd2 = makeSub(vUID2, true);
        CFArrayAppendValue(subList, vd2);
    }
    CFArrayRef subs = subList;
    // Agregat terlihat (private = false) agar DAW bisa memilihnya. UID lama dengan nama yang sama sudah
    // dihapus di destroyStaleAggregates, karena HAL menolak membuat UID yang sudah ada (kAudioHardwareIllegalOperationError).
    CFStringRef aggUID = CFStringCreateWithCString(nullptr, kAggregateUID, kCFStringEncodingUTF8);
    // "master" saja tidak cukup: Core Audio memilih clock dari perangkat fisik jika "clock" tidak diset.
    // "clock" mengendalikan time base agregat (AudioHardware.h), jadi diset eksplisit ke virtual 1.
    const void* keys[] = {CFSTR("name"), CFSTR("uid"), CFSTR("private"), CFSTR("subdevices"), CFSTR("master"), CFSTR("clock")};
    const void* vals[] = {CFSTR("AudioHub Aggregate"), aggUID, kCFBooleanFalse, subs, vUID1, vUID1};
    CFDictionaryRef desc = CFDictionaryCreate(nullptr, keys, vals, 6, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    AudioObjectID agg = kAudioObjectUnknown;
    OSStatus st = AudioHardwareCreateAggregateDevice(desc, &agg);
    CFRelease(desc);
    CFRelease(aggUID);
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
    if (!applyRate(dev, gPrefSampleRate, errBuf, errLen)) return nullptr;
    setBufferFrames(dev, gPrefBufferFrames);
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
    // Sample rate harus sama di master dan virtual sebelum agregat dibuat. Satu perangkat yang menolak menggagalkan pembukaan.
    if (!applyRate(master, gPrefSampleRate, errBuf, errLen)) return nullptr;
    if (!applyRate(deviceForUID(virtualUID1), gPrefSampleRate, errBuf, errLen)) return nullptr;
    if (virtualUID2 && virtualUID2[0] && !applyRate(deviceForUID(virtualUID2), gPrefSampleRate, errBuf, errLen)) return nullptr;
    AudioObjectID agg = createAggregate(master, virtualUID1, virtualUID2);
    if (agg == kAudioObjectUnknown) {
        if (errBuf && errLen > 0) snprintf(errBuf, errLen, "%s", "Gagal membuat aggregate (cek UID virtual device)");
        return nullptr;
    }
    usleep(800000);  // beri waktu HAL menerbitkan aggregate
    // Semua sub-device harus memakai buffer yang sama sebelum IO dimulai, termasuk perangkat virtual.
    // Jika tidak, agregat berjalan di 192 sementara sub-device tetap 512, dan HAL menyisipkan klik.
    setBufferFrames(master, gPrefBufferFrames);
    setBufferFrames(deviceForUID(virtualUID1), gPrefBufferFrames);
    if (virtualUID2 && virtualUID2[0]) setBufferFrames(deviceForUID(virtualUID2), gPrefBufferFrames);
    setBufferFrames(agg, gPrefBufferFrames);
    AHIO* io = openDevice(agg, agg, errBuf, errLen);
    if (io) {
        // Kanal fisik = kanal master; sisanya dari sub-device virtual
        io->physIn = channelsOf(master, kAudioObjectPropertyScopeInput);
        io->physOut = channelsOf(master, kAudioObjectPropertyScopeOutput);
        io->master = master;
        io->inLatency = deviceLatency(master, kAudioObjectPropertyScopeInput);
        io->outLatency = deviceLatency(master, kAudioObjectPropertyScopeOutput);
        // Urutan kanal di agregat: fisik, virtual 1, virtual 2
        AudioObjectID v1 = deviceForUID(virtualUID1);
        io->virt1In = channelsOf(v1, kAudioObjectPropertyScopeInput);
        io->virt1Out = channelsOf(v1, kAudioObjectPropertyScopeOutput);
        if (virtualUID2 && virtualUID2[0]) {
            AudioObjectID v2 = deviceForUID(virtualUID2);
            io->virt2In = channelsOf(v2, kAudioObjectPropertyScopeInput);
            io->virt2Out = channelsOf(v2, kAudioObjectPropertyScopeOutput);
        }
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
    for (int i = 0; i < kMaxCh; i++)
        for (int k = 0; k < kMaxFx; k++) io->fxOrder[i][k].store(k);
    io->dev = dev;
    io->aggregate = aggregate;
    io->physIn = nIn;
    io->physOut = nOut;
    io->nIn = nIn;
    io->nOut = nOut;
    for (int i = 0; i < nIn; i++) io->inPtr[i] = io->inDeint[i];
    for (int j = 0; j < nOut; j++) io->outPtr[j] = io->outDeint[j];

    auto bufA = addr(kAudioDevicePropertyBufferFrameSize);
    UInt32 bs = sizeof(UInt32);
    AudioObjectGetPropertyData(dev, &bufA, 0, nullptr, &bs, &io->bufferFrames);
    auto rateA = addr(kAudioDevicePropertyNominalSampleRate);
    bs = sizeof(Float64);
    AudioObjectGetPropertyData(dev, &rateA, 0, nullptr, &bs, &io->sampleRate);
    // Engine dibuat setelah rate terbaca, supaya koefisien DSP mengikuti rate perangkat yang sebenarnya.
    io->engine = ah_engine_create_rate(nIn, nOut, kMaxFrames, io->sampleRate);
    // Untuk pembukaan tunggal, perangkat ini adalah master. open_with_virtuals menimpanya dengan perangkat fisik.
    io->master = dev;
    io->inLatency = deviceLatency(dev, kAudioObjectPropertyScopeInput);
    io->outLatency = deviceLatency(dev, kAudioObjectPropertyScopeOutput);

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
    // AU harus dilepas sebelum AHIO dihapus; AUInsert tidak punya destruktor.
    for (int i = 0; i < kMaxCh; i++)
        for (int k = 0; k < kMaxFx; k++) audiohub::unloadInsert(io->insert[i][k]);
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

namespace {

bool validSlot(const AHIO* io, int input, int slot) {
    return io && input >= 0 && input < io->nIn && slot >= 0 && slot < kMaxFx;
}

}  // namespace

int ah_io_fx_load(AHIO* io, int input, int slot, const char* effectName) {
    if (!validSlot(io, input, slot) || !effectName) return -1;
    if (io->running) return -2;
    bool ok = audiohub::loadInsert(io->insert[input][slot], effectName, (float)io->sampleRate, kMaxFrames);
    io->fxOn[input][slot].store(ok);
    return ok ? 0 : -1;
}

int ah_io_fx_unload(AHIO* io, int input, int slot) {
    if (!validSlot(io, input, slot)) return -1;
    if (io->running) return -2;
    audiohub::unloadInsert(io->insert[input][slot]);
    io->fxOn[input][slot].store(false);
    return 0;
}

int ah_io_fx_info(AHIO* io, int input, int slot, char* name, int nameLen, int* enabled) {
    if (name && nameLen > 0) name[0] = 0;
    if (enabled) *enabled = 0;
    if (!validSlot(io, input, slot)) return -1;
    const audiohub::AUInsert& s = io->insert[input][slot];
    if (name && nameLen > 0) std::snprintf(name, nameLen, "%s", s.name);
    if (enabled) *enabled = io->fxOn[input][slot].load() ? 1 : 0;
    return s.au ? 1 : 0;
}

int ah_io_fx_set_enabled(AHIO* io, int input, int slot, int enabled) {
    if (!validSlot(io, input, slot)) return -1;
    io->fxOn[input][slot].store(enabled != 0);
    return 0;
}

int ah_io_fx_set_order(AHIO* io, int input, const int* order, int count) {
    if (!io || !order || input < 0 || input >= io->nIn || count != kMaxFx) return -1;
    bool seen[kMaxFx] = {false};
    for (int k = 0; k < kMaxFx; k++) {
        int s = order[k];
        if (s < 0 || s >= kMaxFx || seen[s]) return -1;
        seen[s] = true;
    }
    for (int k = 0; k < kMaxFx; k++) io->fxOrder[input][k].store(order[k]);
    return 0;
}

int ah_io_fx_param_count(AHIO* io, int input, int slot) {
    if (!validSlot(io, input, slot)) return -1;
    return audiohub::paramCount(io->insert[input][slot]);
}

int ah_io_fx_param_get(AHIO* io, int input, int slot, int index, AHFxParam* out) {
    if (!out || !validSlot(io, input, slot)) return -1;
    audiohub::ParamInfo p;
    if (!audiohub::paramInfo(io->insert[input][slot], index, p)) return -1;
    out->id = p.id;
    std::snprintf(out->name, sizeof out->name, "%s", p.name);
    out->minValue = p.minValue;
    out->maxValue = p.maxValue;
    out->defaultValue = p.defaultValue;
    out->value = p.value;
    return 0;
}

int ah_io_fx_param_set(AHIO* io, int input, int slot, unsigned int paramId, float value) {
    if (!validSlot(io, input, slot)) return -1;
    return audiohub::setParamValue(io->insert[input][slot], paramId, value) ? 0 : -1;
}

int ah_io_fx_state_get(AHIO* io, int input, int slot, unsigned char* buf, int len) {
    if (!validSlot(io, input, slot)) return -1;
    return audiohub::getClassInfo(io->insert[input][slot], buf, len);
}

int ah_io_fx_state_set(AHIO* io, int input, int slot, const unsigned char* data, int len) {
    if (!validSlot(io, input, slot)) return -1;
    if (io->running) return -2;
    return audiohub::setClassInfo(io->insert[input][slot], data, len) ? 0 : -1;
}

// Insert lama (satu per input) = slot 0 rantai FX.
int ah_io_insert_load(AHIO* io, int input, const char* effectName) {
    return ah_io_fx_load(io, input, 0, effectName);
}

int ah_io_insert_unload(AHIO* io, int input) {
    return ah_io_fx_unload(io, input, 0);
}

void ah_io_insert_name(AHIO* io, int input, char* buf, int len) {
    if (!buf || len <= 0) return;
    buf[0] = 0;
    if (!validSlot(io, input, 0)) return;
    std::snprintf(buf, len, "%s", io->insert[input][0].name);
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

static audiohub::ChannelLayout layoutOf(const AHIO* io) {
    return {io->physIn, io->virt1In, io->virt2In, io->physOut, io->virt1Out, io->virt2Out};
}

int ah_io_route_allowed(AHIO* io, int input, int output) {
    if (!io) return 0;
    return audiohub::routeAllowed(input, output, layoutOf(io)) ? 1 : 0;
}

void ah_io_channel_owner(AHIO* io, int output, int channel, int* owner, int* local) {
    if (owner) *owner = -1;
    if (local) *local = 0;
    if (!io) return;
    audiohub::ChannelLayout L = layoutOf(io);
    int phys = output ? L.physOut : L.physIn;
    int v1 = output ? L.virt1Out : L.virt1In;
    int v2 = output ? L.virt2Out : L.virt2In;
    if (channel < 0 || channel >= phys + v1 + v2) return;
    int o = audiohub::channelOwner(channel, phys, v1);
    if (owner) *owner = o;
    if (local) *local = o == 0 ? channel + 1 : (o == 1 ? channel - phys + 1 : channel - phys - v1 + 1);
}

int ah_io_set_route(AHIO* io, int input, int output, int enabled, float sendDb) {
    if (!io) return -1;
    // Virtual → virtual ditolak jika dari perangkat virtual yang sama (akan membentuk loop di driver)
    if (!audiohub::routeAllowed(input, output, layoutOf(io))) return -1;
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
    out->inputLatencyFrames = io->inLatency;
    out->outputLatencyFrames = io->outLatency;
}

// Kontrol volume dan mute hardware pada perangkat fisik utama, per kanal output (1-based).
// Channel 0 = master. Mengembalikan 0 jika ada, -1 jika properti tidak tersedia.
int ah_io_hw_output_get(AHIO* io, int channel, float* volume, int* mute) {
    if (!io || io->master == kAudioObjectUnknown) return -1;
    UInt32 el = (UInt32)channel;
    bool any = false;
    if (volume) {
        auto a = addr(kAudioDevicePropertyVolumeScalar, kAudioObjectPropertyScopeOutput);
        a.mElement = el;
        if (AudioObjectHasProperty(io->master, &a)) {
            Float32 v = 0; UInt32 sz = sizeof v;
            if (AudioObjectGetPropertyData(io->master, &a, 0, nullptr, &sz, &v) == noErr) { *volume = v; any = true; }
        }
    }
    if (mute) {
        auto a = addr(kAudioDevicePropertyMute, kAudioObjectPropertyScopeOutput);
        a.mElement = el;
        if (AudioObjectHasProperty(io->master, &a)) {
            UInt32 m = 0, sz = sizeof m;
            if (AudioObjectGetPropertyData(io->master, &a, 0, nullptr, &sz, &m) == noErr) { *mute = m ? 1 : 0; any = true; }
        }
    }
    return any ? 0 : -1;
}

// Mengatur volume dan/atau mute. volume < 0 atau mute < 0 berarti tidak diubah. Mengembalikan 0 jika ada yang berhasil.
int ah_io_hw_output_set(AHIO* io, int channel, float volume, int mute) {
    if (!io || io->master == kAudioObjectUnknown) return -1;
    UInt32 el = (UInt32)channel;
    bool ok = false;
    if (volume >= 0) {
        auto a = addr(kAudioDevicePropertyVolumeScalar, kAudioObjectPropertyScopeOutput);
        a.mElement = el;
        if (AudioObjectHasProperty(io->master, &a)) {
            Float32 v = volume > 1 ? 1 : volume;
            ok = AudioObjectSetPropertyData(io->master, &a, 0, nullptr, sizeof v, &v) == noErr || ok;
        }
    }
    if (mute >= 0) {
        auto a = addr(kAudioDevicePropertyMute, kAudioObjectPropertyScopeOutput);
        a.mElement = el;
        if (AudioObjectHasProperty(io->master, &a)) {
            UInt32 m = mute ? 1 : 0;
            ok = AudioObjectSetPropertyData(io->master, &a, 0, nullptr, sizeof m, &m) == noErr || ok;
        }
    }
    return ok ? 0 : -1;
}

void ah_io_set_preferences(uint32_t bufferFrames, double sampleRate) {
    gPrefBufferFrames = bufferFrames > 0 ? (UInt32)bufferFrames : kPreferredBufferFrames;
    gPrefSampleRate = sampleRate > 0 ? sampleRate : 0;
}

int ah_io_device_capabilities(const char* deviceNameContains, double* rates, int maxRates,
                              uint32_t* bufferMin, uint32_t* bufferMax) {
    AudioObjectID dev = findDevice(deviceNameContains);
    if (dev == kAudioObjectUnknown) return -1;
    if (bufferMin && bufferMax) {
        UInt32 lo = 0, hi = 0;
        if (bufferRangeOf(dev, &lo, &hi)) { *bufferMin = lo; *bufferMax = hi; }
        else { *bufferMin = 0; *bufferMax = 0; }
    }
    return supportedRates(dev, rates, maxRates);
}
