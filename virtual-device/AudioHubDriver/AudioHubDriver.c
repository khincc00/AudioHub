// AudioHub Virtual: AudioServerPlugIn (HAL).
// Dua perangkat virtual, masing-masing 2 in / 2 out, 48 kHz, float32 interleaved:
//   AudioHub Virtual   (UID com.audiohub.virtual.device)
//   AudioHub Virtual 2 (UID com.audiohub.virtual.device2)
// Output yang ditulis aplikasi disimpan ke ring buffer milik perangkat itu, lalu dibaca sebagai input.
// Setiap perangkat punya ring buffer, penghitung IO, dan jangkar clock sendiri.
// Sinkronisasi antar-thread memakai fence memori; belum memakai lock di jalur IO.
//
// PERINGATAN: driver ini berjalan di dalam coreaudiod. Pasang hanya di mesin uji.
#include <CoreAudio/AudioServerPlugIn.h>
#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <stdatomic.h>
#include <string.h>

#define kAH_SampleRate 48000.0
#define kAH_Channels 2
#define kAH_BufferFrames 512
#define kAH_RingFrames 16384
#define kAH_NumDevices 2

// ID objek: perangkat d punya ID 2+3d, input 3+3d, output 4+3d
#define kAH_DeviceID(d) ((AudioObjectID)(2 + 3 * (d)))
#define kAH_InputStreamID(d) ((AudioObjectID)(3 + 3 * (d)))
#define kAH_OutputStreamID(d) ((AudioObjectID)(4 + 3 * (d)))

static const char* kAH_Names[kAH_NumDevices] = {"AudioHub Virtual", "AudioHub Virtual 2"};
static const char* kAH_UIDs[kAH_NumDevices] = {"com.audiohub.virtual.device", "com.audiohub.virtual.device2"};
static const char* kAH_ModelUIDs[kAH_NumDevices] = {"com.audiohub.virtual.model", "com.audiohub.virtual.model2"};

static AudioServerPlugInHostRef gHost = NULL;
static UInt32 gRefCount = 1;
static Float64 gHostTicksPerPeriod = 0;
static pthread_mutex_t gClockMutex = PTHREAD_MUTEX_INITIALIZER;

// State per perangkat
static _Atomic int gIORunning[kAH_NumDevices];
static UInt64 gAnchorHostTime[kAH_NumDevices];
static float gRing[kAH_NumDevices][kAH_RingFrames * kAH_Channels];
// Penanda tulis per perangkat: sampel di bawah nilai ini sudah ditulis oleh output.
// Pembaca memakai nol untuk posisi yang belum ditulis, bukan data lama dari putaran sebelumnya.
static _Atomic SInt64 gWriteEnd[kAH_NumDevices];

// Perangkat dari ID objek: -1 jika bukan perangkat atau stream AudioHub
static int devOf(AudioObjectID obj) {
    if (obj < 2) return -1;
    int d = (int)((obj - 2) / 3);
    if (d >= kAH_NumDevices) return -1;
    return d;
}

// 0 = perangkat, 1 = input stream, 2 = output stream
static int partOf(AudioObjectID obj) {
    return (int)((obj - 2) % 3);
}

// ---- Pendaftaran driver ----

