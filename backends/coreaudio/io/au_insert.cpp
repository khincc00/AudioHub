#include "au_insert.h"

#include <dlfcn.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace audiohub {

namespace {

// Awalan nama effect VST2 di daftar, mis. "VST2: reafir". Tanpa awalan ini, nama dicari sebagai AU.
const char kVstPrefix[] = "VST2: ";

// ---- VST 2.4: bagian ABI yang dipakai host. Tidak memakai SDK, jadi header VST tidak perlu ada di repo. ----

struct VstEffect;
using VstDispatcher = intptr_t (*)(VstEffect*, int32_t, int32_t, intptr_t, void*, float);
using VstSetParameter = void (*)(VstEffect*, int32_t, float);
using VstGetParameter = float (*)(VstEffect*, int32_t);
using VstProcessReplacing = void (*)(VstEffect*, float**, float**, int32_t);
using VstHostCallback = intptr_t (*)(VstEffect*, int32_t, int32_t, intptr_t, void*, float);
using VstMainFn = VstEffect* (*)(VstHostCallback);

// Urutan dan ukuran field harus sama dengan struct AEffect di VST 2.4.
struct VstEffect {
    int32_t magic;
    VstDispatcher dispatcher;
    void* process;
    VstSetParameter setParameter;
    VstGetParameter getParameter;
    int32_t numPrograms;
    int32_t numParams;
    int32_t numInputs;
    int32_t numOutputs;
    int32_t flags;
    intptr_t resvd1;
    intptr_t resvd2;
    int32_t initialDelay;
    int32_t realQualities;
    int32_t offQualities;
    float ioRatio;
    void* object;
    void* user;
    int32_t uniqueID;
    int32_t version;
    VstProcessReplacing processReplacing;
    void* processDoubleReplacing;
    char future[56];
};

constexpr int32_t kVstMagic = 0x56737450;  // 'VstP'
constexpr int32_t kEffOpen = 0;
constexpr int32_t kEffClose = 1;
constexpr int32_t kEffGetParamLabel = 6;
constexpr int32_t kEffGetParamDisplay = 7;
constexpr int32_t kEffGetParamName = 8;
constexpr int32_t kEffSetSampleRate = 10;
constexpr int32_t kEffSetBlockSize = 11;
constexpr int32_t kEffMainsChanged = 12;
constexpr int32_t kEffStartProcess = 71;
constexpr int32_t kEffStopProcess = 72;
constexpr int32_t kAudioMasterVersion = 1;
constexpr int32_t kAudioMasterGetSampleRate = 17;
constexpr int32_t kAudioMasterGetBlockSize = 18;
constexpr int kVstTextLen = 128;

// Nilai yang dilaporkan host ke plugin lewat audioMaster (dibaca saat plugin dibuat).
float gVstSampleRate = 48000.0f;
int32_t gVstBlockSize = 512;

intptr_t hostCallback(VstEffect*, int32_t opcode, int32_t, intptr_t, void*, float) {
    switch (opcode) {
        case kAudioMasterVersion: return 2400;
        case kAudioMasterGetSampleRate: return (intptr_t)gVstSampleRate;
        case kAudioMasterGetBlockSize: return (intptr_t)gVstBlockSize;
        default: return 0;
    }
}

// Path file VST2 yang didaftarkan dari aplikasi.
std::vector<std::string> gVstPaths;

// Nama file tanpa akhiran plugin: "/…/reafir.vst.dylib" menjadi "reafir".
std::string stemOf(const std::string& path) {
    size_t slash = path.find_last_of('/');
    std::string base = slash == std::string::npos ? path : path.substr(slash + 1);
    static const char* suffixes[] = {".vst.dylib", ".vst", ".dylib"};
    for (const char* s : suffixes) {
        size_t n = std::strlen(s);
        if (base.size() > n && base.compare(base.size() - n, n, s) == 0) return base.substr(0, base.size() - n);
    }
    return base;
}

std::string vstDisplayName(const std::string& path) { return kVstPrefix + stemOf(path); }

const std::string* findVstPath(const char* name) {
    for (const auto& p : gVstPaths) {
        if (vstDisplayName(p) == name) return &p;
    }
    return nullptr;
}

}  // namespace

