#include "../AudioHubDriver.c"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("GAGAL %d: %s\n", __LINE__, #c); fails++; } } while (0)

static UInt32 getBuffer(AudioObjectID dev) {
    AudioObjectPropertyAddress a = {kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    UInt32 v = 0, sz = 0;
    GetPropertyData(gDriverRef, dev, 0, &a, 0, NULL, sizeof v, &sz, &v);
    return v;
}
static OSStatus setBuffer(AudioObjectID dev, UInt32 frames) {
    AudioObjectPropertyAddress a = {kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    return SetPropertyData(gDriverRef, dev, 0, &a, 0, NULL, sizeof frames, &frames);
}

// Simulasi siklus IO: tiap siklus menulis blok output lalu membaca input dua blok di belakang.
// Pembacaan harus selalu mendapat data yang ditulis, tanpa nol, untuk ukuran buffer apa pun.
static void ringCheck(UInt32 frames, int cycles) {
    AudioObjectID dev = kAH_DeviceID(0);
    AudioObjectID outS = kAH_OutputStreamID(0), inS = kAH_InputStreamID(0);
    setBuffer(dev, frames);
    StartIO(gDriverRef, dev, 1);
    float* out = malloc(sizeof(float) * frames * kAH_Channels);
    float* in = malloc(sizeof(float) * frames * kAH_Channels);
    int zeros = 0, wrong = 0;
    for (int n = 0; n < cycles; n++) {
        for (UInt32 f = 0; f < frames * kAH_Channels; f++) out[f] = (float)(n + 1);
        AudioServerPlugInIOCycleInfo wc = {0};
        wc.mOutputTime.mSampleTime = (Float64)n * frames;
        DoIOOperation(gDriverRef, dev, outS, 1, kAudioServerPlugInIOOperationWriteMix, frames, &wc, out, NULL);
        AudioServerPlugInIOCycleInfo rc = {0};
        rc.mInputTime.mSampleTime = (Float64)(n + 2) * frames;
        DoIOOperation(gDriverRef, dev, inS, 1, kAudioServerPlugInIOOperationReadInput, frames, &rc, in, NULL);
        // Blok yang dibaca di siklus n adalah blok yang ditulis di siklus n (posisi n*frames)
        for (UInt32 f = 0; f < frames * kAH_Channels; f++) {
            if (in[f] == 0.0f) zeros++;
            else if (in[f] != (float)(n + 1)) wrong++;
        }
    }
    printf("ring %3u frame: %d siklus, nol=%d, salah=%d\n", frames, cycles, zeros, wrong);
    CHECK(zeros == 0 && wrong == 0);
    StopIO(gDriverRef, dev, 1);
    free(out); free(in);
}

int main(void) {
    Initialize(gDriverRef, NULL);
    AudioObjectID dev = kAH_DeviceID(0);

    // 1. Default 512, rentang 16..1024, bisa diatur
    CHECK(getBuffer(dev) == 512);
    AudioObjectPropertyAddress rg = {kAudioDevicePropertyBufferFrameSizeRange, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    AudioValueRange r = {0, 0}; UInt32 rs = 0;
    GetPropertyData(gDriverRef, dev, 0, &rg, 0, NULL, sizeof r, &rs, &r);
    CHECK(r.mMinimum == 16 && r.mMaximum == 1024);
    AudioObjectPropertyAddress bs = {kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    Boolean settable = false;
    IsPropertySettable(gDriverRef, dev, 0, &bs, &settable);
    CHECK(settable == true);
    AudioObjectPropertyAddress nm = {kAudioObjectPropertyName, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    IsPropertySettable(gDriverRef, dev, 0, &nm, &settable);
    CHECK(settable == false);

    // 2. Ukuran 192 diterima; ukuran di luar rentang ditolak
    CHECK(setBuffer(dev, 192) == kAudioHardwareNoError);
    CHECK(getBuffer(dev) == 192);
    CHECK(setBuffer(dev, 8) != kAudioHardwareNoError);
    CHECK(setBuffer(dev, 4096) != kAudioHardwareNoError);
    CHECK(getBuffer(dev) == 192);

    // 3. Timeline mengikuti 192: sampel nol selalu kelipatan 192, dan periode waktu sesuai
    Float64 s1 = 0, s2 = 0; UInt64 h1 = 0, h2 = 0, seed = 0;
    GetZeroTimeStamp(gDriverRef, dev, 0, &s1, &h1, &seed);
    usleep(30000);   // sekitar 30 ms, lebih dari beberapa periode 192 (4 ms)
    GetZeroTimeStamp(gDriverRef, dev, 0, &s2, &h2, &seed);
    CHECK(fmod(s1, 192.0) == 0.0 && fmod(s2, 192.0) == 0.0);
    CHECK(s2 > s1);
    printf("sampel: %.0f -> %.0f (selisih %.0f = %.1f periode)\n", s1, s2, s2 - s1, (s2 - s1) / 192.0);

    // 4. Perangkat kedua tidak ikut berubah
    CHECK(getBuffer(kAH_DeviceID(1)) == 512);

    // 5. Ring untuk blok 192 dan 512 tidak boleh menghasilkan nol atau data salah
    ringCheck(192, 2000);
    ringCheck(512, 500);
    setBuffer(dev, 512);

    printf("%s (%d gagal)\n", fails ? "ada yang gagal" : "semua uji buffer lulus", fails);
    return fails ? 1 : 0;
}