static HRESULT QueryInterface(void* driver, REFIID uuid, LPVOID* outInterface);
static ULONG AddRef(void* driver);
static ULONG Release(void* driver);
static OSStatus Initialize(AudioServerPlugInDriverRef driver, AudioServerPlugInHostRef host);
static OSStatus CreateDevice(AudioServerPlugInDriverRef driver, CFDictionaryRef desc, const AudioServerPlugInClientInfo* client, AudioObjectID* outDevice);
static OSStatus DestroyDevice(AudioServerPlugInDriverRef driver, AudioObjectID device);
static OSStatus AddDeviceClient(AudioServerPlugInDriverRef driver, AudioObjectID device, const AudioServerPlugInClientInfo* client);
static OSStatus RemoveDeviceClient(AudioServerPlugInDriverRef driver, AudioObjectID device, const AudioServerPlugInClientInfo* client);
static OSStatus PerformDeviceConfigurationChange(AudioServerPlugInDriverRef driver, AudioObjectID device, UInt64 action, void* data);
static OSStatus AbortDeviceConfigurationChange(AudioServerPlugInDriverRef driver, AudioObjectID device, UInt64 action, void* data);
static Boolean HasProperty(AudioServerPlugInDriverRef driver, AudioObjectID object, pid_t pid, const AudioObjectPropertyAddress* address);
static OSStatus IsPropertySettable(AudioServerPlugInDriverRef driver, AudioObjectID object, pid_t pid, const AudioObjectPropertyAddress* address, Boolean* outSettable);
static OSStatus GetPropertyDataSize(AudioServerPlugInDriverRef driver, AudioObjectID object, pid_t pid, const AudioObjectPropertyAddress* address, UInt32 qualSize, const void* qual, UInt32* outSize);
static OSStatus GetPropertyData(AudioServerPlugInDriverRef driver, AudioObjectID object, pid_t pid, const AudioObjectPropertyAddress* address, UInt32 qualSize, const void* qual, UInt32 dataSize, UInt32* outSize, void* outData);
static OSStatus SetPropertyData(AudioServerPlugInDriverRef driver, AudioObjectID object, pid_t pid, const AudioObjectPropertyAddress* address, UInt32 qualSize, const void* qual, UInt32 dataSize, const void* data);
static OSStatus StartIO(AudioServerPlugInDriverRef driver, AudioObjectID device, UInt32 clientID);
static OSStatus StopIO(AudioServerPlugInDriverRef driver, AudioObjectID device, UInt32 clientID);
static OSStatus GetZeroTimeStamp(AudioServerPlugInDriverRef driver, AudioObjectID device, UInt32 clientID, Float64* outSampleTime, UInt64* outHostTime, UInt64* outSeed);
static OSStatus WillDoIOOperation(AudioServerPlugInDriverRef driver, AudioObjectID device, UInt32 clientID, UInt32 operation, Boolean* outWillDo, Boolean* outWillDoInPlace);
static OSStatus BeginIOOperation(AudioServerPlugInDriverRef driver, AudioObjectID device, UInt32 clientID, UInt32 operation, UInt32 bufferSize, const AudioServerPlugInIOCycleInfo* cycle);
static OSStatus DoIOOperation(AudioServerPlugInDriverRef driver, AudioObjectID device, AudioObjectID stream, UInt32 clientID, UInt32 operation, UInt32 bufferSize, const AudioServerPlugInIOCycleInfo* cycle, void* mainBuffer, void* secondaryBuffer);
static OSStatus EndIOOperation(AudioServerPlugInDriverRef driver, AudioObjectID device, UInt32 clientID, UInt32 operation, UInt32 bufferSize, const AudioServerPlugInIOCycleInfo* cycle);

static AudioServerPlugInDriverInterface gDriverInterface = {
    NULL,
    QueryInterface, AddRef, Release,
    Initialize, CreateDevice, DestroyDevice,
    AddDeviceClient, RemoveDeviceClient,
    PerformDeviceConfigurationChange, AbortDeviceConfigurationChange,
    HasProperty, IsPropertySettable, GetPropertyDataSize, GetPropertyData, SetPropertyData,
    StartIO, StopIO, GetZeroTimeStamp,
    WillDoIOOperation, BeginIOOperation, DoIOOperation, EndIOOperation,
};
static AudioServerPlugInDriverInterface* gDriverInterfacePtr = &gDriverInterface;
static AudioServerPlugInDriverRef gDriverRef = &gDriverInterfacePtr;

// Titik masuk yang dicari Info.plist (CFPlugInFactories)
void* AudioHubDriverFactory(CFAllocatorRef allocator, CFUUIDRef typeUUID) {
    (void)allocator;
    if (!CFEqual(typeUUID, kAudioServerPlugInTypeUUID)) return NULL;
    return gDriverRef;
}

static HRESULT QueryInterface(void* driver, REFIID uuid, LPVOID* outInterface) {
    (void)driver;
    CFUUIDRef req = CFUUIDCreateFromUUIDBytes(NULL, uuid);
    HRESULT result = E_NOINTERFACE;
    if (CFEqual(req, IUnknownUUID) || CFEqual(req, kAudioServerPlugInDriverInterfaceUUID)) {
        AddRef(driver);
        *outInterface = gDriverRef;
        result = S_OK;
    } else {
        *outInterface = NULL;
    }
    CFRelease(req);
    return result;
}

