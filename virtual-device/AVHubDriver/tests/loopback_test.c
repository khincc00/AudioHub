// Uji logika driver tanpa memasangnya ke sistem.
// Memanggil driver lewat vtable yang sama dengan yang dipakai coreaudiod, lalu
// mensimulasikan siklus IO: output ditulis, input dibaca, dan hasilnya dicek.
//
// Build & jalankan: sh tests/run_tests.sh

#include "../src/AVHubDriver.c"

#include <math.h>
#include <stdio.h>
#include <unistd.h>

static int gFailures = 0;
static int gRequests = 0;        // jumlah RequestDeviceConfigurationChange dari driver
static int gPropertyNotes = 0;   // jumlah PropertiesChanged dari driver

#define CHECK(cond, ...)                         \
    do {                                         \
        if (cond) {                              \
            printf("  PASS  ");                  \
        } else {                                 \
            printf("  FAIL  ");                  \
            gFailures++;                         \
        }                                        \
        printf(__VA_ARGS__);                     \
        printf("\n");                            \
    } while (0)

// Host tiruan. Host sungguhan memanggil Perform setelah IO berhenti; di sini langsung dipanggil.
static OSStatus StubRequestChange(AudioServerPlugInHostRef h, AudioObjectID dev, UInt64 action, void *info)
{
    gRequests++;
    return DrvPerformDeviceConfigurationChange(gDriverRef, dev, action, info);
}

static OSStatus StubPropertiesChanged(AudioServerPlugInHostRef h, AudioObjectID obj,
                                      UInt32 n, const AudioObjectPropertyAddress *addrs)
{
    gPropertyNotes++;
    return kAudioHardwareNoError;
}

static AudioServerPlugInHostInterface gHostStub = {
    .PropertiesChanged = StubPropertiesChanged,
    .RequestDeviceConfigurationChange = StubRequestChange,
};

static AudioObjectPropertyAddress Addr(AudioObjectPropertySelector sel, AudioObjectPropertyScope scope)
{
    AudioObjectPropertyAddress a = { sel, scope, kAudioObjectPropertyElementMain };
    return a;
}

static OSStatus Get(AudioServerPlugInDriverRef drv, AudioObjectID obj, AudioObjectPropertyAddress a,
                    UInt32 qualSize, const void *qual, void *out, UInt32 cap, UInt32 *outSize)
{
    return (*drv)->GetPropertyData(drv, obj, 0, &a, qualSize, qual, cap, outSize, out);
}

static OSStatus Set(AudioServerPlugInDriverRef drv, AudioObjectID obj, AudioObjectPropertyAddress a,
                    UInt32 size, const void *data)
{
    return (*drv)->SetPropertyData(drv, obj, 0, &a, 0, NULL, size, data);
}

static UInt32 BufferFrames(AudioServerPlugInDriverRef drv, AudioObjectID dev)
{
    UInt32 frames = 0;
    UInt32 size = 0;
    Get(drv, dev, Addr(kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeGlobal), 0, NULL, &frames, sizeof frames, &size);
    return frames;
}

static void CFStringToBuf(CFStringRef s, char *buf, size_t n)
{
    buf[0] = '\0';
    if (s) CFStringGetCString(s, buf, (CFIndex)n, kCFStringEncodingUTF8);
}

static Float64 DeviceRate(AudioServerPlugInDriverRef drv, AudioObjectID dev)
{
    Float64 rate = 0;
    UInt32 size = 0;
    Get(drv, dev, Addr(kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal), 0, NULL, &rate, sizeof rate, &size);
    return rate;
}

// Simulasi kCycles siklus IO pada satu perangkat.
//
// HAL memberi input dengan label sample time yang tertinggal dua buffer dari output pada
// siklus yang sama. Karena itu input siklus c membawa data output siklus c-2, dan dua siklus
// pertama input harus nol. Driver tidak menambah offset lagi.
//
// gain dan muted mengikuti kontrol input. Nilai yang diharapkan dihitung dengan operasi float yang
// sama dengan driver, sehingga perbandingannya bit demi bit.
//
// Mengembalikan jumlah siklus yang tidak cocok. *silentOK = 1 jika dua siklus pertama input nol.
#define kCycles 300
#define kHalLagCycles 2
static Float32 gOutBuf[kBufferFrames * kChannels];
static Float32 gInBuf[kBufferFrames * kChannels];
static Float32 gWant[kBufferFrames * kChannels];
static Float32 gExpect[kCycles][kBufferFrames * kChannels];

static int RunLoopback(AudioServerPlugInDriverRef drv, AudioObjectID dev, AudioObjectID outS,
                       AudioObjectID inS, UInt32 frames, Float32 gain, int muted, int *silentOK)
{
    int mismatches = 0;
    *silentOK = 1;

    for (int c = 0; c < kCycles; c++) {
        AudioServerPlugInIOCycleInfo info;
        memset(&info, 0, sizeof info);
        info.mOutputTime.mSampleTime = (Float64)c * frames;
        info.mInputTime.mSampleTime = (Float64)(c - kHalLagCycles) * frames;

        for (UInt32 k = 0; k < frames; k++) {
            // Nilai unik per frame dan per kanal, agar pergeseran waktu ketahuan.
            gOutBuf[k * 2 + 0] = (Float32)(c * frames + k) + 0.25f;
            gOutBuf[k * 2 + 1] = -(Float32)(c * frames + k) - 0.5f;
        }
        memcpy(gExpect[c], gOutBuf, frames * kChannels * sizeof(Float32));

        (*drv)->DoIOOperation(drv, dev, outS, 1, kAudioServerPlugInIOOperationWriteMix,
                              frames, &info, gOutBuf, NULL);
        (*drv)->DoIOOperation(drv, dev, inS, 1, kAudioServerPlugInIOOperationReadInput,
                              frames, &info, gInBuf, NULL);

        if (c < kHalLagCycles) {
            for (UInt32 n = 0; n < frames * kChannels; n++) {
                if (gInBuf[n] != 0.0f) *silentOK = 0;
            }
            continue;
        }
        const Float32 *src = gExpect[c - kHalLagCycles];
        for (UInt32 n = 0; n < frames * kChannels; n++) {
            gWant[n] = muted ? 0.0f : src[n] * gain;
        }
        if (memcmp(gInBuf, gWant, frames * kChannels * sizeof(Float32)) != 0) mismatches++;
    }
    return mismatches;
}

