#include "../AudioHubDriver.c"
#include <stdio.h>
#include <string.h>
static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("GAGAL %d: %s\n", __LINE__, #c); fails++; } } while (0)

static void readCFString(AudioObjectID obj, AudioObjectPropertySelector sel, char* out, int n) {
    AudioObjectPropertyAddress a = {sel, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    CFStringRef s = NULL; UInt32 sz = 0;
    GetPropertyData(gDriverRef, obj, 0, &a, 0, NULL, sizeof s, &sz, &s);
    CFStringGetCString(s, out, n, kCFStringEncodingUTF8);
    CFRelease(s);
}

int main(void) {
    Initialize(gDriverRef, NULL);
    // 1. Daftar perangkat
    AudioObjectPropertyAddress lst = {kAudioPlugInPropertyDeviceList, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    AudioObjectID ids[4] = {0}; UInt32 sz = 0;
    GetPropertyData(gDriverRef, kAudioObjectPlugInObject, 0, &lst, 0, NULL, sizeof ids, &sz, ids);
    CHECK(sz == 2 * sizeof(AudioObjectID));
    CHECK(ids[0] == 2 && ids[1] == 5);
    printf("perangkat: %u, %u\n", ids[0], ids[1]);

    // 2. Nama dan UID berbeda
    char n0[64], n1[64], u0[64], u1[64];
    readCFString(2, kAudioObjectPropertyName, n0, 64);
    readCFString(5, kAudioObjectPropertyName, n1, 64);
    readCFString(2, kAudioDevicePropertyDeviceUID, u0, 64);
    readCFString(5, kAudioDevicePropertyDeviceUID, u1, 64);
    printf("nama: '%s' / '%s'\n", n0, n1);
    printf("uid : '%s' / '%s'\n", u0, u1);
    CHECK(strcmp(n0, "AudioHub Virtual") == 0 && strcmp(n1, "AudioHub Virtual 2") == 0);
    CHECK(strcmp(u0, "com.audiohub.virtual.device") == 0 && strcmp(u1, "com.audiohub.virtual.device2") == 0);

    // 3. Stream per perangkat
    AudioObjectPropertyAddress own = {kAudioObjectPropertyOwnedObjects, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    AudioObjectID st[4] = {0};
    GetPropertyData(gDriverRef, 5, 0, &own, 0, NULL, sizeof st, &sz, st);
    CHECK(st[0] == 6 && st[1] == 7);
    AudioObjectPropertyAddress owner = {kAudioObjectPropertyOwner, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    AudioObjectID own7 = 0;
    GetPropertyData(gDriverRef, 7, 0, &owner, 0, NULL, sizeof own7, &sz, &own7);
    CHECK(own7 == 5);

    // 4. IO terpisah: tulis output perangkat 2, baca input perangkat 2 (harus sama),
    //    baca input perangkat 5 (harus nol).
    StartIO(gDriverRef, 2, 1);
    StartIO(gDriverRef, 5, 2);
    AudioServerPlugInIOCycleInfo ci; memset(&ci, 0, sizeof ci);
    float wr[512 * 2], rd0[512 * 2], rd1[512 * 2];
    int bad0 = 0, bad1 = 0;
    for (int cyc = 0; cyc < 40; cyc++) {
        Float64 T = (Float64)cyc * 512;
        for (UInt32 i = 0; i < 512 * 2; i++) wr[i] = (float)(cyc * 1000 + i) * 1e-6f;
        ci.mOutputTime.mSampleTime = T;
        DoIOOperation(gDriverRef, 2, 4, 1, kAudioServerPlugInIOOperationWriteMix, 512, &ci, wr, NULL);
        if (cyc >= 2) {
            ci.mInputTime.mSampleTime = T;   // dibaca mundur 2 buffer di dalam driver
            DoIOOperation(gDriverRef, 2, 3, 1, kAudioServerPlugInIOOperationReadInput, 512, &ci, rd0, NULL);
            DoIOOperation(gDriverRef, 5, 6, 2, kAudioServerPlugInIOOperationReadInput, 512, &ci, rd1, NULL);
            int cycRead = (int)((T - 1024) / 512);
            for (UInt32 i = 0; i < 512 * 2; i++) {
                float want = (float)(cycRead * 1000 + i) * 1e-6f;
                if (rd0[i] != want) { bad0++; break; }
            }
            for (UInt32 i = 0; i < 512 * 2; i++) if (rd1[i] != 0.0f) { bad1++; break; }
        }
    }
    StopIO(gDriverRef, 2, 1);
    StopIO(gDriverRef, 5, 2);
    CHECK(bad0 == 0);
    CHECK(bad1 == 0);
    printf("IO perangkat 2: %s, perangkat 5 terpisah (nol): %s\n", bad0 == 0 ? "cocok" : "GAGAL", bad1 == 0 ? "ya" : "GAGAL");

    // 5. Running per perangkat
    UInt32 run2 = 0, run5 = 0; AudioObjectPropertyAddress rn = {kAudioDevicePropertyDeviceIsRunning, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    GetPropertyData(gDriverRef, 2, 0, &rn, 0, NULL, 4, &sz, &run2);
    GetPropertyData(gDriverRef, 5, 0, &rn, 0, NULL, 4, &sz, &run5);
    CHECK(run2 == 0 && run5 == 0);

    printf("%s (%d gagal)\n", fails == 0 ? "semua uji lulus" : "ADA KEGAGALAN", fails);
    return fails != 0;
}