static ULONG AddRef(void* driver) { (void)driver; return ++gRefCount; }
static ULONG Release(void* driver) { (void)driver; return gRefCount > 1 ? --gRefCount : 1; }

static OSStatus Initialize(AudioServerPlugInDriverRef driver, AudioServerPlugInHostRef host) {
    (void)driver;
    gHost = host;
    mach_timebase_info_data_t tb;
    mach_timebase_info(&tb);
    Float64 hostTicksPerSecond = 1e9 * (Float64)tb.denom / (Float64)tb.numer;
    gHostTicksPerPeriod = hostTicksPerSecond * kAH_BufferFrames / kAH_SampleRate;
    return kAudioHardwareNoError;
}

// Perangkat tetap (dua buah) dibuat saat driver dimuat; CreateDevice tidak didukung.
static OSStatus CreateDevice(AudioServerPlugInDriverRef d, CFDictionaryRef desc, const AudioServerPlugInClientInfo* c, AudioObjectID* out) {
    (void)d; (void)desc; (void)c; (void)out;
    return kAudioHardwareUnsupportedOperationError;
}
static OSStatus DestroyDevice(AudioServerPlugInDriverRef d, AudioObjectID dev) {
    (void)d; (void)dev;
    return kAudioHardwareUnsupportedOperationError;
}
static OSStatus AddDeviceClient(AudioServerPlugInDriverRef d, AudioObjectID dev, const AudioServerPlugInClientInfo* c) {
    (void)d; (void)dev; (void)c;
    return kAudioHardwareNoError;
}
static OSStatus RemoveDeviceClient(AudioServerPlugInDriverRef d, AudioObjectID dev, const AudioServerPlugInClientInfo* c) {
    (void)d; (void)dev; (void)c;
    return kAudioHardwareNoError;
}
static OSStatus PerformDeviceConfigurationChange(AudioServerPlugInDriverRef d, AudioObjectID dev, UInt64 a, void* data) {
    (void)d; (void)dev; (void)a; (void)data;
    return kAudioHardwareNoError;
}
static OSStatus AbortDeviceConfigurationChange(AudioServerPlugInDriverRef d, AudioObjectID dev, UInt64 a, void* data) {
    (void)d; (void)dev; (void)a; (void)data;
    return kAudioHardwareNoError;
}

// ---- Format ----

static AudioStreamBasicDescription streamFormat(void) {
    AudioStreamBasicDescription f = {0};
    f.mSampleRate = kAH_SampleRate;
    f.mFormatID = kAudioFormatLinearPCM;
    f.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsPacked;
    f.mBytesPerPacket = kAH_Channels * sizeof(float);
    f.mFramesPerPacket = 1;
    f.mBytesPerFrame = kAH_Channels * sizeof(float);
    f.mChannelsPerFrame = kAH_Channels;
    f.mBitsPerChannel = 32;
    return f;
}

// ---- Properti ----
// Untuk setiap properti: jika data == NULL, hanya kirim ukuran. Jika tidak, isi data.

#define SIZE_OR_COPY(src, n) do { \
    if (!data) { *outSize = (UInt32)(n); return kAudioHardwareNoError; } \
    if (dataSize < (UInt32)(n)) return kAudioHardwareBadPropertySizeError; \
    memcpy(data, (src), (n)); *outSize = (UInt32)(n); return kAudioHardwareNoError; } while (0)