// Satu plugin VST2 yang dimuat. Hanya dibuat di loadVst dan dihapus di unloadVst.
struct Vst2Slot {
    VstEffect* fx = nullptr;
    // Buffer mono ke stereo: input kiri dan kanan sama, keluaran kiri yang dipakai.
    std::vector<float> in, outL, outR;
    // Nilai awal parameter setelah plugin dibuka, dipakai sebagai default.
    std::vector<float> defaults;
};

namespace {

constexpr unsigned char kVstStateMagic[4] = {'V', 'S', 'T', '2'};

OSStatus renderInput(void* refCon, AudioUnitRenderActionFlags*, const AudioTimeStamp*, UInt32, UInt32 frames, AudioBufferList* ioData) {
    AUInsert* s = static_cast<AUInsert*>(refCon);
    if (ioData->mNumberBuffers < 1) return noErr;
    float* dst = static_cast<float*>(ioData->mBuffers[0].mData);
    if (!dst || !s->source) return noErr;
    std::memcpy(dst, s->source, sizeof(float) * frames);
    return noErr;
}

AudioStreamBasicDescription monoFloat(float sampleRate) {
    AudioStreamBasicDescription f{};
    f.mSampleRate = sampleRate;
    f.mFormatID = kAudioFormatLinearPCM;
    f.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked | kAudioFormatFlagIsNonInterleaved;
    f.mBytesPerPacket = 4;
    f.mFramesPerPacket = 1;
    f.mBytesPerFrame = 4;
    f.mChannelsPerFrame = 1;
    f.mBitsPerChannel = 32;
    return f;
}

// ---- VST2: muat, proses, dan lepas ----

bool loadVst(AUInsert& slot, const char* name, float sampleRate, int maxFrames) {
    const std::string* path = findVstPath(name);
    if (!path) return false;
    void* lib = dlopen(path->c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!lib) return false;
    VstMainFn entry = reinterpret_cast<VstMainFn>(dlsym(lib, "VSTPluginMain"));
    if (!entry) entry = reinterpret_cast<VstMainFn>(dlsym(lib, "main"));
    if (!entry) return false;

    gVstSampleRate = sampleRate;
    gVstBlockSize = maxFrames;
    VstEffect* fx = entry(hostCallback);
    if (!fx || fx->magic != kVstMagic) return false;
    // Hanya plugin efek mono/stereo yang didukung. Instrumen (tanpa input) ditolak.
    if (!fx->processReplacing || fx->numInputs < 1 || fx->numInputs > 2 || fx->numOutputs < 1) return false;

    fx->dispatcher(fx, kEffOpen, 0, 0, nullptr, 0);
    fx->dispatcher(fx, kEffSetSampleRate, 0, 0, nullptr, sampleRate);
    fx->dispatcher(fx, kEffSetBlockSize, 0, maxFrames, nullptr, 0);
    fx->dispatcher(fx, kEffMainsChanged, 0, 1, nullptr, 0);
    fx->dispatcher(fx, kEffStartProcess, 0, 0, nullptr, 0);

    Vst2Slot* v = new Vst2Slot;
    v->fx = fx;
    v->in.assign(4096, 0.0f);
    v->outL.assign(4096, 0.0f);
    v->outR.assign(4096, 0.0f);
    v->defaults.resize(fx->numParams > 0 ? (size_t)fx->numParams : 0);
    for (int i = 0; i < fx->numParams; i++) v->defaults[(size_t)i] = fx->getParameter(fx, i);
    slot.vst = v;
    std::snprintf(slot.name, sizeof slot.name, "%s", name);
    return true;
}

void unloadVst(Vst2Slot* v) {
    if (v->fx) {
        v->fx->dispatcher(v->fx, kEffStopProcess, 0, 0, nullptr, 0);
        v->fx->dispatcher(v->fx, kEffMainsChanged, 0, 0, nullptr, 0);
        v->fx->dispatcher(v->fx, kEffClose, 0, 0, nullptr, 0);
    }
    // Library tidak di-dlclose: plugin REAPER menyimpan state global (SWELL) yang bisa crash saat dilepas.
    delete v;
}

void processVst(Vst2Slot& v, float* buf, int frames) {
    if (!v.fx || !v.fx->processReplacing || frames <= 0 || frames > (int)v.in.size()) return;
    std::memcpy(v.in.data(), buf, sizeof(float) * frames);
    float* ins[2] = {v.in.data(), v.in.data()};
    float* outs[2] = {v.outL.data(), v.outR.data()};
    v.fx->processReplacing(v.fx, ins, outs, frames);
    std::memcpy(buf, v.outL.data(), sizeof(float) * frames);
}

bool vstParamInfo(const Vst2Slot& v, int index, ParamInfo& out) {
    if (!v.fx || index < 0 || index >= v.fx->numParams) return false;
    char name[kVstTextLen] = {0};
    v.fx->dispatcher(v.fx, kEffGetParamName, index, 0, name, 0);
    out = ParamInfo{};
    out.id = (unsigned int)index;
    if (name[0]) {
        std::snprintf(out.name, sizeof out.name, "%s", name);
    } else {
        std::snprintf(out.name, sizeof out.name, "Parameter %d", index + 1);
    }
    out.minValue = 0;
    out.maxValue = 1;
    out.defaultValue = v.defaults[(size_t)index];
    out.value = v.fx->getParameter(v.fx, index);
    return true;
}

int vstStateGet(const Vst2Slot& v, unsigned char* buf, int len) {
    if (!v.fx) return -1;
    int32_t count = v.fx->numParams;
    int n = 8 + 4 * count;
    if (buf && len >= n) {
        std::memcpy(buf, kVstStateMagic, 4);
        std::memcpy(buf + 4, &count, 4);
        for (int32_t i = 0; i < count; i++) {
            float value = v.fx->getParameter(v.fx, i);
            std::memcpy(buf + 8 + 4 * i, &value, 4);
        }
    }
    return n;
}

bool vstStateSet(Vst2Slot& v, const unsigned char* data, int len) {
    if (!v.fx || !data || len < 8 || std::memcmp(data, kVstStateMagic, 4) != 0) return false;
    int32_t count = 0;
    std::memcpy(&count, data + 4, 4);
    if (count != v.fx->numParams || len < 8 + 4 * count) return false;
    for (int32_t i = 0; i < count; i++) {
        float value = 0;
        std::memcpy(&value, data + 8 + 4 * i, 4);
        v.fx->setParameter(v.fx, i, value);
    }
    return true;
}

}  // namespace