static int IDsEqual(const AudioObjectID *a, const AudioObjectID *b, UInt32 n)
{
    return memcmp(a, b, n * sizeof(AudioObjectID)) == 0;
}

int main(void)
{
    printf("Factory dan interface\n");
    AudioServerPlugInDriverRef drv = (AudioServerPlugInDriverRef)AVHubDriver_Create(NULL, kAudioServerPlugInTypeUUID);
    CHECK(drv != NULL, "AVHubDriver_Create mengembalikan driver");

    LPVOID iface = NULL;
    CFUUIDBytes drvUUID = CFUUIDGetUUIDBytes(kAudioServerPlugInDriverInterfaceUUID);
    HRESULT hr = (*drv)->QueryInterface(drv, drvUUID, &iface);
    CHECK(hr == S_OK && iface == drv, "QueryInterface untuk AudioServerPlugInDriverInterface");

    CFUUIDBytes bogus = CFUUIDGetUUIDBytes(CFUUIDGetConstantUUIDWithBytes(NULL, 1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16));
    hr = (*drv)->QueryInterface(drv, bogus, &iface);
    CHECK(hr == E_NOINTERFACE && iface == NULL, "UUID tak dikenal ditolak");

    (*drv)->Initialize(drv, &gHostStub);

    printf("Daftar perangkat\n");
    UInt32 size = 0;
    AudioObjectPropertyAddress devListAddr = Addr(kAudioPlugInPropertyDeviceList, kAudioObjectPropertyScopeGlobal);
    (*drv)->GetPropertyDataSize(drv, kAudioObjectPlugInObject, 0, &devListAddr, 0, NULL, &size);
    CHECK(size == 3 * sizeof(AudioObjectID), "DeviceList berisi 3 perangkat (size=%u)", size);

    AudioObjectID devs[3] = { 0, 0, 0 };
    Get(drv, kAudioObjectPlugInObject, devListAddr, 0, NULL, devs, sizeof devs, &size);
    CHECK(devs[0] == 10 && devs[1] == 20 && devs[2] == 30, "ID perangkat: %u, %u, %u", devs[0], devs[1], devs[2]);

    const char *wantNames[3] = { "avhub_virtual1", "avhub_virtual2", "avhub_loopback" };
    for (int i = 0; i < 3; i++) {
        CFStringRef name = NULL;
        Get(drv, devs[i], Addr(kAudioObjectPropertyName, kAudioObjectPropertyScopeGlobal), 0, NULL, &name, sizeof name, &size);
        char buf[64];
        CFStringToBuf(name, buf, sizeof buf);
        CHECK(strcmp(buf, wantNames[i]) == 0, "Nama perangkat %d = \"%s\"", i + 1, buf);
        if (name) CFRelease(name);

        CFStringRef uid = NULL;
        Get(drv, devs[i], Addr(kAudioDevicePropertyDeviceUID, kAudioObjectPropertyScopeGlobal), 0, NULL, &uid, sizeof uid, &size);
        CFStringToBuf(uid, buf, sizeof buf);
        CHECK(strcmp(buf, wantNames[i]) == 0, "UID perangkat %d = \"%s\"", i + 1, buf);
        if (uid) CFRelease(uid);

        CFStringRef man = NULL;
        Get(drv, devs[i], Addr(kAudioObjectPropertyManufacturer, kAudioObjectPropertyScopeGlobal), 0, NULL, &man, sizeof man, &size);
        CFStringToBuf(man, buf, sizeof buf);
        CHECK(strcmp(buf, "AVHub") == 0, "Pabrikan perangkat %d = \"%s\"", i + 1, buf);
        if (man) CFRelease(man);
    }

    printf("Translate UID -> ID\n");
    CFStringRef q = CFSTR("avhub_virtual2");
    AudioObjectID translated = 0;
    AudioObjectPropertyAddress tAddr = Addr(kAudioPlugInPropertyTranslateUIDToDevice, kAudioObjectPropertyScopeGlobal);
    Get(drv, kAudioObjectPlugInObject, tAddr, sizeof q, &q, &translated, sizeof translated, &size);
    CHECK(translated == 20, "avhub_virtual2 -> %u (harus 20)", translated);
    q = CFSTR("avhub_loopback");
    Get(drv, kAudioObjectPlugInObject, tAddr, sizeof q, &q, &translated, sizeof translated, &size);
    CHECK(translated == 30, "avhub_loopback -> %u (harus 30)", translated);

    printf("Tiga perangkat stereo\n");
    for (int i = 0; i < 3; i++) {
        AudioObjectID dev = devs[i];
        AudioBufferList abl;
        Get(drv, dev, Addr(kAudioDevicePropertyStreamConfiguration, kAudioObjectPropertyScopeOutput), 0, NULL, &abl, sizeof abl, &size);
        CHECK(abl.mNumberBuffers == 1 && abl.mBuffers[0].mNumberChannels == 2,
              "Perangkat %d: konfigurasi stream 2 kanal", i + 1);

        AudioObjectID streams[2] = { 0, 0 };
        Get(drv, dev, Addr(kAudioDevicePropertyStreams, kAudioObjectPropertyScopeGlobal), 0, NULL, streams, sizeof streams, &size);
        CHECK(size == 2 * sizeof(AudioObjectID), "Perangkat %d: 2 stream (output + input)", i + 1);

        AudioStreamBasicDescription fmt;
        Get(drv, streams[0], Addr(kAudioStreamPropertyVirtualFormat, kAudioObjectPropertyScopeGlobal), 0, NULL, &fmt, sizeof fmt, &size);
        CHECK(fmt.mChannelsPerFrame == 2 && fmt.mBytesPerFrame == 8 && (fmt.mFormatFlags & kAudioFormatFlagIsFloat),
              "Perangkat %d: format stream 2 kanal float32 interleaved", i + 1);
    }

    printf("Stream dan format\n");
    AudioObjectID streams[2] = { 0, 0 };
    AudioObjectPropertyAddress streamsAddr = Addr(kAudioDevicePropertyStreams, kAudioObjectPropertyScopeOutput);
    Get(drv, 10, streamsAddr, 0, NULL, streams, sizeof streams, &size);
    CHECK(size == sizeof(AudioObjectID) && streams[0] == 11, "Scope output -> stream 11");

    streamsAddr.mScope = kAudioObjectPropertyScopeInput;
    Get(drv, 10, streamsAddr, 0, NULL, streams, sizeof streams, &size);
    CHECK(size == sizeof(AudioObjectID) && streams[0] == 12, "Scope input -> stream 12");

    AudioStreamBasicDescription fmt;
    Get(drv, 11, Addr(kAudioStreamPropertyVirtualFormat, kAudioObjectPropertyScopeGlobal), 0, NULL, &fmt, sizeof fmt, &size);
    CHECK(fmt.mSampleRate == 48000 && fmt.mChannelsPerFrame == 2 &&
          (fmt.mFormatFlags & kAudioFormatFlagIsFloat) && fmt.mBytesPerFrame == 8,
          "Format output default: 48 kHz, 2 kanal, float32 interleaved");

    UInt32 dir = 99;
    Get(drv, 11, Addr(kAudioStreamPropertyDirection, kAudioObjectPropertyScopeGlobal), 0, NULL, &dir, sizeof dir, &size);
    CHECK(dir == 0, "Stream 11 arahnya output (0)");
    Get(drv, 12, Addr(kAudioStreamPropertyDirection, kAudioObjectPropertyScopeGlobal), 0, NULL, &dir, sizeof dir, &size);
    CHECK(dir == 1, "Stream 12 arahnya input (1)");

    printf("Kontrol volume dan mute\n");
    AudioObjectID ctrls[7] = { 0, 0, 0, 0, 0, 0, 0 };
    Get(drv, 10, Addr(kAudioObjectPropertyControlList, kAudioObjectPropertyScopeGlobal), 0, NULL, ctrls, sizeof ctrls, &size);
    AudioObjectID wantCtrls[7] = { 13, 14, 15, 16, 17, 18, 19 };
    CHECK(size == 7 * sizeof(AudioObjectID) && IDsEqual(ctrls, wantCtrls, 7),
          "ControlList perangkat 1: volume (13), mute (14), pitch (15), clock (16), volume output (17), mute output (18), volume kedua (19)");

    AudioObjectID levelOnly[7] = { 0 };
    AudioClassID levelClass = kAudioLevelControlClassID;
    Get(drv, 10, Addr(kAudioObjectPropertyOwnedObjects, kAudioObjectPropertyScopeGlobal),
        sizeof levelClass, &levelClass, levelOnly, sizeof levelOnly, &size);
    CHECK(size == 4 * sizeof(AudioObjectID) && levelOnly[0] == 13 && levelOnly[1] == 15 && levelOnly[2] == 17 && levelOnly[3] == 19,
          "OwnedObjects dengan kelas level memuat volume input (13), pitch (15), volume output (17), volume kedua (19)");

    AudioObjectID boolOnly[6] = { 0 };
    AudioClassID boolClass = kAudioBooleanControlClassID;
    Get(drv, 10, Addr(kAudioObjectPropertyOwnedObjects, kAudioObjectPropertyScopeGlobal),
        sizeof boolClass, &boolClass, boolOnly, sizeof boolOnly, &size);
    CHECK(size == 2 * sizeof(AudioObjectID) && boolOnly[0] == 14 && boolOnly[1] == 18,
          "OwnedObjects dengan kelas boolean memuat mute input (14) dan mute output (18)");

    AudioClassID volClass = 0, volBase = 0;
    Get(drv, 13, Addr(kAudioObjectPropertyClass, kAudioObjectPropertyScopeGlobal), 0, NULL, &volClass, sizeof volClass, &size);
    Get(drv, 13, Addr(kAudioObjectPropertyBaseClass, kAudioObjectPropertyScopeGlobal), 0, NULL, &volBase, sizeof volBase, &size);
    CHECK(volClass == kAudioVolumeControlClassID && volBase == kAudioLevelControlClassID,
          "Volume: kelas 'vlme', kelas dasar 'levl'");

    AudioObjectID owner = 0;
    Get(drv, 13, Addr(kAudioObjectPropertyOwner, kAudioObjectPropertyScopeGlobal), 0, NULL, &owner, sizeof owner, &size);
    CHECK(owner == 10, "Pemilik kontrol volume adalah perangkat 1 (%u)", owner);

    UInt32 ctrlScope = 0;
    Get(drv, 13, Addr(kAudioControlPropertyScope, kAudioObjectPropertyScopeGlobal), 0, NULL, &ctrlScope, sizeof ctrlScope, &size);
    CHECK(ctrlScope == kAudioObjectPropertyScopeInput, "Kontrol berada di sisi input");

    Float32 gainNow = 0;
    Get(drv, 13, Addr(kAudioLevelControlPropertyScalarValue, kAudioObjectPropertyScopeGlobal), 0, NULL, &gainNow, sizeof gainNow, &size);
    CHECK(gainNow == 1.0f, "Volume default 1.0 (%f)", gainNow);

    Float32 dbNow = 99;
    Get(drv, 13, Addr(kAudioLevelControlPropertyDecibelValue, kAudioObjectPropertyScopeGlobal), 0, NULL, &dbNow, sizeof dbNow, &size);
    CHECK(fabsf(dbNow) < 1e-4f, "Volume default 0 dB (%f)", dbNow);

    AudioValueRange dbRange = { 0, 0 };
    Get(drv, 13, Addr(kAudioLevelControlPropertyDecibelRange, kAudioObjectPropertyScopeGlobal), 0, NULL, &dbRange, sizeof dbRange, &size);
    CHECK(dbRange.mMinimum == -64.0 && dbRange.mMaximum == 0.0, "Rentang dB volume -64..0");

    UInt32 muted = 99;
    Get(drv, 14, Addr(kAudioBooleanControlPropertyValue, kAudioObjectPropertyScopeGlobal), 0, NULL, &muted, sizeof muted, &size);
    CHECK(muted == 0, "Mute default mati");

    AudioObjectPropertyAddress volAddr = Addr(kAudioLevelControlPropertyScalarValue, kAudioObjectPropertyScopeGlobal);
    Boolean settable = false;
    (*drv)->IsPropertySettable(drv, 13, 0, &volAddr, &settable);
    CHECK(settable, "Volume scalar bisa diubah");
    (*drv)->IsPropertySettable(drv, 14, 0, &(AudioObjectPropertyAddress){ kAudioBooleanControlPropertyValue, kAudioObjectPropertyScopeGlobal, 0 }, &settable);
    CHECK(settable, "Mute bisa diubah");

    printf("Objek Box\n");
    AudioObjectID boxIDs[4] = { 0 };
    AudioObjectPropertyAddress boxListAddr = Addr(kAudioPlugInPropertyBoxList, kAudioObjectPropertyScopeGlobal);
    Get(drv, kAudioObjectPlugInObject, boxListAddr, 0, NULL, boxIDs, sizeof boxIDs, &size);
    CHECK(size == sizeof(AudioObjectID) && boxIDs[0] == kBoxObjectID, "Plug-in memuat satu Box (%u)", boxIDs[0]);

    AudioObjectID plugOwned[4] = { 0 };
    AudioClassID boxClass = kAudioBoxClassID;
    Get(drv, kAudioObjectPlugInObject, Addr(kAudioObjectPropertyOwnedObjects, kAudioObjectPropertyScopeGlobal),
        sizeof boxClass, &boxClass, plugOwned, sizeof plugOwned, &size);
    CHECK(size == sizeof(AudioObjectID) && plugOwned[0] == kBoxObjectID, "OwnedObjects plug-in dengan kelas Box memuat Box");

    CFStringRef boxUIDq = CFSTR(kBoxUID);
    AudioObjectID boxFromUID = 0;
    Get(drv, kAudioObjectPlugInObject, Addr(kAudioPlugInPropertyTranslateUIDToBox, kAudioObjectPropertyScopeGlobal),
        sizeof boxUIDq, &boxUIDq, &boxFromUID, sizeof boxFromUID, &size);
    CHECK(boxFromUID == kBoxObjectID, "Translate UID Box -> %u", boxFromUID);

    AudioObjectID boxDevs[4] = { 0 };
    Get(drv, kBoxObjectID, Addr(kAudioBoxPropertyDeviceList, kAudioObjectPropertyScopeGlobal), 0, NULL, boxDevs, sizeof boxDevs, &size);
    CHECK(size == 3 * sizeof(AudioObjectID) && boxDevs[0] == 10 && boxDevs[1] == 20 && boxDevs[2] == 30,
          "Daftar perangkat Box = 10, 20, dan 30");

    UInt32 boxAcquired = 0, boxAudio = 0, boxVideo = 1;
    Get(drv, kBoxObjectID, Addr(kAudioBoxPropertyAcquired, kAudioObjectPropertyScopeGlobal), 0, NULL, &boxAcquired, sizeof boxAcquired, &size);
    Get(drv, kBoxObjectID, Addr(kAudioBoxPropertyHasAudio, kAudioObjectPropertyScopeGlobal), 0, NULL, &boxAudio, sizeof boxAudio, &size);
    Get(drv, kBoxObjectID, Addr(kAudioBoxPropertyHasVideo, kAudioObjectPropertyScopeGlobal), 0, NULL, &boxVideo, sizeof boxVideo, &size);
    CHECK(boxAcquired == 1 && boxAudio == 1 && boxVideo == 0, "Box: aktif, ada audio, tanpa video");

    AudioObjectPropertyAddress idAddr = Addr(kAudioObjectPropertyIdentify, kAudioObjectPropertyScopeGlobal);
    (*drv)->IsPropertySettable(drv, kBoxObjectID, 0, &idAddr, &settable);
    CHECK(settable, "Box identify bisa diset");
    UInt32 identifyOn = 1;
    OSStatus stId = Set(drv, kBoxObjectID, idAddr, sizeof identifyOn, &identifyOn);
    CHECK(stId == kAudioHardwareNoError, "Identify diterima (status=%d)", (int)stId);

    printf("Sample rate\n");
    AudioValueRange rates[kSupportedRateCount];
    AudioValueRange wantRateRange[2] = { { 8000, 8000 }, { 768000, 768000 } };
    Get(drv, 10, Addr(kAudioDevicePropertyAvailableNominalSampleRates, kAudioObjectPropertyScopeGlobal), 0, NULL, rates, sizeof rates, &size);
    CHECK(size == 13 * sizeof rates[0] && rates[0].mMinimum == wantRateRange[0].mMinimum && rates[12].mMinimum == wantRateRange[1].mMinimum,
          "Perangkat 1: 13 rate 8000..768000 Hz");
    Get(drv, 20, Addr(kAudioDevicePropertyAvailableNominalSampleRates, kAudioObjectPropertyScopeGlobal), 0, NULL, rates, sizeof rates, &size);
    CHECK(size == 13 * sizeof rates[0] && rates[0].mMinimum == 8000 && rates[12].mMinimum == 768000,
          "Perangkat 2: 13 rate 8000..768000 Hz (sama dengan perangkat 1)");

    AudioObjectPropertyAddress rateAddr = Addr(kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal);
    (*drv)->IsPropertySettable(drv, 10, 0, &rateAddr, &settable);
    CHECK(settable, "Sample rate perangkat bisa diubah");
    AudioObjectPropertyAddress bufAddr = Addr(kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeGlobal);
    (*drv)->IsPropertySettable(drv, 10, 0, &bufAddr, &settable);
    CHECK(settable, "Ukuran buffer perangkat bisa diubah");

    printf("Loopback 48 kHz (default)\n");
    (*drv)->StartIO(drv, 10, 1);
    (*drv)->StartIO(drv, 20, 1);

    // Zero timestamp harus maju seiring waktu nyata.
    Float64 s0 = 0, s1 = 0; UInt64 h0 = 0, h1 = 0, seed = 0;
    (*drv)->GetZeroTimeStamp(drv, 10, 1, &s0, &h0, &seed);
    usleep(800000);   // periode zero timestamp 16384 frame (~0,34 s di 48 kHz)
    (*drv)->GetZeroTimeStamp(drv, 10, 1, &s1, &h1, &seed);
    CHECK(s1 > s0 && h1 > h0, "Zero timestamp maju (%.0f -> %.0f)", s0, s1);
    CHECK(fmod(s1, 16384) == 0, "Sample time kelipatan periode zero timestamp 16384");

    int silent = 0;
    int mismatches = RunLoopback(drv, 10, 11, 12, kBufferFrames, 1.0f, 0, &silent);
    CHECK(silent, "Input nol selama %d siklus pertama (belum ada data)", kHalLagCycles);
    CHECK(mismatches == 0, "48 kHz: input = output dengan jeda %d siklus, %d tidak cocok (dari %d)",
          kHalLagCycles, mismatches, kCycles - kHalLagCycles);

    printf("Isolasi antar perangkat\n");
    // Perangkat 20 tidak pernah ditulis, jadi inputnya harus nol.
    AudioServerPlugInIOCycleInfo info2;
    memset(&info2, 0, sizeof info2);
    info2.mInputTime.mSampleTime = (Float64)(kCycles * kBufferFrames);
    static Float32 otherBuf[kBufferFrames * kChannels];
    memset(otherBuf, 0x7f, sizeof otherBuf);
    (*drv)->DoIOOperation(drv, 20, 22, 2, kAudioServerPlugInIOOperationReadInput,
                          kBufferFrames, &info2, otherBuf, NULL);
    int nonZero = 0;
    for (UInt32 n = 0; n < kBufferFrames * kChannels; n++) if (otherBuf[n] != 0.0f) nonZero++;
    CHECK(nonZero == 0, "Perangkat 2 bebas dari data perangkat 1");

    printf("Kontrol output (nilai master sama dengan input)\n");
    AudioObjectID outVolCtl = 17, outMuteCtl = 18;
    Float32 linkV = 0.25f;
    Set(drv, 13, volAddr, sizeof linkV, &linkV);
    Float32 altV = -1;
    Get(drv, 19, volAddr, 0, NULL, &altV, sizeof altV, &size);
    CHECK(altV == 0.25f, "Volume input kedua mengikuti volume utama (%.3f)", altV);
    Float32 outReadV = -1;
    Get(drv, outVolCtl, volAddr, 0, NULL, &outReadV, sizeof outReadV, &size);
    CHECK(outReadV == 0.25f, "Volume output mengikuti input (0.25 -> %.3f)", outReadV);
    Float32 oneV = 1.0f;
    Set(drv, outVolCtl, volAddr, sizeof oneV, &oneV);
    Get(drv, 13, volAddr, 0, NULL, &outReadV, sizeof outReadV, &size);
    CHECK(outReadV == 1.0f, "Volume input mengikuti output (1.0 -> %.3f)", outReadV);

    UInt32 linkOn = 1, linkNow = 9;
    Set(drv, outMuteCtl, Addr(kAudioBooleanControlPropertyValue, kAudioObjectPropertyScopeGlobal), sizeof linkOn, &linkOn);
    Get(drv, 14, Addr(kAudioBooleanControlPropertyValue, kAudioObjectPropertyScopeGlobal), 0, NULL, &linkNow, sizeof linkNow, &size);
    CHECK(linkNow == 1, "Mute output mengikuti mute input (%u)", linkNow);
    UInt32 linkOff = 0;
    Set(drv, outMuteCtl, Addr(kAudioBooleanControlPropertyValue, kAudioObjectPropertyScopeGlobal), sizeof linkOff, &linkOff);

    AudioObjectPropertyAddress scopeAddr = Addr(kAudioControlPropertyScope, kAudioObjectPropertyScopeGlobal);
    UInt32 outScope = 0;
    Get(drv, outVolCtl, scopeAddr, 0, NULL, &outScope, sizeof outScope, &size);
    CHECK(outScope == kAudioObjectPropertyScopeOutput, "Volume output berada di sisi output");

    printf("Tulis terlambat\n");
    AudioServerPlugInIOCycleInfo late;
    memset(&late, 0, sizeof late);
    late.mOutputTime.mSampleTime = 100.0;
    late.mCurrentTime.mSampleTime = 1000.0;
    static Float32 lateBuf[kBufferFrames * kChannels];
    memset(lateBuf, 0x3f, sizeof lateBuf);
    OSStatus stLate = (*drv)->DoIOOperation(drv, 10, 11, 1, kAudioServerPlugInIOOperationWriteMix,
                                            kBufferFrames, &late, lateBuf, NULL);
    CHECK(stLate == kAVHubErrLateWrite, "Tulis yang sudah lewat ditolak (status=%d)", (int)stLate);

    AudioServerPlugInIOCycleInfo onTime = late;
    onTime.mOutputTime.mSampleTime = 1000.0;
    stLate = (*drv)->DoIOOperation(drv, 10, 11, 1, kAudioServerPlugInIOOperationWriteMix,
                                   kBufferFrames, &onTime, lateBuf, NULL);
    CHECK(stLate == kAudioHardwareNoError, "Tulis tepat waktu diterima");

    printf("Volume dan mute pada loopback\n");
    AudioObjectPropertyAddress volDbAddr = Addr(kAudioLevelControlPropertyDecibelValue, kAudioObjectPropertyScopeGlobal);
    Float32 halfGain = 0.5f;
    Set(drv, 13, volAddr, sizeof halfGain, &halfGain);
    Get(drv, 13, volDbAddr, 0, NULL, &dbNow, sizeof dbNow, &size);
    CHECK(fabsf(dbNow - (-32.0f)) < 1e-3f, "Volume skalar 0.5 = %.3f dB (kurva dB linear)", dbNow);

    silent = 0;
    mismatches = RunLoopback(drv, 10, 11, 12, kBufferFrames, ScalarToGain(0.5f), 0, &silent);
    CHECK(mismatches == 0, "Volume 0.5: input = output x gain(%.5f), %d tidak cocok", ScalarToGain(0.5f), mismatches);

    Float32 dbMinus12 = -12.0f;
    Set(drv, 13, volDbAddr, sizeof dbMinus12, &dbMinus12);
    Get(drv, 13, volAddr, 0, NULL, &gainNow, sizeof gainNow, &size);
    CHECK(fabsf(gainNow - 0.8125f) < 1e-4f && fabsf(ScalarToGain(gainNow) - 0.2512f) < 1e-3f,
          "Volume -12 dB = skalar %.4f, gain %.4f", gainNow, ScalarToGain(gainNow));

    Float32 dbLow = -200.0f;
    Set(drv, 13, volDbAddr, sizeof dbLow, &dbLow);
    Get(drv, 13, volAddr, 0, NULL, &gainNow, sizeof gainNow, &size);
    CHECK(gainNow == 0.0f && ScalarToGain(gainNow) == 0.0f, "Volume di bawah -64 dB dijepit ke diam (%f)", gainNow);

    Float32 one = 1.0f;
    Set(drv, 13, volAddr, sizeof one, &one);
    UInt32 muteOn = 1;
    int notesBeforeMute = gPropertyNotes;
    Set(drv, 14, Addr(kAudioBooleanControlPropertyValue, kAudioObjectPropertyScopeGlobal), sizeof muteOn, &muteOn);
    CHECK(gPropertyNotes > notesBeforeMute, "Perubahan mute memberi tahu klien lewat PropertiesChanged");

    silent = 0;
    mismatches = RunLoopback(drv, 10, 11, 12, kBufferFrames, 1.0f, 1, &silent);
    CHECK(mismatches == 0, "Mute: input nol saat loopback, %d tidak cocok", mismatches);

    UInt32 muteOff = 0;
    Set(drv, 14, Addr(kAudioBooleanControlPropertyValue, kAudioObjectPropertyScopeGlobal), sizeof muteOff, &muteOff);
    silent = 0;
    mismatches = RunLoopback(drv, 10, 11, 12, kBufferFrames, 1.0f, 0, &silent);
    CHECK(mismatches == 0, "Mute dimatikan: input kembali = output, %d tidak cocok", mismatches);

    printf("Pitch dan clock source (control)\n");
    AudioObjectPropertyAddress pitchAddr = Addr(kAudioLevelControlPropertyScalarValue, kAudioObjectPropertyScopeGlobal);
    AudioObjectPropertyAddress csAddr = Addr(kAudioSelectorControlPropertyCurrentItem, kAudioObjectPropertyScopeGlobal);
    AudioClassID pitchCls = 0, csCls = 0;
    Get(drv, 15, Addr(kAudioObjectPropertyClass, kAudioObjectPropertyScopeGlobal), 0, NULL, &pitchCls, sizeof pitchCls, &size);
    Get(drv, 16, Addr(kAudioObjectPropertyClass, kAudioObjectPropertyScopeGlobal), 0, NULL, &csCls, sizeof csCls, &size);
    CHECK(pitchCls == kAudioLevelControlClassID && csCls == kAudioClockSourceControlClassID,
          "Pitch: level control (15), clock source: selector clck (16)");

    Float32 pitchNow = -1;
    Get(drv, 15, pitchAddr, 0, NULL, &pitchNow, sizeof pitchNow, &size);
    CHECK(pitchNow == kPitchNeutral, "Pitch default netral (%.2f)", pitchNow);
    UInt32 csDefault = 99;
    Get(drv, 16, csAddr, 0, NULL, &csDefault, sizeof csDefault, &size);
    CHECK(csDefault == 0, "Clock source default item 0 (nominal)");

    Float32 pitchIgnored = 0.0f;
    double nominalBefore = gDevices[0].activeTicksPerFrame;
    Set(drv, 15, pitchAddr, sizeof pitchIgnored, &pitchIgnored);
    CHECK(gDevices[0].activeTicksPerFrame == nominalBefore, "Clock source 0 mengabaikan pitch");
    Float32 neutralFirst = kPitchNeutral;
    Set(drv, 15, pitchAddr, sizeof neutralFirst, &neutralFirst);

    UInt32 csOn = 1;
    Set(drv, 16, csAddr, sizeof csOn, &csOn);
    UInt32 csNow = 99;
    Get(drv, 16, csAddr, 0, NULL, &csNow, sizeof csNow, &size);
    CHECK(csNow == 1, "Clock source bisa diubah ke 1");
    UInt32 csOdd = 7;
    OSStatus stOdd = Set(drv, 16, csAddr, sizeof csOdd, &csOdd);
    Get(drv, 16, csAddr, 0, NULL, &csNow, sizeof csNow, &size);
    CHECK(stOdd != kAudioHardwareNoError && csNow == 1, "Item selector 7 ditolak, item tetap 1 (status=%d)", (int)stOdd);

    // Item yang tersedia dan namanya.
    AudioObjectPropertyAddress availAddr = Addr(kAudioSelectorControlPropertyAvailableItems, kAudioObjectPropertyScopeGlobal);
    UInt32 avail[4] = { 9, 9, 9, 9 };
    Get(drv, 16, availAddr, 0, NULL, avail, sizeof avail, &size);
    CHECK(size == 2 * sizeof(UInt32) && avail[0] == 0 && avail[1] == 1, "Item selector clock: 0 dan 1");
    AudioObjectPropertyAddress nameAddr = Addr(kAudioSelectorControlPropertyItemName, kAudioObjectPropertyScopeGlobal);
    UInt32 itemZero = 0;
    CFStringRef itemName = NULL;
    Get(drv, 16, nameAddr, sizeof itemZero, &itemZero, &itemName, sizeof itemName, &size);
    char nameBuf[64];
    CFStringToBuf(itemName, nameBuf, sizeof nameBuf);
    CHECK(strcmp(nameBuf, "Nominal") == 0, "Nama item 0 = \"%s\"", nameBuf);
    if (itemName) CFRelease(itemName);
    csOn = 1;
    Set(drv, 16, csAddr, sizeof csOn, &csOn);

    double ticksNeutral = gDevices[0].activeTicksPerFrame;
    Float32 pitchFast = 0.0f;
    Set(drv, 15, pitchAddr, sizeof pitchFast, &pitchFast);
    CHECK(fabs(gDevices[0].activeTicksPerFrame / ticksNeutral - 1.01) < 1e-9,
          "Pitch 0 memperlambat clock 1 %% (rasio %.6f)", gDevices[0].activeTicksPerFrame / ticksNeutral);

    Float32 pitchSlow = 1.0f;
    Set(drv, 15, pitchAddr, sizeof pitchSlow, &pitchSlow);
    CHECK(fabs(gDevices[0].activeTicksPerFrame / ticksNeutral - 0.99) < 1e-9,
          "Pitch 1 memperlambat clock 1 %% (rasio %.6f)", gDevices[0].activeTicksPerFrame / ticksNeutral);

    Float32 pitchOut = 7.0f;
    Set(drv, 15, pitchAddr, sizeof pitchOut, &pitchOut);
    Get(drv, 15, pitchAddr, 0, NULL, &pitchNow, sizeof pitchNow, &size);
    CHECK(pitchNow == 1.0f, "Pitch di luar rentang dijepit ke 1 (%.2f)", pitchNow);

    Float32 pitchNaN = NAN;
    OSStatus stNaN = Set(drv, 15, pitchAddr, sizeof pitchNaN, &pitchNaN);
    Get(drv, 15, pitchAddr, 0, NULL, &pitchNow, sizeof pitchNow, &size);
    CHECK(stNaN != kAudioHardwareNoError && pitchNow == 1.0f, "Pitch NaN ditolak (status=%d)", (int)stNaN);

    Float32 neutral = kPitchNeutral;
    Set(drv, 15, pitchAddr, sizeof neutral, &neutral);
    CHECK(fabs(gDevices[0].activeTicksPerFrame / ticksNeutral - 1.0) < 1e-12, "Pitch kembali netral");

    // Clock tidak boleh mundur ketika pitch berubah di tengah jalan.
    Float64 pa = 0, pb = 0; UInt64 ha = 0, hb = 0;
    (*drv)->GetZeroTimeStamp(drv, 10, 1, &pa, &ha, &seed);
    Float32 pitchChange = 0.0f;
    Set(drv, 15, pitchAddr, sizeof pitchChange, &pitchChange);
    usleep(500000);   // lebih dari satu periode zero timestamp (~0,34 s di 48 kHz)
    (*drv)->GetZeroTimeStamp(drv, 10, 1, &pb, &hb, &seed);
    CHECK(hb > ha && pb > pa, "Clock maju melewati batas periode saat pitch berubah (%llu -> %llu, sampel %.0f -> %.0f)",
          (unsigned long long)ha, (unsigned long long)hb, pa, pb);
    Float32 pitchBack = kPitchNeutral;
    Set(drv, 15, pitchAddr, sizeof pitchBack, &pitchBack);
    UInt32 csOff = 0;
    Set(drv, 16, csAddr, sizeof csOff, &csOff);

    (*drv)->StopIO(drv, 10, 1);
    (*drv)->StopIO(drv, 20, 1);

    printf("Ganti sample rate\n");
    Float64 want96 = 96000.0;
    UInt64 seedBeforeChange = seed;
    int notesBefore = gPropertyNotes;
    OSStatus st = Set(drv, 10, rateAddr, sizeof want96, &want96);
    CHECK(st == kAudioHardwareNoError && DeviceRate(drv, 10) == 96000.0,
          "Set 96000 Hz pada perangkat 1 (status=%d, rate=%.0f)", (int)st, DeviceRate(drv, 10));
    CHECK(gRequests >= 1, "Perubahan lewat RequestDeviceConfigurationChange (%d permintaan)", gRequests);
    CHECK(gPropertyNotes > notesBefore, "Klien diberi tahu lewat PropertiesChanged");

    AudioStreamBasicDescription fmt96;
    Get(drv, 11, Addr(kAudioStreamPropertyVirtualFormat, kAudioObjectPropertyScopeGlobal), 0, NULL, &fmt96, sizeof fmt96, &size);
    CHECK(fmt96.mSampleRate == 96000.0, "Format stream output ikut ke 96 kHz");

    AudioStreamRangedDescription ranged[kSupportedRateCount];
    Get(drv, 11, Addr(kAudioStreamPropertyAvailableVirtualFormats, kAudioObjectPropertyScopeGlobal), 0, NULL, ranged, sizeof ranged, &size);
    CHECK(size == 13 * sizeof ranged[0] && ranged[0].mSampleRateRange.mMinimum == 768000 &&
          ranged[12].mSampleRateRange.mMinimum == 8000,
          "AvailableVirtualFormats perangkat 1: 13 format, dari 768000 turun ke 8000");

    CHECK(DeviceRate(drv, 20) == 48000.0, "Perangkat 2 tetap 48 kHz");

    (*drv)->StartIO(drv, 10, 1);
    (*drv)->GetZeroTimeStamp(drv, 10, 1, &s0, &h0, &seed);
    CHECK(seed > seedBeforeChange, "Seed baru setelah ganti rate (%llu -> %llu)",
          (unsigned long long)seedBeforeChange, (unsigned long long)seed);
    silent = 0;
    mismatches = RunLoopback(drv, 10, 11, 12, kBufferFrames, 1.0f, 0, &silent);
    CHECK(silent && mismatches == 0, "Loopback 96 kHz: input = output, %d tidak cocok", mismatches);
    (*drv)->StopIO(drv, 10, 1);

    printf("Ganti lewat format stream\n");
    AudioStreamBasicDescription want44 = MakeFormat(44100.0);
    st = Set(drv, 11, Addr(kAudioStreamPropertyPhysicalFormat, kAudioObjectPropertyScopeGlobal), sizeof want44, &want44);
    CHECK(st == kAudioHardwareNoError && DeviceRate(drv, 10) == 44100.0,
          "Set format fisik 44100 Hz mengubah rate perangkat (rate=%.0f)", DeviceRate(drv, 10));

    printf("Penolakan nilai tidak valid\n");
    Float64 bad = 22050.0;
    st = Set(drv, 10, rateAddr, sizeof bad, &bad);
    CHECK(st != kAudioHardwareNoError && DeviceRate(drv, 10) == 44100.0,
          "22050 Hz ditolak, rate tetap 44100 (status=%d)", (int)st);

    st = Set(drv, 10, rateAddr, 4, &want96);
    CHECK(st == kAudioHardwareBadPropertySizeError, "Ukuran data salah ditolak");

    AudioStreamBasicDescription notFloat = MakeFormat(48000.0);
    notFloat.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
    st = Set(drv, 11, Addr(kAudioStreamPropertyVirtualFormat, kAudioObjectPropertyScopeGlobal), sizeof notFloat, &notFloat);
    CHECK(st == kAudioDeviceUnsupportedFormatError && DeviceRate(drv, 10) == 44100.0,
          "Format integer ditolak");

    printf("Berhenti dan mulai ulang\n");
    UInt32 running = 99;
    Get(drv, 10, Addr(kAudioDevicePropertyDeviceIsRunning, kAudioObjectPropertyScopeGlobal), 0, NULL, &running, sizeof running, &size);
    CHECK(running == 0, "Perangkat berhenti setelah StopIO");

    printf("Ukuran buffer\n");
    AudioValueRange bufRange = { 0, 0 };
    Get(drv, 10, Addr(kAudioDevicePropertyBufferFrameSizeRange, kAudioObjectPropertyScopeGlobal), 0, NULL, &bufRange, sizeof bufRange, &size);
    CHECK(bufRange.mMinimum == kMinBufferFrames && bufRange.mMaximum == kMaxBufferFrames,
          "Rentang buffer %d..%d frame", kMinBufferFrames, kMaxBufferFrames);

    AudioObjectPropertyAddress bufSetAddr = Addr(kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeGlobal);
    UInt32 tooSmall = kMinBufferFrames - 1;
    st = Set(drv, 10, bufSetAddr, sizeof tooSmall, &tooSmall);
    CHECK(st == kAudioDeviceUnsupportedFormatError && BufferFrames(drv, 10) == kBufferFrames,
          "Buffer %u frame ditolak, tetap %u (status=%d)", tooSmall, BufferFrames(drv, 10), (int)st);

    UInt32 want256 = 256;
    int reqBefore = gRequests;
    st = Set(drv, 10, bufSetAddr, sizeof want256, &want256);
    CHECK(st == kAudioHardwareNoError && BufferFrames(drv, 10) == 256 && gRequests > reqBefore,
          "Set buffer 256 frame lewat RequestDeviceConfigurationChange (status=%d, buffer=%u)", (int)st, BufferFrames(drv, 10));

    UInt32 period = 0;
    Get(drv, 10, Addr(kAudioDevicePropertyZeroTimeStampPeriod, kAudioObjectPropertyScopeGlobal), 0, NULL, &period, sizeof period, &size);
    CHECK(period == 16384, "Periode zero timestamp tetap 16384 saat buffer 256 (%u)", period);

    UInt32 latency = 0;
    Get(drv, 12, Addr(kAudioStreamPropertyLatency, kAudioObjectPropertyScopeGlobal), 0, NULL, &latency, sizeof latency, &size);
    CHECK(latency == 0, "Latensi stream input dilaporkan 0 (%u)", latency);

    (*drv)->StartIO(drv, 10, 1);
    silent = 0;
    mismatches = RunLoopback(drv, 10, 11, 12, 256, 1.0f, 0, &silent);
    CHECK(silent && mismatches == 0, "Loopback 256 frame: input = output, %d tidak cocok", mismatches);
    (*drv)->StopIO(drv, 10, 1);

    UInt32 want512 = 512;
    st = Set(drv, 10, bufSetAddr, sizeof want512, &want512);
    CHECK(st == kAudioHardwareNoError && BufferFrames(drv, 10) == 512, "Kembali ke 512 frame");

    printf("\n%s (%d gagal)\n", gFailures == 0 ? "SEMUA LULUS" : "ADA YANG GAGAL", gFailures);
    return gFailures == 0 ? 0 : 1;
}