static OSStatus getProperty(AudioObjectID obj, const AudioObjectPropertyAddress* a, UInt32 dataSize, UInt32* outSize, void* data) {
    AudioObjectID ids[kAH_NumDevices * 3];

    if (obj == kAudioObjectPlugInObject) {
        switch (a->mSelector) {
        case kAudioObjectPropertyBaseClass: { AudioClassID v = kAudioObjectClassID; SIZE_OR_COPY(&v, sizeof v); }
        case kAudioObjectPropertyClass: { AudioClassID v = kAudioPlugInClassID; SIZE_OR_COPY(&v, sizeof v); }
        case kAudioObjectPropertyOwner: { AudioObjectID v = kAudioObjectUnknown; SIZE_OR_COPY(&v, sizeof v); }
        case kAudioObjectPropertyManufacturer: { CFStringRef v = CFSTR("AudioHub"); SIZE_OR_COPY(&v, sizeof v); }
        case kAudioObjectPropertyOwnedObjects:
        case kAudioPlugInPropertyDeviceList: {
            for (int d = 0; d < kAH_NumDevices; d++) ids[d] = kAH_DeviceID(d);
            SIZE_OR_COPY(ids, kAH_NumDevices * sizeof(AudioObjectID)); }
        case kAudioPlugInPropertyResourceBundle: { CFStringRef v = CFSTR(""); SIZE_OR_COPY(&v, sizeof v); }
        default: break;
        }
        return kAudioHardwareUnknownPropertyError;
    }

    int d = devOf(obj);
    if (d < 0) return kAudioHardwareUnknownPropertyError;
    int part = partOf(obj);

    if (part == 0) {
        switch (a->mSelector) {
        case kAudioObjectPropertyBaseClass: { AudioClassID v = kAudioObjectClassID; SIZE_OR_COPY(&v, sizeof v); }
        case kAudioObjectPropertyClass: { AudioClassID v = kAudioDeviceClassID; SIZE_OR_COPY(&v, sizeof v); }
        case kAudioObjectPropertyOwner: { AudioObjectID v = kAudioObjectPlugInObject; SIZE_OR_COPY(&v, sizeof v); }
        case kAudioObjectPropertyName: { CFStringRef v = CFStringCreateWithCString(NULL, kAH_Names[d], kCFStringEncodingUTF8);
            if (!data) { *outSize = sizeof v; CFRelease(v); return kAudioHardwareNoError; }
            if (dataSize < sizeof v) { CFRelease(v); return kAudioHardwareBadPropertySizeError; }
            *(CFStringRef*)data = v; *outSize = sizeof v; return kAudioHardwareNoError; }
        case kAudioObjectPropertyManufacturer: { CFStringRef v = CFSTR("AudioHub"); SIZE_OR_COPY(&v, sizeof v); }
        case kAudioDevicePropertyDeviceUID: { CFStringRef v = CFStringCreateWithCString(NULL, kAH_UIDs[d], kCFStringEncodingUTF8);
            if (!data) { *outSize = sizeof v; CFRelease(v); return kAudioHardwareNoError; }
            if (dataSize < sizeof v) { CFRelease(v); return kAudioHardwareBadPropertySizeError; }
            *(CFStringRef*)data = v; *outSize = sizeof v; return kAudioHardwareNoError; }
        case kAudioDevicePropertyModelUID: { CFStringRef v = CFStringCreateWithCString(NULL, kAH_ModelUIDs[d], kCFStringEncodingUTF8);
            if (!data) { *outSize = sizeof v; CFRelease(v); return kAudioHardwareNoError; }
            if (dataSize < sizeof v) { CFRelease(v); return kAudioHardwareBadPropertySizeError; }
            *(CFStringRef*)data = v; *outSize = sizeof v; return kAudioHardwareNoError; }
        case kAudioDevicePropertyTransportType: { UInt32 v = kAudioDeviceTransportTypeVirtual; SIZE_OR_COPY(&v, sizeof v); }
        case kAudioDevicePropertyRelatedDevices: { ids[0] = kAH_DeviceID(d); SIZE_OR_COPY(ids, sizeof(AudioObjectID)); }
        case kAudioDevicePropertyClockDomain: { UInt32 v = 0; SIZE_OR_COPY(&v, sizeof v); }
        case kAudioDevicePropertyDeviceIsAlive: { UInt32 v = 1; SIZE_OR_COPY(&v, sizeof v); }
        case kAudioDevicePropertyDeviceIsRunning: { UInt32 v = atomic_load(&gIORunning[d]) > 0 ? 1 : 0; SIZE_OR_COPY(&v, sizeof v); }
        case kAudioDevicePropertyDeviceCanBeDefaultDevice: { UInt32 v = 1; SIZE_OR_COPY(&v, sizeof v); }
        case kAudioDevicePropertyDeviceCanBeDefaultSystemDevice: { UInt32 v = 1; SIZE_OR_COPY(&v, sizeof v); }
        case kAudioDevicePropertyIsHidden: { UInt32 v = 0; SIZE_OR_COPY(&v, sizeof v); }
        case kAudioDevicePropertyLatency: { UInt32 v = 0; SIZE_OR_COPY(&v, sizeof v); }
        case kAudioDevicePropertySafetyOffset: { UInt32 v = 0; SIZE_OR_COPY(&v, sizeof v); }
        case kAudioDevicePropertyNominalSampleRate: { Float64 v = kAH_SampleRate; SIZE_OR_COPY(&v, sizeof v); }
        case kAudioDevicePropertyAvailableNominalSampleRates: {
            AudioValueRange r = {kAH_SampleRate, kAH_SampleRate};
            SIZE_OR_COPY(&r, sizeof r); }
        case kAudioDevicePropertyZeroTimeStampPeriod: { UInt32 v = kAH_BufferFrames; SIZE_OR_COPY(&v, sizeof v); }
        case kAudioDevicePropertyBufferFrameSize: { UInt32 v = kAH_BufferFrames; SIZE_OR_COPY(&v, sizeof v); }
        case kAudioDevicePropertyBufferFrameSizeRange: {
            AudioValueRange r = {kAH_BufferFrames, kAH_BufferFrames};
            SIZE_OR_COPY(&r, sizeof r); }
        case kAudioDevicePropertyPreferredChannelsForStereo: { UInt32 v[2] = {1, 2}; SIZE_OR_COPY(v, sizeof v); }
        case kAudioObjectPropertyControlList: { SIZE_OR_COPY(NULL, 0); }
        case kAudioObjectPropertyOwnedObjects:
        case kAudioDevicePropertyStreams: {
            // Scope input: stream input saja; output: stream output saja; global: keduanya
            if (a->mScope == kAudioObjectPropertyScopeInput) { ids[0] = kAH_InputStreamID(d); SIZE_OR_COPY(ids, sizeof(AudioObjectID)); }
            if (a->mScope == kAudioObjectPropertyScopeOutput) { ids[0] = kAH_OutputStreamID(d); SIZE_OR_COPY(ids, sizeof(AudioObjectID)); }
            ids[0] = kAH_InputStreamID(d); ids[1] = kAH_OutputStreamID(d);
            SIZE_OR_COPY(ids, 2 * sizeof(AudioObjectID)); }
        case kAudioDevicePropertyStreamConfiguration: {
            // AudioBufferList dengan satu buffer stereo
            UInt32 size = offsetof(AudioBufferList, mBuffers) + sizeof(AudioBuffer);
            if (!data) { *outSize = size; return kAudioHardwareNoError; }
            if (dataSize < size) return kAudioHardwareBadPropertySizeError;
            AudioBufferList* list = (AudioBufferList*)data;
            list->mNumberBuffers = 1;
            list->mBuffers[0].mNumberChannels = kAH_Channels;
            list->mBuffers[0].mDataByteSize = 0;
            list->mBuffers[0].mData = NULL;
            *outSize = size;
            return kAudioHardwareNoError; }
        default: break;
        }
        return kAudioHardwareUnknownPropertyError;
    }

    // Stream input (part 1) atau output (part 2)
    Boolean isInput = part == 1;
    switch (a->mSelector) {
    case kAudioObjectPropertyBaseClass: { AudioClassID v = kAudioObjectClassID; SIZE_OR_COPY(&v, sizeof v); }
    case kAudioObjectPropertyClass: { AudioClassID v = kAudioStreamClassID; SIZE_OR_COPY(&v, sizeof v); }
    case kAudioObjectPropertyOwner: { AudioObjectID v = kAH_DeviceID(d); SIZE_OR_COPY(&v, sizeof v); }
    case kAudioObjectPropertyOwnedObjects: { SIZE_OR_COPY(NULL, 0); }
    case kAudioStreamPropertyIsActive: { UInt32 v = 1; SIZE_OR_COPY(&v, sizeof v); }
    case kAudioStreamPropertyDirection: { UInt32 v = isInput ? 1 : 0; SIZE_OR_COPY(&v, sizeof v); }
    case kAudioStreamPropertyTerminalType: { UInt32 v = kAudioStreamTerminalTypeUnknown; SIZE_OR_COPY(&v, sizeof v); }
    case kAudioStreamPropertyStartingChannel: { UInt32 v = 1; SIZE_OR_COPY(&v, sizeof v); }
    case kAudioStreamPropertyLatency: { UInt32 v = 0; SIZE_OR_COPY(&v, sizeof v); }
    case kAudioStreamPropertyVirtualFormat:
    case kAudioStreamPropertyPhysicalFormat: {
        AudioStreamBasicDescription f = streamFormat();
        SIZE_OR_COPY(&f, sizeof f); }
    case kAudioStreamPropertyAvailableVirtualFormats:
    case kAudioStreamPropertyAvailablePhysicalFormats: {
        AudioStreamRangedDescription r;
        r.mFormat = streamFormat();
        r.mSampleRateRange.mMinimum = kAH_SampleRate;
        r.mSampleRateRange.mMaximum = kAH_SampleRate;
        SIZE_OR_COPY(&r, sizeof r); }
    default: break;
    }
    return kAudioHardwareUnknownPropertyError;
}

