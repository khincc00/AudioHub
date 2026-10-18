// Hak Cipta © 2026 taufiq sholikhin. Seluruh hak dilindungi. Situs: khincreator.com
// Lisensi lengkap ada di berkas LICENSE pada folder ini.
//
// Driver audio virtual AVHub v2: dua perangkat loopback stereo.
//   avhub_virtual1  dan  avhub_virtual2
//
// Ditulis dari nol berdasarkan antarmuka AudioServerPlugIn.h (SDK Apple). Kode driver lain
// tidak disalin; perilakunya hanya dijadikan acuan.
//
// Setiap perangkat punya:
//   - satu stream output dan satu stream input, masing-masing stereo (2 kanal, float32 interleaved)
//   - 13 sample rate, 8 kHz sampai 768 kHz
//   - kontrol volume dan mute pada sisi input (diterapkan saat input dibaca)
//   - koreksi pitch (clock) lewat properti kustom 'avpa', default netral
//
// Loopback tidak menambah offset sendiri: input membaca sample time yang sama dengan output.
// Jeda yang terlihat di aplikasi berasal dari HAL (sekitar dua buffer), bukan dari driver.

#include <CoreAudio/AudioHardware.h>
#include <CoreAudio/AudioServerPlugIn.h>
#include <CoreFoundation/CoreFoundation.h>
#include <CoreFoundation/CFPlugInCOM.h>
#include <mach/mach_time.h>
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define kManufacturer      "AVHub"
#define kModelName         "AVHub Virtual Loopback"
#define kModelUID          "AVHub Virtual Loopback"
#define kDefaultSampleRate 48000.0
#define kChangeActionSampleRate 1   // aksi untuk RequestDeviceConfigurationChange
#define kChangeActionBufferSize 2   // aksi untuk RequestDeviceConfigurationChange (ukuran buffer)
#define kSupportedRateCount 13
#define kChannels          2
#define kBufferFrames      512      // ukuran awal; bisa diganti klien dalam rentang di bawah
#define kMinBufferFrames   16
#define kMaxBufferFrames   4096
#define kRingFrames        65536    // harus pangkat dua; sekitar 1,4 detik di 48 kHz
#define kDeviceCount       2
#define kMaxOwnedObjects   10       // dipakai untuk menyusun daftar objek milik perangkat
#define kZeroTimeStampPeriodFrames 16384
#define kBufferSizeReported        4096
#define kVolumeMinDB       (-64.0f) // batas bawah kurva volume
#define kPitchNeutral      0.5f     // nilai 'avpa' yang tidak mengubah clock
#define kPitchRange        0.02     // pada 0 dan 1, clock berubah 1 % ke arah masing-masing
#define kBoxObjectID         2       // objek Box: pengelompokan perangkat di Audio MIDI Setup
#define kBoxUID              "com.avhub.driver.box"
#define kBoxName             "AVHub Box"
// Status untuk tulis yang sudah terlambat (output berakhir sebelum waktu saat ini).
#define kAVHubErrLateWrite   ((OSStatus)'what')

typedef enum {
    kKindNone,
    kKindPlugIn,
    kKindBox,
    kKindDevice,
    kKindStream,
    kKindVolume,
    kKindMute,
    kKindPitch,
    kKindClockSource,
    kKindOutVolume,
    kKindOutMute
} Kind;

typedef struct {
    const char *uid;            // juga dipakai sebagai nama perangkat
    AudioObjectID deviceID;
    AudioObjectID outputStreamID;
    AudioObjectID inputStreamID;
    AudioObjectID volumeID;     // kontrol volume sisi input
    AudioObjectID muteID;       // kontrol mute sisi input
    AudioObjectID pitchID;      // kontrol pitch: level control 0..1, 0,5 = netral
    AudioObjectID clockSourceID;// kontrol clock source: selector, item 0 = nominal, 1 = dengan pitch
    AudioObjectID outVolumeID;  // kontrol volume sisi output (nilai master yang sama dengan input)
    AudioObjectID volumeAltID;  // kontrol volume input kedua, nilai sama dengan volume utama
    AudioObjectID outMuteID;    // kontrol mute sisi output (nilai master yang sama dengan input)

    pthread_mutex_t lock;       // melindungi clock dan status IO (tidak dipakai di jalur sampel)
    Float32 ring[kRingFrames * kChannels];
    _Atomic UInt64 writtenUpTo; // sample time akhir dari data yang sudah ditulis
    UInt32 ioRefCount;

    UInt64 anchorHostTime;      // host time saat timeline dimulai
    Float64 elapsedTicks;       // host time sejak anchor, sampai batas periode terakhir
    UInt64 periodsSeen;         // jumlah periode zero timestamp yang sudah berlalu
    UInt64 seed;                // berubah setiap kali timeline di-reset
    Float64 hostTicksPerFrame;  // tick host per frame pada rate nominal
    Float64 adjustedTicksPerFrame; // sama, setelah koreksi pitch
    Float64 activeTicksPerFrame;// yang dipakai GetZeroTimeStamp: nominal atau disesuaikan

    Float64 sampleRate;         // sample rate aktif
    Float64 pendingRate;        // rate yang diminta, dibaca saat Perform...

    UInt32 bufferFrames;        // ukuran buffer IO aktif (frame)
    UInt32 pendingBuffer;       // ukuran buffer yang diminta, dibaca saat Perform...

    const Float64 *rates;       // daftar sample rate perangkat (menaik)
    UInt32 rateCount;
    UInt32 periodFrames;        // periode zero timestamp (frame), dilaporkan ke klien

    _Atomic Float32 volume;     // gain input, 0..1
    _Atomic UInt32 mute;        // 1 = input diam
    _Atomic Float32 pitch;      // 0..1, 0.5 = netral
    _Atomic UInt32 clockSource; // 0 = nominal, 1 = memakai koreksi pitch
} Device;

// Daftar 13 rate yang dilaporkan ke sistem.
static const Float64 kRatesAVHub[kSupportedRateCount] = {
    8000.0, 16000.0, 24000.0, 44100.0, 48000.0, 88200.0, 96000.0,
    176400.0, 192000.0, 352800.0, 384000.0, 705600.0, 768000.0,
};

static Device gDevices[kDeviceCount] = {
    { .uid = "avhub_virtual1", .deviceID = 10, .outputStreamID = 11, .inputStreamID = 12,
      .volumeID = 13, .muteID = 14, .pitchID = 15, .clockSourceID = 16, .outVolumeID = 17, .outMuteID = 18, .volumeAltID = 19, .lock = PTHREAD_MUTEX_INITIALIZER,
      .sampleRate = kDefaultSampleRate, .bufferFrames = kBufferFrames,
      .rates = kRatesAVHub, .rateCount = kSupportedRateCount, .periodFrames = kZeroTimeStampPeriodFrames,
      .volume = 1.0f, .mute = 0, .pitch = kPitchNeutral, .clockSource = 0 },
    { .uid = "avhub_virtual2", .deviceID = 20, .outputStreamID = 21, .inputStreamID = 22,
      .volumeID = 23, .muteID = 24, .pitchID = 25, .clockSourceID = 26, .outVolumeID = 27, .outMuteID = 28, .volumeAltID = 29, .lock = PTHREAD_MUTEX_INITIALIZER,
      .sampleRate = kDefaultSampleRate, .bufferFrames = kBufferFrames,
      .rates = kRatesAVHub, .rateCount = kSupportedRateCount, .periodFrames = kZeroTimeStampPeriodFrames,
      .volume = 1.0f, .mute = 0, .pitch = kPitchNeutral, .clockSource = 0 },
};

