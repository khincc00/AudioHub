// Memuat bundle hasil build lewat CFPlugIn, sama seperti coreaudiod memuat driver HAL.
// Memeriksa Info.plist (CFPlugInTypes/CFPlugInFactories) dan simbol factory yang diekspor.
//
// Pemakaian: plugin_load_test <path ke AVHubDriver.driver>

#include <CoreAudio/AudioServerPlugIn.h>
#include <CoreFoundation/CoreFoundation.h>
#include <CoreFoundation/CFPlugInCOM.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "Pemakaian: %s <bundle.driver>\n", argv[0]);
        return 2;
    }
    int failures = 0;

    CFURLRef url = CFURLCreateFromFileSystemRepresentation(kCFAllocatorDefault,
                       (const UInt8 *)argv[1], (CFIndex)strlen(argv[1]), true);
    CFPlugInRef plugin = CFPlugInCreate(kCFAllocatorDefault, url);
    CFRelease(url);
    if (!plugin) { printf("  FAIL  CFPlugInCreate gagal\n"); return 1; }
    printf("  PASS  CFPlugInCreate berhasil\n");

    CFArrayRef factories = CFPlugInFindFactoriesForPlugInType(kAudioServerPlugInTypeUUID);
    CFIndex nFactories = factories ? CFArrayGetCount(factories) : 0;
    if (nFactories == 1) {
        printf("  PASS  Ditemukan 1 factory untuk tipe AudioServerPlugIn\n");
    } else {
        printf("  FAIL  Factory yang ditemukan: %ld (harus 1)\n", (long)nFactories);
        failures++;
    }

    if (nFactories >= 1) {
        CFUUIDRef factoryID = (CFUUIDRef)CFArrayGetValueAtIndex(factories, 0);
        IUnknownVTbl **unk = (IUnknownVTbl **)CFPlugInInstanceCreate(kCFAllocatorDefault, factoryID, kAudioServerPlugInTypeUUID);
        if (!unk) {
            printf("  FAIL  CFPlugInInstanceCreate gagal (simbol factory tidak ditemukan?)\n");
            failures++;
        } else {
            printf("  PASS  Instance dibuat dari factory\n");
            LPVOID iface = NULL;
            CFUUIDBytes drvUUID = CFUUIDGetUUIDBytes(kAudioServerPlugInDriverInterfaceUUID);
            HRESULT hr = (*unk)->QueryInterface(unk, drvUUID, &iface);
            if (hr == S_OK && iface) {
                printf("  PASS  QueryInterface ke AudioServerPlugInDriverInterface berhasil\n");
            } else {
                printf("  FAIL  QueryInterface gagal (hr=0x%x)\n", (unsigned)hr);
                failures++;
            }
            (*unk)->Release(unk);
        }
    }

    if (factories) CFRelease(factories);
    CFRelease(plugin);
    printf("\n%s\n", failures == 0 ? "SEMUA LULUS" : "ADA YANG GAGAL");
    return failures == 0 ? 0 : 1;
}