static Boolean HasProperty(AudioServerPlugInDriverRef d, AudioObjectID obj, pid_t pid, const AudioObjectPropertyAddress* a) {
    (void)d; (void)pid;
    UInt32 size = 0;
    return getProperty(obj, a, 0, &size, NULL) == kAudioHardwareNoError;
}

static OSStatus IsPropertySettable(AudioServerPlugInDriverRef d, AudioObjectID obj, pid_t pid, const AudioObjectPropertyAddress* a, Boolean* outSettable) {
    (void)d; (void)obj; (void)pid; (void)a;
    *outSettable = false;   // tidak ada properti yang bisa diubah dari luar
    return kAudioHardwareNoError;
}

static OSStatus GetPropertyDataSize(AudioServerPlugInDriverRef d, AudioObjectID obj, pid_t pid, const AudioObjectPropertyAddress* a, UInt32 qualSize, const void* qual, UInt32* outSize) {
    (void)d; (void)pid; (void)qualSize; (void)qual;
    return getProperty(obj, a, 0, outSize, NULL);
}

static OSStatus GetPropertyData(AudioServerPlugInDriverRef d, AudioObjectID obj, pid_t pid, const AudioObjectPropertyAddress* a, UInt32 qualSize, const void* qual, UInt32 dataSize, UInt32* outSize, void* outData) {
    (void)d; (void)pid; (void)qualSize; (void)qual;
    return getProperty(obj, a, dataSize, outSize, outData);
}