static const AudioObjectID kPlugInObjectID = kAudioObjectPlugInObject;
static AudioServerPlugInHostRef gHost = NULL;
static Float64 gHostTicksPerSecond = 0;

// Permintaan properti, dikumpulkan agar fungsi query tidak terlalu panjang.
typedef struct {
    AudioObjectPropertySelector sel;
    AudioObjectPropertyScope scope;
    UInt32 qualSize;
    const void *qual;
    UInt32 cap;
    void *out;
    UInt32 *outSize;
} Req;

static OSStatus Query(AudioObjectID obj, const AudioObjectPropertyAddress *addr,
                      UInt32 qualSize, const void *qual,
                      UInt32 cap, void *out, UInt32 *outSize);

//==================================================================================================
#pragma mark - Helper

static int Lookup(AudioObjectID obj, Kind *kind, int *isInput)
{
    *kind = kKindNone;
    *isInput = 0;
    if (obj == kPlugInObjectID) {
        *kind = kKindPlugIn;
        return -1;
    }
    if (obj == kBoxObjectID) {
        *kind = kKindBox;
        return -1;
    }
    for (int i = 0; i < kDeviceCount; i++) {
        if (obj == gDevices[i].deviceID) {
            *kind = kKindDevice;
            return i;
        }
        if (obj == gDevices[i].outputStreamID) {
            *kind = kKindStream;
            return i;
        }
        if (obj == gDevices[i].inputStreamID) {
            *kind = kKindStream;
            *isInput = 1;
            return i;
        }
        if (obj == gDevices[i].volumeID) {
            *kind = kKindVolume;
            *isInput = 1;
            return i;
        }
        if (obj == gDevices[i].muteID) {
            *kind = kKindMute;
            *isInput = 1;
            return i;
        }
        if (obj == gDevices[i].pitchID) {
            *kind = kKindPitch;
            return i;
        }
        if (obj == gDevices[i].clockSourceID) {
            *kind = kKindClockSource;
            return i;
        }
        if (obj == gDevices[i].outVolumeID) {
            *kind = kKindOutVolume;
            return i;
        }
        if (obj == gDevices[i].outMuteID) {
            *kind = kKindOutMute;
            return i;
        }
        if (obj == gDevices[i].volumeAltID) {
            *kind = kKindVolume;   // sama dengan volume utama: nilai master yang sama
            *isInput = 1;
            return i;
        }
    }
    return -1;
}

static Boolean IsSupportedRate(const Device *d, Float64 rate)
{
    for (UInt32 i = 0; i < d->rateCount; i++) {
        if (rate == d->rates[i]) return true;
    }
    return false;
}

static OSStatus ReturnBytes(const Req *r, const void *src, UInt32 size)
{
    *r->outSize = size;
    if (r->out) {
        if (r->cap < size) return kAudioHardwareBadPropertySizeError;
        if (size > 0) memcpy(r->out, src, size);
    }
    return kAudioHardwareNoError;
}

static OSStatus ReturnIDs(const Req *r, const AudioObjectID *ids, UInt32 count)
{
    return ReturnBytes(r, ids, count * sizeof(AudioObjectID));
}

// Cocokkan kelas objek dengan kelas yang diminta. Kontrol volume juga cocok dengan kelas level,
// dan kontrol mute cocok dengan kelas boolean, sesuai hierarki kelas di SDK.
static Boolean ClassMatches(AudioClassID objClass, AudioClassID want)
{
    if (want == objClass) return true;
    if (want == kAudioControlClassID) return true;
    if (want == kAudioLevelControlClassID &&
        (objClass == kAudioVolumeControlClassID || objClass == kAudioLevelControlClassID)) return true;
    if (want == kAudioBooleanControlClassID && objClass == kAudioMuteControlClassID) return true;
    if (want == kAudioSelectorControlClassID && objClass == kAudioClockSourceControlClassID) return true;
    return false;
}

// Mengembalikan daftar ID; jika qualifier berisi AudioClassID, hanya objek dengan kelas yang cocok.
static OSStatus ReturnFiltered(const Req *r, const AudioObjectID *ids, const AudioClassID *classes, UInt32 count)
{
    AudioObjectID keep[kMaxOwnedObjects];
    UInt32 n = 0;
    int filter = r->qual && r->qualSize == sizeof(AudioClassID);
    AudioClassID want = filter ? *(const AudioClassID *)r->qual : 0;

    for (UInt32 k = 0; k < count && n < kMaxOwnedObjects; k++) {
        if (!filter || ClassMatches(classes[k], want)) keep[n++] = ids[k];
    }
    return ReturnIDs(r, keep, n);
}

static OSStatus ReturnCFString(const Req *r, const char *s)
{
    *r->outSize = sizeof(CFStringRef);
    if (r->out) {
        if (r->cap < sizeof(CFStringRef)) return kAudioHardwareBadPropertySizeError;
        // Host mengambil alih kepemilikan string ini (+1).
        *(CFStringRef *)r->out = CFStringCreateWithCString(kCFAllocatorDefault, s, kCFStringEncodingUTF8);
    }
    return kAudioHardwareNoError;
}

static AudioStreamBasicDescription MakeFormat(Float64 rate)
{
    AudioStreamBasicDescription f;
    memset(&f, 0, sizeof f);
    f.mSampleRate = rate;
    f.mFormatID = kAudioFormatLinearPCM;
    f.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;   // interleaved
    f.mBytesPerPacket = kChannels * sizeof(Float32);
    f.mFramesPerPacket = 1;
    f.mBytesPerFrame = kChannels * sizeof(Float32);
    f.mChannelsPerFrame = kChannels;
    f.mBitsPerChannel = 32;
    return f;
}

// Kurva volume: skalar 0..1 dipetakan linear ke dB pada rentang -64..0.
static Float32 ScalarToDB(Float32 s)
{
    return kVolumeMinDB * (1.0f - s);
}

static Float32 DBToScalar(Float32 db)
{
    return 1.0f - db / kVolumeMinDB;
}

// Gain linear dari skalar. Skalar 0 membungkam input sepenuhnya.
static Float32 ScalarToGain(Float32 s)
{
    if (s <= 0.0f) return 0.0f;
    return powf(10.0f, ScalarToDB(s) / 20.0f);
}

//==================================================================================================
#pragma mark - Properti per jenis objek