void setVst2Paths(const char* paths) {
    gVstPaths.clear();
    if (!paths) return;
    std::string text(paths);
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        if (end > start) gVstPaths.push_back(text.substr(start, end - start));
        start = end + 1;
    }
}

int listEffectNames(char* buf, int len) {
    if (!buf || len <= 0) return 0;
    buf[0] = 0;
    AudioComponentDescription desc{};
    desc.componentType = kAudioUnitType_Effect;
    AudioComponent comp = nullptr;
    int written = 0;
    while ((comp = AudioComponentFindNext(comp, &desc)) != nullptr) {
        CFStringRef cfName = nullptr;
        if (AudioComponentCopyName(comp, &cfName) != noErr || !cfName) continue;
        char name[256] = {0};
        CFStringGetCString(cfName, name, sizeof name, kCFStringEncodingUTF8);
        CFRelease(cfName);
        int need = (int)std::strlen(name) + 1;
        if (written + need >= len) break;
        std::strcat(buf, name);
        std::strcat(buf, "\n");
        written += need;
    }
    for (const auto& p : gVstPaths) {
        std::string name = vstDisplayName(p);
        int need = (int)name.size() + 1;
        if (written + need >= len) break;
        std::strcat(buf, name.c_str());
        std::strcat(buf, "\n");
        written += need;
    }
    return written;
}