static OSStatus SetPropertyData(AudioServerPlugInDriverRef d, AudioObjectID obj, pid_t pid, const AudioObjectPropertyAddress* a, UInt32 qualSize, const void* qual, UInt32 dataSize, const void* data) {
    (void)d; (void)obj; (void)pid; (void)a; (void)qualSize; (void)qual; (void)dataSize; (void)data;
    return kAudioHardwareUnknownPropertyError;
}

// ---- Clock dan IO ----

static OSStatus StartIO(AudioServerPlugInDriverRef d, AudioObjectID dev, UInt32 clientID) {
    (void)d; (void)clientID;
    int i = devOf(dev);
    if (i < 0 || partOf(dev) != 0) return kAudioHardwareBadObjectError;
    pthread_mutex_lock(&gClockMutex);
    if (atomic_load(&gIORunning[i]) == 0) {
        gAnchorHostTime[i] = mach_absolute_time();
        memset(gRing[i], 0, sizeof gRing[i]);
        atomic_store_explicit(&gWriteEnd[i], 0, memory_order_release);
    }
    atomic_fetch_add(&gIORunning[i], 1);
    pthread_mutex_unlock(&gClockMutex);
    return kAudioHardwareNoError;
}

static OSStatus StopIO(AudioServerPlugInDriverRef d, AudioObjectID dev, UInt32 clientID) {
    (void)d; (void)clientID;
    int i = devOf(dev);
    if (i < 0 || partOf(dev) != 0) return kAudioHardwareBadObjectError;
    atomic_fetch_sub(&gIORunning[i], 1);
    return kAudioHardwareNoError;
}