static OSStatus PlugInProperty(const Req *r)
{
    AudioObjectID devs[kDeviceCount + 1];
    AudioClassID devClasses[kDeviceCount + 1];
    for (int i = 0; i < kDeviceCount; i++) {
        devs[i] = gDevices[i].deviceID;
        devClasses[i] = kAudioDeviceClassID;
    }
    devs[kDeviceCount] = kBoxObjectID;
    devClasses[kDeviceCount] = kAudioBoxClassID;
    AudioObjectID boxes[1] = { kBoxObjectID };

    switch (r->sel) {
    case kAudioObjectPropertyBaseClass: {
        AudioClassID v = kAudioObjectClassID;
        return ReturnBytes(r, &v, sizeof v);
    }
    case kAudioObjectPropertyClass: {
        AudioClassID v = kAudioPlugInClassID;
        return ReturnBytes(r, &v, sizeof v);
    }
    case kAudioObjectPropertyOwner: {
        AudioObjectID v = kAudioObjectUnknown;
        return ReturnBytes(r, &v, sizeof v);
    }
    case kAudioObjectPropertyManufacturer:
        return ReturnCFString(r, kManufacturer);
    case kAudioObjectPropertyOwnedObjects:
        return ReturnFiltered(r, devs, devClasses, kDeviceCount + 1);
    case kAudioPlugInPropertyDeviceList:
        return ReturnIDs(r, devs, kDeviceCount);
    case kAudioPlugInPropertyBoxList:
        return ReturnIDs(r, boxes, 1);
    case kAudioPlugInPropertyTranslateUIDToBox: {
        AudioObjectID v = kAudioObjectUnknown;
        if (r->out && r->qual && r->qualSize == sizeof(CFStringRef)) {
            CFStringRef uid = *(const CFStringRef *)r->qual;
            CFStringRef ref = CFStringCreateWithCString(kCFAllocatorDefault, kBoxUID, kCFStringEncodingUTF8);
            if (CFEqual(uid, ref)) v = kBoxObjectID;
            CFRelease(ref);
        }
        return ReturnBytes(r, &v, sizeof v);
    }
    case kAudioPlugInPropertyTranslateUIDToDevice: {
        AudioObjectID v = kAudioObjectUnknown;
        if (r->out && r->qual && r->qualSize == sizeof(CFStringRef)) {
            CFStringRef uid = *(const CFStringRef *)r->qual;
            for (int i = 0; i < kDeviceCount; i++) {
                CFStringRef ref = CFStringCreateWithCString(kCFAllocatorDefault, gDevices[i].uid, kCFStringEncodingUTF8);
                if (CFEqual(uid, ref)) v = gDevices[i].deviceID;
                CFRelease(ref);
            }
        }
        return ReturnBytes(r, &v, sizeof v);
    }
    case kAudioPlugInPropertyResourceBundle:
        return ReturnCFString(r, "");
    default:
        return kAudioHardwareUnknownPropertyError;
    }
}

static OSStatus DeviceProperty(const Req *r, int i)
{
    Device *d = &gDevices[i];
    AudioObjectID owned[10] = { d->outputStreamID, d->inputStreamID, d->volumeID, d->muteID,
                                d->pitchID, d->clockSourceID, d->outVolumeID, d->outMuteID, d->volumeAltID };
    AudioClassID ownedClasses[10] = { kAudioStreamClassID, kAudioStreamClassID,
                                      kAudioVolumeControlClassID, kAudioMuteControlClassID,
                                      kAudioLevelControlClassID, kAudioClockSourceControlClassID,
                                      kAudioVolumeControlClassID, kAudioMuteControlClassID,
                                      kAudioVolumeControlClassID };
    AudioObjectID streams[2] = { d->outputStreamID, d->inputStreamID };
    AudioObjectID controls[7] = { d->volumeID, d->muteID, d->pitchID, d->clockSourceID,
                                  d->outVolumeID, d->outMuteID, d->volumeAltID };

    switch (r->sel) {
    case kAudioObjectPropertyBaseClass: {
        AudioClassID v = kAudioObjectClassID;
        return ReturnBytes(r, &v, sizeof v);
    }
    case kAudioObjectPropertyClass: {
        AudioClassID v = kAudioDeviceClassID;
        return ReturnBytes(r, &v, sizeof v);
    }
    case kAudioObjectPropertyOwner: {
        AudioObjectID v = kPlugInObjectID;
        return ReturnBytes(r, &v, sizeof v);
    }
    case kAudioObjectPropertyName:
        return ReturnCFString(r, d->uid);
    case kAudioObjectPropertyManufacturer:
        return ReturnCFString(r, kManufacturer);
    case kAudioObjectPropertyOwnedObjects:
        return ReturnFiltered(r, owned, ownedClasses, 9);
    case kAudioObjectPropertyControlList:
        return ReturnIDs(r, controls, 7);
    case kAudioDevicePropertyDeviceUID:
        return ReturnCFString(r, d->uid);
    case kAudioDevicePropertyModelUID:
        return ReturnCFString(r, kModelUID);
    case kAudioDevicePropertyTransportType: {
        UInt32 v = kAudioDeviceTransportTypeVirtual;
        return ReturnBytes(r, &v, sizeof v);
    }
    case kAudioDevicePropertyRelatedDevices:
        return ReturnIDs(r, &d->deviceID, 1);
    case kAudioDevicePropertyClockDomain: {
        UInt32 v = 0;
        return ReturnBytes(r, &v, sizeof v);
    }
    case kAudioDevicePropertyDeviceIsAlive: {
        UInt32 v = 1;
        return ReturnBytes(r, &v, sizeof v);
    }
    case kAudioDevicePropertyDeviceIsRunning: {
        UInt32 v = d->ioRefCount > 0 ? 1 : 0;
        return ReturnBytes(r, &v, sizeof v);
    }
    case kAudioDevicePropertyDeviceCanBeDefaultDevice:
    case kAudioDevicePropertyDeviceCanBeDefaultSystemDevice: {
        UInt32 v = 1;
        return ReturnBytes(r, &v, sizeof v);
    }
    case kAudioDevicePropertyIsHidden: {
        UInt32 v = 0;
        return ReturnBytes(r, &v, sizeof v);
    }
    case kAudioDevicePropertyLatency:
    case kAudioDevicePropertySafetyOffset: {
        UInt32 v = 0;
        return ReturnBytes(r, &v, sizeof v);
    }
    case kAudioDevicePropertyNominalSampleRate: {
        Float64 v = d->sampleRate;
        return ReturnBytes(r, &v, sizeof v);
    }
    case kAudioDevicePropertyAvailableNominalSampleRates: {
        AudioValueRange v[kSupportedRateCount];
        for (UInt32 k = 0; k < d->rateCount; k++) {
            v[k].mMinimum = d->rates[k];
            v[k].mMaximum = d->rates[k];
        }
        return ReturnBytes(r, v, d->rateCount * sizeof v[0]);
    }
    case kAudioDevicePropertyZeroTimeStampPeriod: {
        UInt32 v = d->periodFrames;
        return ReturnBytes(r, &v, sizeof v);
    }
    case kAudioDevicePropertyBufferSize: {
        UInt32 v = kBufferSizeReported;
        return ReturnBytes(r, &v, sizeof v);
    }
    case kAudioDevicePropertyBufferFrameSize: {
        UInt32 v = d->bufferFrames;
        return ReturnBytes(r, &v, sizeof v);
    }
    case kAudioDevicePropertyBufferFrameSizeRange: {
        AudioValueRange v = { kMinBufferFrames, kMaxBufferFrames };
        return ReturnBytes(r, &v, sizeof v);
    }
    case kAudioDevicePropertyStreams: {
        if (r->scope == kAudioObjectPropertyScopeInput)
            return ReturnIDs(r, &d->inputStreamID, 1);
        if (r->scope == kAudioObjectPropertyScopeOutput)
            return ReturnIDs(r, &d->outputStreamID, 1);
        return ReturnIDs(r, streams, 2);
    }
    case kAudioDevicePropertyStreamConfiguration: {
        // Satu buffer berisi kChannels kanal (stereo).
        AudioBufferList abl;
        memset(&abl, 0, sizeof abl);
        abl.mNumberBuffers = 1;
        abl.mBuffers[0].mNumberChannels = kChannels;
        return ReturnBytes(r, &abl, offsetof(AudioBufferList, mBuffers) + sizeof(AudioBuffer));
    }
    default:
        return kAudioHardwareUnknownPropertyError;
    }
}