bool loadInsert(AUInsert& slot, const char* name, float sampleRate, int maxFrames) {
    if (isLoaded(slot)) unloadInsert(slot);
    if (maxFrames > 4096) maxFrames = 4096;
    if (std::strncmp(name, kVstPrefix, sizeof kVstPrefix - 1) == 0) return loadVst(slot, name, sampleRate, maxFrames);
    AudioComponentDescription desc{};
    desc.componentType = kAudioUnitType_Effect;
    AudioComponent comp = nullptr;
    while ((comp = AudioComponentFindNext(comp, &desc)) != nullptr) {
        CFStringRef cfName = nullptr;
        if (AudioComponentCopyName(comp, &cfName) != noErr || !cfName) continue;
        char candidate[256] = {0};
        CFStringGetCString(cfName, candidate, sizeof candidate, kCFStringEncodingUTF8);
        CFRelease(cfName);
        if (std::strcmp(candidate, name) == 0) break;
    }
    if (!comp) return false;

    AudioUnit au = nullptr;
    if (AudioComponentInstanceNew(comp, &au) != noErr || !au) return false;
    AudioStreamBasicDescription fmt = monoFloat(sampleRate);
    UInt32 maxSlice = (UInt32)maxFrames;
    bool ok = AudioUnitSetProperty(au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &fmt, sizeof fmt) == noErr
        && AudioUnitSetProperty(au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &fmt, sizeof fmt) == noErr
        && AudioUnitSetProperty(au, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &maxSlice, sizeof maxSlice) == noErr;
    slot.au = au;
    std::snprintf(slot.name, sizeof slot.name, "%s", name);
    if (ok) {
        AURenderCallbackStruct cb{renderInput, &slot};
        ok = AudioUnitSetProperty(au, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof cb) == noErr
            && AudioUnitInitialize(au) == noErr;
    }
    if (!ok) {
        AudioComponentInstanceDispose(au);
        slot.au = nullptr;
        slot.name[0] = 0;
        return false;
    }
    slot.ts = AudioTimeStamp{};
    slot.ts.mFlags = kAudioTimeStampSampleTimeValid;
    return true;
}

void unloadInsert(AUInsert& slot) {
    if (slot.vst) {
        unloadVst(slot.vst);
        slot.vst = nullptr;
    }
    if (slot.au) {
        AudioUnitUninitialize(slot.au);
        AudioComponentInstanceDispose(slot.au);
    }
    slot.au = nullptr;
    slot.name[0] = 0;
    slot.source = nullptr;
}

void processInsert(AUInsert& slot, float* buf, int frames) {
    if (slot.vst) {
        processVst(*slot.vst, buf, frames);
        return;
    }
    if (!slot.au || frames <= 0 || frames > 4096) return;
    std::memcpy(slot.inScratch, buf, sizeof(float) * frames);
    slot.source = slot.inScratch;
    AudioBufferList abl{};
    abl.mNumberBuffers = 1;
    abl.mBuffers[0].mNumberChannels = 1;
    abl.mBuffers[0].mDataByteSize = (UInt32)(sizeof(float) * frames);
    abl.mBuffers[0].mData = buf;
    AudioUnitRenderActionFlags flags = 0;
    slot.ts.mSampleTime += frames;
    AudioUnitRender(slot.au, &flags, &slot.ts, 0, (UInt32)frames, &abl);
    slot.source = nullptr;
}

int paramCount(const AUInsert& slot) {
    if (slot.vst) return slot.vst->fx ? slot.vst->fx->numParams : 0;
    if (!slot.au) return 0;
    UInt32 size = 0;
    if (AudioUnitGetPropertyInfo(slot.au, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, &size, nullptr) != noErr) return 0;
    return (int)(size / sizeof(AudioUnitParameterID));
}