// Timestamp nol berdasarkan periode buffer tetap dari waktu mulai, per perangkat
static OSStatus GetZeroTimeStamp(AudioServerPlugInDriverRef d, AudioObjectID dev, UInt32 clientID, Float64* outSampleTime, UInt64* outHostTime, UInt64* outSeed) {
    (void)d; (void)clientID;
    int i = devOf(dev);
    if (i < 0) return kAudioHardwareBadObjectError;
    pthread_mutex_lock(&gClockMutex);
    UInt64 now = mach_absolute_time();
    UInt64 periods = (UInt64)((Float64)(now - gAnchorHostTime[i]) / gHostTicksPerPeriod);
    *outSampleTime = (Float64)(periods * kAH_BufferFrames);
    *outHostTime = gAnchorHostTime[i] + (UInt64)((Float64)periods * gHostTicksPerPeriod);
    *outSeed = 1;
    pthread_mutex_unlock(&gClockMutex);
    return kAudioHardwareNoError;
}

static OSStatus WillDoIOOperation(AudioServerPlugInDriverRef d, AudioObjectID dev, UInt32 clientID, UInt32 op, Boolean* outWillDo, Boolean* outWillDoInPlace) {
    (void)d; (void)dev; (void)clientID;
    *outWillDo = (op == kAudioServerPlugInIOOperationWriteMix || op == kAudioServerPlugInIOOperationReadInput);
    *outWillDoInPlace = true;
    return kAudioHardwareNoError;
}

static OSStatus BeginIOOperation(AudioServerPlugInDriverRef d, AudioObjectID dev, UInt32 clientID, UInt32 op, UInt32 bufferSize, const AudioServerPlugInIOCycleInfo* cycle) {
    (void)d; (void)dev; (void)clientID; (void)op; (void)bufferSize; (void)cycle;
    return kAudioHardwareNoError;
}

static OSStatus DoIOOperation(AudioServerPlugInDriverRef d, AudioObjectID dev, AudioObjectID stream, UInt32 clientID, UInt32 op, UInt32 bufferSize, const AudioServerPlugInIOCycleInfo* cycle, void* mainBuffer, void* secondaryBuffer) {
    (void)d; (void)dev; (void)clientID; (void)secondaryBuffer;
    int i = devOf(stream);
    if (i < 0) return kAudioHardwareBadObjectError;
    float* buf = (float*)mainBuffer;
    float* ring = gRing[i];
    if (op == kAudioServerPlugInIOOperationWriteMix && partOf(stream) == 2) {
        SInt64 start = (SInt64)cycle->mOutputTime.mSampleTime;
        for (UInt32 f = 0; f < bufferSize; f++) {
            SInt64 idx = ((start + f) % kAH_RingFrames + kAH_RingFrames) % kAH_RingFrames;
            for (int c = 0; c < kAH_Channels; c++) ring[idx * kAH_Channels + c] = buf[f * kAH_Channels + c];
        }
        // Terbitkan penanda tulis setelah data ditulis (release)
        SInt64 end = start + (SInt64)bufferSize;
        if (end > atomic_load_explicit(&gWriteEnd[i], memory_order_relaxed))
            atomic_store_explicit(&gWriteEnd[i], end, memory_order_release);
    } else if (op == kAudioServerPlugInIOOperationReadInput && partOf(stream) == 1) {
        // Baca tertinggal dua buffer aktif. Posisi yang belum ditulis dibaca sebagai nol.
        SInt64 written = atomic_load_explicit(&gWriteEnd[i], memory_order_acquire);
        SInt64 start = (SInt64)cycle->mInputTime.mSampleTime - 2 * (SInt64)bufferSize;
        for (UInt32 f = 0; f < bufferSize; f++) {
            SInt64 pos = start + (SInt64)f;
            if (pos < 0 || pos >= written) {
                for (int c = 0; c < kAH_Channels; c++) buf[f * kAH_Channels + c] = 0.0f;
                continue;
            }
            SInt64 idx = (pos % kAH_RingFrames + kAH_RingFrames) % kAH_RingFrames;
            for (int c = 0; c < kAH_Channels; c++) buf[f * kAH_Channels + c] = ring[idx * kAH_Channels + c];
        }
    }
    return kAudioHardwareNoError;
}

static OSStatus EndIOOperation(AudioServerPlugInDriverRef d, AudioObjectID dev, UInt32 clientID, UInt32 op, UInt32 bufferSize, const AudioServerPlugInIOCycleInfo* cycle) {
    (void)d; (void)dev; (void)clientID; (void)op; (void)bufferSize; (void)cycle;
    return kAudioHardwareNoError;
}