static OSStatus StreamProperty(const Req *r, int i, int isInput)
{
    Device *d = &gDevices[i];
    AudioStreamBasicDescription fmt = MakeFormat(d->sampleRate);
    // Format virtual diurutkan dari rate tertinggi ke terendah.
    AudioStreamRangedDescription ranged[kSupportedRateCount];
    for (UInt32 k = 0; k < d->rateCount; k++) {
        Float64 rate = d->rates[d->rateCount - 1 - k];
        ranged[k].mFormat = MakeFormat(rate);
        ranged[k].mSampleRateRange.mMinimum = rate;
        ranged[k].mSampleRateRange.mMaximum = rate;
    }

    switch (r->sel) {
    case kAudioObjectPropertyBaseClass: {
        AudioClassID v = kAudioObjectClassID;
        return ReturnBytes(r, &v, sizeof v);
    }
    case kAudioObjectPropertyClass: {
        AudioClassID v = kAudioStreamClassID;
        return ReturnBytes(r, &v, sizeof v);
    }
    case kAudioObjectPropertyOwner: {
        AudioObjectID v = d->deviceID;
        return ReturnBytes(r, &v, sizeof v);
    }
    case kAudioObjectPropertyOwnedObjects:
        return ReturnIDs(r, NULL, 0);
    case kAudioStreamPropertyIsActive: {
        UInt32 v = 1;
        return ReturnBytes(r, &v, sizeof v);
    }
    case kAudioStreamPropertyDirection: {
        UInt32 v = isInput ? 1 : 0;   // 0 = output, 1 = input
        return ReturnBytes(r, &v, sizeof v);
    }
    case kAudioStreamPropertyTerminalType: {
        UInt32 v = isInput ? kAudioStreamTerminalTypeMicrophone : kAudioStreamTerminalTypeSpeaker;
        return ReturnBytes(r, &v, sizeof v);
    }
    case kAudioStreamPropertyStartingChannel: {
        UInt32 v = 1;
        return ReturnBytes(r, &v, sizeof v);
    }
    case kAudioStreamPropertyLatency: {
        UInt32 v = 0;
        return ReturnBytes(r, &v, sizeof v);
    }
    case kAudioStreamPropertyVirtualFormat:
    case kAudioStreamPropertyPhysicalFormat:
        return ReturnBytes(r, &fmt, sizeof fmt);
    case kAudioStreamPropertyAvailableVirtualFormats:
    case kAudioStreamPropertyAvailablePhysicalFormats:
        return ReturnBytes(r, ranged, d->rateCount * sizeof ranged[0]);
    default:
        return kAudioHardwareUnknownPropertyError;
    }
}

static OSStatus BoxProperty(const Req *r)
{
    AudioObjectID devs[kDeviceCount];
    for (int i = 0; i < kDeviceCount; i++) devs[i] = gDevices[i].deviceID;

    switch (r->sel) {
    case kAudioObjectPropertyBaseClass: {
        AudioClassID v = kAudioObjectClassID;
        return ReturnBytes(r, &v, sizeof v);
    }
    case kAudioObjectPropertyClass: {
        AudioClassID v = kAudioBoxClassID;
        return ReturnBytes(r, &v, sizeof v);
    }
    case kAudioObjectPropertyOwner: {
        AudioObjectID v = kPlugInObjectID;
        return ReturnBytes(r, &v, sizeof v);
    }
    case kAudioObjectPropertyName:
        return ReturnCFString(r, kBoxName);
    case kAudioObjectPropertyManufacturer:
        return ReturnCFString(r, kManufacturer);
    case kAudioObjectPropertyOwnedObjects:
        return ReturnIDs(r, NULL, 0);
    case kAudioBoxPropertyBoxUID:
        return ReturnCFString(r, kBoxUID);
    case kAudioBoxPropertyTransportType: {
        UInt32 v = kAudioDeviceTransportTypeVirtual;
        return ReturnBytes(r, &v, sizeof v);
    }
    case kAudioBoxPropertyHasAudio: {
        UInt32 v = 1;
        return ReturnBytes(r, &v, sizeof v);
    }
    case kAudioBoxPropertyHasVideo:
    case kAudioBoxPropertyHasMIDI:
    case kAudioBoxPropertyIsProtected:
    case kAudioBoxPropertyAcquisitionFailed: {
        UInt32 v = 0;
        return ReturnBytes(r, &v, sizeof v);
    }
    case kAudioBoxPropertyAcquired: {
        UInt32 v = 1;   // driver selalu aktif, tidak perlu diakuisisi
        return ReturnBytes(r, &v, sizeof v);
    }
    case kAudioBoxPropertyDeviceList:
    case kAudioBoxPropertyClockDeviceList:
        return ReturnIDs(r, devs, kDeviceCount);
    default:
        return kAudioHardwareUnknownPropertyError;
    }
}

static OSStatus ControlProperty(const Req *r, int i, Kind kind)
{
    Device *d = &gDevices[i];
    int isVolume = kind == kKindVolume || kind == kKindOutVolume;
    int isMute = kind == kKindMute || kind == kKindOutMute;
    int isPitch = kind == kKindPitch;
    int isClock = kind == kKindClockSource;
    int isOutput = kind == kKindOutVolume || kind == kKindOutMute;

    // Volume dan mute input maupun output memakai nilai master yang sama.
    AudioObjectPropertyScope scope = isOutput ? kAudioObjectPropertyScopeOutput
                                   : (isVolume || isMute) ? kAudioObjectPropertyScopeInput
                                   : kAudioObjectPropertyScopeGlobal;
    AudioClassID cls = isVolume ? kAudioVolumeControlClassID
                     : isMute ? kAudioMuteControlClassID
                     : isPitch ? kAudioLevelControlClassID
                     : kAudioClockSourceControlClassID;
    AudioClassID base = isVolume ? kAudioLevelControlClassID
                      : isMute ? kAudioBooleanControlClassID
                      : isPitch ? kAudioControlClassID
                      : kAudioSelectorControlClassID;
    const char *name = isVolume ? "Volume" : isMute ? "Mute" : isPitch ? "Pitch" : "Clock Source";

    switch (r->sel) {
    case kAudioObjectPropertyBaseClass:
        return ReturnBytes(r, &base, sizeof base);
    case kAudioObjectPropertyClass:
        return ReturnBytes(r, &cls, sizeof cls);
    case kAudioObjectPropertyOwner: {
        AudioObjectID v = d->deviceID;
        return ReturnBytes(r, &v, sizeof v);
    }
    case kAudioObjectPropertyOwnedObjects:
        return ReturnIDs(r, NULL, 0);
    case kAudioObjectPropertyName:
        return ReturnCFString(r, name);
    case kAudioObjectPropertyManufacturer:
        return ReturnCFString(r, kManufacturer);
    case kAudioControlPropertyScope: {
        AudioObjectPropertyScope v = scope;
        return ReturnBytes(r, &v, sizeof v);
    }
    case kAudioControlPropertyElement: {
        AudioObjectPropertyElement v = kAudioObjectPropertyElementMain;
        return ReturnBytes(r, &v, sizeof v);
    }
    case kAudioLevelControlPropertyScalarValue: {
        if (isVolume) {
            Float32 v = atomic_load(&d->volume);
            return ReturnBytes(r, &v, sizeof v);
        }
        if (isPitch) {
            Float32 v = atomic_load(&d->pitch);
            return ReturnBytes(r, &v, sizeof v);
        }
        break;
    }
    case kAudioLevelControlPropertyDecibelValue: {
        if (!isVolume) break;
        Float32 v = ScalarToDB(atomic_load(&d->volume));
        return ReturnBytes(r, &v, sizeof v);
    }
    case kAudioLevelControlPropertyDecibelRange: {
        if (!isVolume) break;
        AudioValueRange v = { kVolumeMinDB, 0.0 };
        return ReturnBytes(r, &v, sizeof v);
    }
    case kAudioBooleanControlPropertyValue: {
        if (!isMute) break;
        UInt32 v = atomic_load(&d->mute) ? 1 : 0;
        return ReturnBytes(r, &v, sizeof v);
    }
    case kAudioSelectorControlPropertyCurrentItem: {
        if (!isClock) break;
        UInt32 v = atomic_load(&d->clockSource) ? 1 : 0;
        return ReturnBytes(r, &v, sizeof v);
    }
    case kAudioSelectorControlPropertyAvailableItems: {
        if (!isClock) break;
        UInt32 items[2] = { 0, 1 };
        return ReturnBytes(r, items, sizeof items);
    }
    case kAudioSelectorControlPropertyItemName: {
        if (!isClock) break;
        if (!r->qual || r->qualSize != sizeof(UInt32)) return kAudioHardwareBadPropertySizeError;
        UInt32 item = *(const UInt32 *)r->qual;
        if (item > 1) return kAudioHardwareUnknownPropertyError;
        return ReturnCFString(r, item == 0 ? "Nominal" : "Pitch Adjusted");
    }
    case kAudioSelectorControlPropertyItemKind: {
        if (!isClock) break;
        UInt32 v = 0;
        return ReturnBytes(r, &v, sizeof v);
    }
    default:
        break;
    }
    return kAudioHardwareUnknownPropertyError;
}