bool paramInfo(const AUInsert& slot, int index, ParamInfo& out) {
    if (slot.vst) return vstParamInfo(*slot.vst, index, out);
    int n = paramCount(slot);
    if (index < 0 || index >= n) return false;
    std::vector<AudioUnitParameterID> ids((size_t)n);
    UInt32 size = (UInt32)(n * sizeof(AudioUnitParameterID));
    if (AudioUnitGetProperty(slot.au, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, ids.data(), &size) != noErr) return false;

    AudioUnitParameterInfo info{};
    UInt32 infoSize = sizeof info;
    if (AudioUnitGetProperty(slot.au, kAudioUnitProperty_ParameterInfo, kAudioUnitScope_Global, ids[index], &info, &infoSize) != noErr) return false;

    out = ParamInfo{};
    out.id = ids[index];
    if ((info.flags & kAudioUnitParameterFlag_HasCFNameString) && info.cfNameString) {
        CFStringGetCString(info.cfNameString, out.name, sizeof out.name, kCFStringEncodingUTF8);
        if (info.flags & kAudioUnitParameterFlag_CFNameRelease) CFRelease(info.cfNameString);
    } else {
        std::snprintf(out.name, sizeof out.name, "%s", info.name);
    }
    out.minValue = info.minValue;
    out.maxValue = info.maxValue;
    out.defaultValue = info.defaultValue;
    AudioUnitGetParameter(slot.au, out.id, kAudioUnitScope_Global, 0, &out.value);
    return true;
}

bool setParamValue(AUInsert& slot, unsigned int id, float value) {
    if (slot.vst) {
        if (!slot.vst->fx || id >= (unsigned int)slot.vst->fx->numParams) return false;
        slot.vst->fx->setParameter(slot.vst->fx, (int32_t)id, value);
        return true;
    }
    if (!slot.au) return false;
    return AudioUnitSetParameter(slot.au, id, kAudioUnitScope_Global, 0, value, 0) == noErr;
}

bool paramText(const AUInsert& slot, int index, char* buf, int len) {
    if (!buf || len <= 0) return false;
    buf[0] = 0;
    if (!slot.vst || !slot.vst->fx || index < 0 || index >= slot.vst->fx->numParams) return false;
    VstEffect* fx = slot.vst->fx;
    char display[kVstTextLen] = {0};
    char label[kVstTextLen] = {0};
    fx->dispatcher(fx, kEffGetParamDisplay, index, 0, display, 0);
    fx->dispatcher(fx, kEffGetParamLabel, index, 0, label, 0);
    if (!display[0]) return false;
    if (label[0]) {
        std::snprintf(buf, (size_t)len, "%s %s", display, label);
    } else {
        std::snprintf(buf, (size_t)len, "%s", display);
    }
    return true;
}

int getClassInfo(const AUInsert& slot, unsigned char* buf, int len) {
    if (slot.vst) return vstStateGet(*slot.vst, buf, len);
    if (!slot.au) return -1;
    CFPropertyListRef plist = nullptr;
    UInt32 size = sizeof plist;
    if (AudioUnitGetProperty(slot.au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &plist, &size) != noErr || !plist) return -1;
    CFDataRef data = CFPropertyListCreateData(kCFAllocatorDefault, plist, kCFPropertyListBinaryFormat_v1_0, 0, nullptr);
    CFRelease(plist);
    if (!data) return -1;
    int n = (int)CFDataGetLength(data);
    if (buf && len >= n) std::memcpy(buf, CFDataGetBytePtr(data), (size_t)n);
    CFRelease(data);
    return n;
}

bool setClassInfo(AUInsert& slot, const unsigned char* data, int len) {
    if (slot.vst) return vstStateSet(*slot.vst, data, len);
    if (!slot.au || !data || len <= 0) return false;
    CFDataRef cfData = CFDataCreate(kCFAllocatorDefault, data, len);
    if (!cfData) return false;
    CFPropertyListRef plist = CFPropertyListCreateWithData(kCFAllocatorDefault, cfData, kCFPropertyListImmutable, nullptr, nullptr);
    CFRelease(cfData);
    if (!plist) return false;
    OSStatus st = AudioUnitSetProperty(slot.au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &plist, sizeof plist);
    CFRelease(plist);
    return st == noErr;
}

}  // namespace audiohub
