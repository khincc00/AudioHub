#include "au_insert.h"

#include <cstdio>
#include <cstring>

namespace audiohub {

namespace {

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

}  // namespace

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
    return written;
}

bool loadInsert(AUInsert& slot, const char* name, float sampleRate, int maxFrames) {
    if (slot.au) unloadInsert(slot);
    if (maxFrames > 4096) maxFrames = 4096;
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
    if (slot.au) {
        AudioUnitUninitialize(slot.au);
        AudioComponentInstanceDispose(slot.au);
    }
    slot.au = nullptr;
    slot.name[0] = 0;
    slot.source = nullptr;
}

void processInsert(AUInsert& slot, float* buf, int frames) {
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

}  // namespace audiohub