//==================================================================================================
#pragma mark - Clock dan ring buffer

// Menghitung tick host per frame dari sample rate aktif dan nilai pitch. Dipanggil di bawah lock
// (atau sebelum IO berjalan).
static void RecomputeTicks(Device *d)
{
    d->hostTicksPerFrame = gHostTicksPerSecond / d->sampleRate;
    Float64 pitch = atomic_load(&d->pitch);
    d->adjustedTicksPerFrame = d->hostTicksPerFrame * (1.0 + (kPitchNeutral - pitch) * kPitchRange);
    // Clock source 1 memakai tick yang sudah dikoreksi; 0 tetap memakai clock nominal.
    d->activeTicksPerFrame = atomic_load(&d->clockSource) ? d->adjustedTicksPerFrame : d->hostTicksPerFrame;
}

// Memulai timeline baru. Dipanggil di bawah lock.
static void ResetClock(Device *d)
{
    d->anchorHostTime = mach_absolute_time();
    d->elapsedTicks = 0;
    d->periodsSeen = 0;
    d->seed++;
    RecomputeTicks(d);
}

// Dipanggil saat IO berhenti (host yang menjamin). Mengganti rate dan memulai timeline baru.
static void ApplySampleRate(Device *d, Float64 rate)
{
    pthread_mutex_lock(&d->lock);
    d->sampleRate = rate;
    memset(d->ring, 0, sizeof d->ring);
    atomic_store_explicit(&d->writtenUpTo, 0, memory_order_release);
    ResetClock(d);
    pthread_mutex_unlock(&d->lock);
}

// Sama seperti ApplySampleRate, untuk ukuran buffer. Timeline baru dimulai dengan periode baru.
static void ApplyBufferSize(Device *d, UInt32 frames)
{
    pthread_mutex_lock(&d->lock);
    d->bufferFrames = frames;
    memset(d->ring, 0, sizeof d->ring);
    atomic_store_explicit(&d->writtenUpTo, 0, memory_order_release);
    ResetClock(d);
    pthread_mutex_unlock(&d->lock);
}

// Memberitahu klien bahwa format stream berubah setelah rate diganti.
static void NotifyStreamFormats(const Device *d)
{
    if (!gHost) return;
    AudioObjectPropertyAddress addrs[4] = {
        { kAudioStreamPropertyVirtualFormat, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain },
        { kAudioStreamPropertyPhysicalFormat, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain },
        { kAudioStreamPropertyAvailableVirtualFormats, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain },
        { kAudioStreamPropertyAvailablePhysicalFormats, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain },
    };
    gHost->PropertiesChanged(gHost, d->outputStreamID, 4, addrs);
    gHost->PropertiesChanged(gHost, d->inputStreamID, 4, addrs);
}

// Memberitahu klien bahwa ukuran buffer dan periode zero timestamp berubah.
static void NotifyBufferSize(const Device *d)
{
    if (!gHost) return;
    AudioObjectPropertyAddress addrs[2] = {
        { kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain },
        { kAudioDevicePropertyZeroTimeStampPeriod, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain },
    };
    gHost->PropertiesChanged(gHost, d->deviceID, 2, addrs);
}

// Memberitahu klien bahwa nilai kontrol input berubah.
static void NotifyControl(AudioObjectID control, AudioObjectPropertyScope scope, AudioObjectPropertySelector sel)
{
    if (!gHost) return;
    AudioObjectPropertyAddress addr = { sel, scope, kAudioObjectPropertyElementMain };
    gHost->PropertiesChanged(gHost, control, 1, &addr);
}

static void RingWrite(Device *d, UInt64 start, const Float32 *src, UInt32 frames)
{
    UInt64 prev = atomic_load_explicit(&d->writtenUpTo, memory_order_acquire);

    // Jika ada celah tanpa penulisan (output sempat berhenti), isi dengan silence
    // agar tidak terbaca data lama dari putaran ring sebelumnya.
    if (start > prev) {
        UInt64 gap = start - prev;
        if (gap > kRingFrames) gap = kRingFrames;
        for (UInt64 f = start - gap; f < start; f++) {
            memset(&d->ring[(f % kRingFrames) * kChannels], 0, sizeof(Float32) * kChannels);
        }
    }
    for (UInt32 k = 0; k < frames; k++) {
        memcpy(&d->ring[((start + k) % kRingFrames) * kChannels],
               &src[k * kChannels], sizeof(Float32) * kChannels);
    }
    atomic_store_explicit(&d->writtenUpTo, start + frames, memory_order_release);
}

// Membaca frame pada sample time yang sama dengan output. Jika belum seluruhnya tertulis,
// hasilnya silence, sehingga input tidak pernah membaca data setengah jadi.
static void RingRead(Device *d, Float64 sampleTime, Float32 *dst, UInt32 frames)
{
    UInt64 written = atomic_load_explicit(&d->writtenUpTo, memory_order_acquire);
    if (sampleTime < 0 || (UInt64)sampleTime + frames > written) {
        memset(dst, 0, sizeof(Float32) * kChannels * frames);
        return;
    }
    UInt64 base = (UInt64)sampleTime;
    for (UInt32 k = 0; k < frames; k++) {
        memcpy(&dst[k * kChannels], &d->ring[((base + k) % kRingFrames) * kChannels],
               sizeof(Float32) * kChannels);
    }
}

// Mute dan volume master diterapkan pada data input, setelah dibaca dari ring.
static void ApplyInputControls(Device *d, Float32 *buf, UInt32 frames)
{
    UInt32 n = frames * kChannels;
    Float32 gain = atomic_load(&d->mute) ? 0.0f : ScalarToGain(atomic_load(&d->volume));
    if (gain == 1.0f) return;
    if (gain == 0.0f) {
        memset(buf, 0, sizeof(Float32) * n);
        return;
    }
    for (UInt32 k = 0; k < n; k++) buf[k] *= gain;
}

//==================================================================================================
#pragma mark - Antarmuka driver (IUnknown)

static HRESULT DrvQueryInterface(void *inDriver, REFIID inUUID, LPVOID *outInterface);
static ULONG DrvAddRef(void *inDriver) { return 1; }
static ULONG DrvRelease(void *inDriver) { return 1; }

static OSStatus DrvInitialize(AudioServerPlugInDriverRef inDriver, AudioServerPlugInHostRef inHost)
{
    gHost = inHost;
    mach_timebase_info_data_t tb;
    mach_timebase_info(&tb);
    gHostTicksPerSecond = 1e9 * (Float64)tb.denom / (Float64)tb.numer;
    for (int i = 0; i < kDeviceCount; i++) RecomputeTicks(&gDevices[i]);
    return kAudioHardwareNoError;
}

static OSStatus DrvCreateDevice(AudioServerPlugInDriverRef inDriver, CFDictionaryRef inDescription,
                                const AudioServerPlugInClientInfo *inClientInfo, AudioObjectID *outDeviceObjectID)
{
    // Perangkat tetap (dua perangkat dibuat saat driver dimuat).
    return kAudioHardwareUnsupportedOperationError;
}

static OSStatus DrvDestroyDevice(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID)
{
    return kAudioHardwareUnsupportedOperationError;
}

static OSStatus DrvAddDeviceClient(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID,
                                   const AudioServerPlugInClientInfo *inClientInfo)
{
    Kind kind; int isInput;
    return Lookup(inDeviceObjectID, &kind, &isInput) >= 0 ? kAudioHardwareNoError : kAudioHardwareBadObjectError;
}

static OSStatus DrvRemoveDeviceClient(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID,
                                      const AudioServerPlugInClientInfo *inClientInfo)
{
    Kind kind; int isInput;
    return Lookup(inDeviceObjectID, &kind, &isInput) >= 0 ? kAudioHardwareNoError : kAudioHardwareBadObjectError;
}

static OSStatus DrvPerformDeviceConfigurationChange(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID,
                                                    UInt64 inChangeAction, void *inChangeInfo)
{
    Kind kind; int isInput;
    int i = Lookup(inDeviceObjectID, &kind, &isInput);
    if (i < 0 || kind != kKindDevice) return kAudioHardwareBadObjectError;
    if (inChangeInfo == NULL) return kAudioHardwareUnsupportedOperationError;

    if (inChangeAction == kChangeActionBufferSize) {
        UInt32 frames = *(const UInt32 *)inChangeInfo;
        if (frames < kMinBufferFrames || frames > kMaxBufferFrames) return kAudioDeviceUnsupportedFormatError;

        ApplyBufferSize(&gDevices[i], frames);
        NotifyBufferSize(&gDevices[i]);
        return kAudioHardwareNoError;
    }
    if (inChangeAction != kChangeActionSampleRate) return kAudioHardwareUnsupportedOperationError;

    Float64 rate = *(const Float64 *)inChangeInfo;
    if (!IsSupportedRate(&gDevices[i], rate)) return kAudioDeviceUnsupportedFormatError;

    ApplySampleRate(&gDevices[i], rate);
    NotifyStreamFormats(&gDevices[i]);
    return kAudioHardwareNoError;
}

static OSStatus DrvAbortDeviceConfigurationChange(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID,
                                                  UInt64 inChangeAction, void *inChangeInfo)
{
    return kAudioHardwareNoError;
}

static Boolean DrvHasProperty(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID,
                              const AudioObjectPropertyAddress *inAddress)
{
    UInt32 size = 0;
    return Query(inObjectID, inAddress, 0, NULL, 0, NULL, &size) == kAudioHardwareNoError;
}

static OSStatus DrvIsPropertySettable(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID,
                                      const AudioObjectPropertyAddress *inAddress, Boolean *outIsSettable)
{
    Kind kind; int isInput;
    Lookup(inObjectID, &kind, &isInput);
    if (kind == kKindNone) return kAudioHardwareBadObjectError;

    AudioObjectPropertySelector sel = inAddress->mSelector;
    Boolean settable = false;
    switch (kind) {
    case kKindDevice:
        settable = sel == kAudioDevicePropertyNominalSampleRate || sel == kAudioDevicePropertyBufferFrameSize;
        break;
    case kKindBox:
        settable = sel == kAudioObjectPropertyIdentify;
        break;
    case kKindStream:
        settable = sel == kAudioStreamPropertyVirtualFormat || sel == kAudioStreamPropertyPhysicalFormat;
        break;
    case kKindVolume:
    case kKindOutVolume:
        settable = sel == kAudioLevelControlPropertyScalarValue || sel == kAudioLevelControlPropertyDecibelValue;
        break;
    case kKindMute:
    case kKindOutMute:
        settable = sel == kAudioBooleanControlPropertyValue;
        break;
    case kKindPitch:
        settable = sel == kAudioLevelControlPropertyScalarValue;
        break;
    case kKindClockSource:
        settable = sel == kAudioSelectorControlPropertyCurrentItem;
        break;
    default:
        break;
    }
    *outIsSettable = settable;
    return kAudioHardwareNoError;
}

static OSStatus DrvGetPropertyDataSize(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID,
                                       const AudioObjectPropertyAddress *inAddress, UInt32 inQualifierDataSize,
                                       const void *inQualifierData, UInt32 *outDataSize)
{
    return Query(inObjectID, inAddress, inQualifierDataSize, inQualifierData, 0, NULL, outDataSize);
}

static OSStatus DrvGetPropertyData(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID,
                                   const AudioObjectPropertyAddress *inAddress, UInt32 inQualifierDataSize,
                                   const void *inQualifierData, UInt32 inDataSize, UInt32 *outDataSize, void *outData)
{
    return Query(inObjectID, inAddress, inQualifierDataSize, inQualifierData, inDataSize, outData, outDataSize);
}

// Ukuran buffer: validasi, lalu minta perubahan lewat host (dilakukan saat IO berhenti).
static OSStatus SetBufferSize(Device *d, UInt32 inDataSize, const void *inData)
{
    if (inDataSize != sizeof(UInt32)) return kAudioHardwareBadPropertySizeError;
    UInt32 frames = *(const UInt32 *)inData;
    if (frames < kMinBufferFrames || frames > kMaxBufferFrames) return kAudioDeviceUnsupportedFormatError;
    if (frames == d->bufferFrames) return kAudioHardwareNoError;
    if (!gHost) return kAudioHardwareUnsupportedOperationError;

    d->pendingBuffer = frames;
    return gHost->RequestDeviceConfigurationChange(gHost, d->deviceID, kChangeActionBufferSize, &d->pendingBuffer);
}

// Sample rate: validasi, lalu minta perubahan lewat host.
static OSStatus SetSampleRate(Device *d, Float64 rate)
{
    if (!IsSupportedRate(d, rate)) return kAudioDeviceUnsupportedFormatError;
    if (rate == d->sampleRate) return kAudioHardwareNoError;
    if (!gHost) return kAudioHardwareUnsupportedOperationError;

    // Perubahan rate harus lewat host. Host memanggil PerformDeviceConfigurationChange
    // setelah IO aman untuk diubah.
    d->pendingRate = rate;
    return gHost->RequestDeviceConfigurationChange(gHost, d->deviceID, kChangeActionSampleRate, &d->pendingRate);
}

static OSStatus SetStreamFormat(Device *d, UInt32 inDataSize, const void *inData)
{
    if (inDataSize != sizeof(AudioStreamBasicDescription)) return kAudioHardwareBadPropertySizeError;
    const AudioStreamBasicDescription *f = (const AudioStreamBasicDescription *)inData;
    if (f->mFormatID != kAudioFormatLinearPCM || f->mChannelsPerFrame != kChannels ||
        !(f->mFormatFlags & kAudioFormatFlagIsFloat) || f->mBitsPerChannel != 32) {
        return kAudioDeviceUnsupportedFormatError;
    }
    return SetSampleRate(d, f->mSampleRate);
}

// Pitch: 0..1, dijepit ke rentang. NaN ditolak. Perubahan tidak memulai timeline baru.
static OSStatus SetPitch(Device *d, UInt32 inDataSize, const void *inData)
{
    if (inDataSize != sizeof(Float32)) return kAudioHardwareBadPropertySizeError;
    Float32 v = *(const Float32 *)inData;
    if (isnan(v)) return kAudioHardwareIllegalOperationError;
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;

    pthread_mutex_lock(&d->lock);
    atomic_store(&d->pitch, v);
    RecomputeTicks(d);
    pthread_mutex_unlock(&d->lock);
    NotifyControl(d->pitchID, kAudioObjectPropertyScopeGlobal, kAudioLevelControlPropertyScalarValue);
    return kAudioHardwareNoError;
}

// Clock source: 0 = nominal, 1 = dengan koreksi pitch. Tidak memulai timeline baru.
static OSStatus SetClockSource(Device *d, UInt32 inDataSize, const void *inData)
{
    if (inDataSize != sizeof(UInt32)) return kAudioHardwareBadPropertySizeError;
    UInt32 v = *(const UInt32 *)inData;
    if (v > 1) return kAudioHardwareIllegalOperationError;   // item 0 = nominal, 1 = dengan pitch

    pthread_mutex_lock(&d->lock);
    atomic_store(&d->clockSource, v);
    RecomputeTicks(d);
    pthread_mutex_unlock(&d->lock);
    NotifyControl(d->clockSourceID, kAudioObjectPropertyScopeGlobal, kAudioSelectorControlPropertyCurrentItem);
    return kAudioHardwareNoError;
}

// Volume dan mute input maupun output memakai nilai master yang sama, jadi keduanya diberi tahu bersamaan.
static void NotifyVolume(const Device *d)
{
    NotifyControl(d->volumeID, kAudioObjectPropertyScopeInput, kAudioLevelControlPropertyScalarValue);
    NotifyControl(d->volumeID, kAudioObjectPropertyScopeInput, kAudioLevelControlPropertyDecibelValue);
    NotifyControl(d->volumeAltID, kAudioObjectPropertyScopeInput, kAudioLevelControlPropertyScalarValue);
    NotifyControl(d->volumeAltID, kAudioObjectPropertyScopeInput, kAudioLevelControlPropertyDecibelValue);
    NotifyControl(d->outVolumeID, kAudioObjectPropertyScopeOutput, kAudioLevelControlPropertyScalarValue);
    NotifyControl(d->outVolumeID, kAudioObjectPropertyScopeOutput, kAudioLevelControlPropertyDecibelValue);
}

static void NotifyMute(const Device *d)
{
    NotifyControl(d->muteID, kAudioObjectPropertyScopeInput, kAudioBooleanControlPropertyValue);
    NotifyControl(d->outMuteID, kAudioObjectPropertyScopeOutput, kAudioBooleanControlPropertyValue);
}

static OSStatus SetVolume(Device *d, AudioObjectPropertySelector sel, UInt32 inDataSize, const void *inData)
{
    if (inDataSize != sizeof(Float32)) return kAudioHardwareBadPropertySizeError;
    Float32 v = *(const Float32 *)inData;
    if (isnan(v)) return kAudioHardwareIllegalOperationError;

    Float32 scalar;
    if (sel == kAudioLevelControlPropertyScalarValue) {
        scalar = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    } else {
        Float32 db = v > 0.0f ? 0.0f : (v < kVolumeMinDB ? kVolumeMinDB : v);
        scalar = DBToScalar(db);
    }
    atomic_store(&d->volume, scalar);
    NotifyVolume(d);
    return kAudioHardwareNoError;
}

static OSStatus SetMute(Device *d, UInt32 inDataSize, const void *inData)
{
    if (inDataSize != sizeof(UInt32)) return kAudioHardwareBadPropertySizeError;
    atomic_store(&d->mute, *(const UInt32 *)inData ? 1u : 0u);
    NotifyMute(d);
    return kAudioHardwareNoError;
}

static OSStatus DrvSetPropertyData(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID,
                                   const AudioObjectPropertyAddress *inAddress, UInt32 inQualifierDataSize,
                                   const void *inQualifierData, UInt32 inDataSize, const void *inData)
{
    Kind kind; int isInput;
    int i = Lookup(inObjectID, &kind, &isInput);
    if (kind == kKindNone || kind == kKindPlugIn) return kAudioHardwareBadObjectError;

    // Box tidak punya indeks perangkat (i = -1), jadi pointer perangkat hanya dibuat untuk kind lain.
    Device *d = i >= 0 ? &gDevices[i] : NULL;
    AudioObjectPropertySelector sel = inAddress->mSelector;
    switch (kind) {
    case kKindDevice:
        if (sel == kAudioDevicePropertyBufferFrameSize) return SetBufferSize(d, inDataSize, inData);
        if (sel == kAudioDevicePropertyNominalSampleRate) {
            if (inDataSize != sizeof(Float64)) return kAudioHardwareBadPropertySizeError;
            return SetSampleRate(d, *(const Float64 *)inData);
        }
        return kAudioHardwareUnknownPropertyError;
    case kKindBox:
        // Identify hanya perlu diterima. Driver tidak punya lampu atau tampilan untuk ditampilkan.
        if (sel == kAudioObjectPropertyIdentify) {
            return inDataSize == sizeof(UInt32) ? kAudioHardwareNoError : kAudioHardwareBadPropertySizeError;
        }
        return kAudioHardwareUnknownPropertyError;
    case kKindStream:
        if (sel == kAudioStreamPropertyVirtualFormat || sel == kAudioStreamPropertyPhysicalFormat)
            return SetStreamFormat(d, inDataSize, inData);
        return kAudioHardwareUnknownPropertyError;
    case kKindVolume:
    case kKindOutVolume:
        if (sel == kAudioLevelControlPropertyScalarValue || sel == kAudioLevelControlPropertyDecibelValue)
            return SetVolume(d, sel, inDataSize, inData);
        return kAudioHardwareUnknownPropertyError;
    case kKindMute:
    case kKindOutMute:
        if (sel == kAudioBooleanControlPropertyValue) return SetMute(d, inDataSize, inData);
        return kAudioHardwareUnknownPropertyError;
    case kKindPitch:
        if (sel == kAudioLevelControlPropertyScalarValue) return SetPitch(d, inDataSize, inData);
        return kAudioHardwareUnknownPropertyError;
    case kKindClockSource:
        if (sel == kAudioSelectorControlPropertyCurrentItem) return SetClockSource(d, inDataSize, inData);
        return kAudioHardwareUnknownPropertyError;
    default:
        return kAudioHardwareUnknownPropertyError;
    }
}

static OSStatus DrvStartIO(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, UInt32 inClientID)
{
    Kind kind; int isInput;
    int i = Lookup(inDeviceObjectID, &kind, &isInput);
    if (i < 0 || kind != kKindDevice) return kAudioHardwareBadObjectError;

    Device *d = &gDevices[i];
    pthread_mutex_lock(&d->lock);
    if (d->ioRefCount == 0) {
        // Mulai timeline baru dengan ring yang kosong.
        memset(d->ring, 0, sizeof d->ring);
        atomic_store_explicit(&d->writtenUpTo, 0, memory_order_release);
        ResetClock(d);
    }
    d->ioRefCount++;
    pthread_mutex_unlock(&d->lock);
    return kAudioHardwareNoError;
}

static OSStatus DrvStopIO(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, UInt32 inClientID)
{
    Kind kind; int isInput;
    int i = Lookup(inDeviceObjectID, &kind, &isInput);
    if (i < 0 || kind != kKindDevice) return kAudioHardwareBadObjectError;

    Device *d = &gDevices[i];
    pthread_mutex_lock(&d->lock);
    if (d->ioRefCount > 0) d->ioRefCount--;
    pthread_mutex_unlock(&d->lock);
    return kAudioHardwareNoError;
}

static OSStatus DrvGetZeroTimeStamp(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, UInt32 inClientID,
                                    Float64 *outSampleTime, UInt64 *outHostTime, UInt64 *outSeed)
{
    Kind kind; int isInput;
    int i = Lookup(inDeviceObjectID, &kind, &isInput);
    if (i < 0 || kind != kKindDevice) return kAudioHardwareBadObjectError;

    Device *d = &gDevices[i];
    pthread_mutex_lock(&d->lock);
    // Waktu berjalan sejak anchor dibagi periode; periode yang sudah lewat dihitung sekali
    // dan tidak pernah mundur, meskipun koreksi pitch mengubah panjang periode.
    Float64 period = d->activeTicksPerFrame * d->periodFrames;
    Float64 since = (Float64)(mach_absolute_time() - d->anchorHostTime);
    if (since - d->elapsedTicks >= period) {
        UInt64 n = (UInt64)((since - d->elapsedTicks) / period);
        d->periodsSeen += n;
        d->elapsedTicks += (Float64)n * period;
    }
    *outSampleTime = (Float64)(d->periodsSeen * d->periodFrames);
    *outHostTime = d->anchorHostTime + (UInt64)d->elapsedTicks;
    *outSeed = d->seed;
    pthread_mutex_unlock(&d->lock);
    return kAudioHardwareNoError;
}

static OSStatus DrvWillDoIOOperation(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, UInt32 inClientID,
                                     UInt32 inOperationID, Boolean *outWillDo, Boolean *outWillDoInPlace)
{
    *outWillDo = (inOperationID == kAudioServerPlugInIOOperationWriteMix ||
                  inOperationID == kAudioServerPlugInIOOperationReadInput);
    *outWillDoInPlace = true;
    return kAudioHardwareNoError;
}

static OSStatus DrvBeginIOOperation(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, UInt32 inClientID,
                                    UInt32 inOperationID, UInt32 inIOBufferFrameSize,
                                    const AudioServerPlugInIOCycleInfo *inIOCycleInfo)
{
    return kAudioHardwareNoError;
}

static OSStatus DrvDoIOOperation(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID,
                                 AudioObjectID inStreamObjectID, UInt32 inClientID, UInt32 inOperationID,
                                 UInt32 inIOBufferFrameSize, const AudioServerPlugInIOCycleInfo *inIOCycleInfo,
                                 void *ioMainBuffer, void *ioSecondaryBuffer)
{
    Kind kind; int isInput;
    int i = Lookup(inStreamObjectID, &kind, &isInput);
    if (i < 0 || kind != kKindStream || ioMainBuffer == NULL) return kAudioHardwareBadObjectError;

    Device *d = &gDevices[i];
    if (inOperationID == kAudioServerPlugInIOOperationWriteMix && !isInput) {
        // Output yang sudah lewat dari waktu sekarang tidak ditulis ke ring.
        if (inIOCycleInfo->mOutputTime.mSampleTime + inIOBufferFrameSize < inIOCycleInfo->mCurrentTime.mSampleTime)
            return kAVHubErrLateWrite;
        RingWrite(d, (UInt64)inIOCycleInfo->mOutputTime.mSampleTime, (const Float32 *)ioMainBuffer, inIOBufferFrameSize);
    } else if (inOperationID == kAudioServerPlugInIOOperationReadInput && isInput) {
        RingRead(d, inIOCycleInfo->mInputTime.mSampleTime, (Float32 *)ioMainBuffer, inIOBufferFrameSize);
        ApplyInputControls(d, (Float32 *)ioMainBuffer, inIOBufferFrameSize);
    }
    return kAudioHardwareNoError;
}

static OSStatus DrvEndIOOperation(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, UInt32 inClientID,
                                  UInt32 inOperationID, UInt32 inIOBufferFrameSize,
                                  const AudioServerPlugInIOCycleInfo *inIOCycleInfo)
{
    return kAudioHardwareNoError;
}

static AudioServerPlugInDriverInterface gDriverInterface = {
    ._reserved = NULL,
    .QueryInterface = DrvQueryInterface,
    .AddRef = DrvAddRef,
    .Release = DrvRelease,
    .Initialize = DrvInitialize,
    .CreateDevice = DrvCreateDevice,
    .DestroyDevice = DrvDestroyDevice,
    .AddDeviceClient = DrvAddDeviceClient,
    .RemoveDeviceClient = DrvRemoveDeviceClient,
    .PerformDeviceConfigurationChange = DrvPerformDeviceConfigurationChange,
    .AbortDeviceConfigurationChange = DrvAbortDeviceConfigurationChange,
    .HasProperty = DrvHasProperty,
    .IsPropertySettable = DrvIsPropertySettable,
    .GetPropertyDataSize = DrvGetPropertyDataSize,
    .GetPropertyData = DrvGetPropertyData,
    .SetPropertyData = DrvSetPropertyData,
    .StartIO = DrvStartIO,
    .StopIO = DrvStopIO,
    .GetZeroTimeStamp = DrvGetZeroTimeStamp,
    .WillDoIOOperation = DrvWillDoIOOperation,
    .BeginIOOperation = DrvBeginIOOperation,
    .DoIOOperation = DrvDoIOOperation,
    .EndIOOperation = DrvEndIOOperation,
};

static AudioServerPlugInDriverInterface *gDriverInterfacePtr = &gDriverInterface;
static AudioServerPlugInDriverRef gDriverRef = &gDriverInterfacePtr;

// Dispatcher properti untuk semua jenis objek.
static OSStatus Query(AudioObjectID obj, const AudioObjectPropertyAddress *addr,
                      UInt32 qualSize, const void *qual,
                      UInt32 cap, void *out, UInt32 *outSize)
{
    Req r = { addr->mSelector, addr->mScope, qualSize, qual, cap, out, outSize };
    Kind kind;
    int isInput;
    int i = Lookup(obj, &kind, &isInput);

    switch (kind) {
    case kKindPlugIn: return PlugInProperty(&r);
    case kKindBox:    return BoxProperty(&r);
    case kKindDevice: return DeviceProperty(&r, i);
    case kKindStream: return StreamProperty(&r, i, isInput);
    case kKindVolume:
    case kKindMute:
    case kKindPitch:
    case kKindClockSource:
    case kKindOutVolume:
    case kKindOutMute:
        return ControlProperty(&r, i, kind);
    default:          return kAudioHardwareBadObjectError;
    }
}

static HRESULT DrvQueryInterface(void *inDriver, REFIID inUUID, LPVOID *outInterface)
{
    CFUUIDRef requested = CFUUIDCreateFromUUIDBytes(kCFAllocatorDefault, inUUID);
    HRESULT result;
    if (CFEqual(requested, kAudioServerPlugInDriverInterfaceUUID) || CFEqual(requested, IUnknownUUID)) {
        *outInterface = gDriverRef;
        result = S_OK;
    } else {
        *outInterface = NULL;
        result = E_NOINTERFACE;
    }
    CFRelease(requested);
    return result;
}

//==================================================================================================
#pragma mark - Titik masuk plug-in

// Dipanggil oleh CFPlugIn. Nama ini harus sama dengan CFPlugInFactories di Info.plist.
__attribute__((visibility("default")))
void *AVHubDriver_Create(CFAllocatorRef allocator, CFUUIDRef typeUUID)
{
    (void)allocator;
    if (CFEqual(typeUUID, kAudioServerPlugInTypeUUID)) {
        return gDriverRef;
    }
    return NULL;
}
