// Uji aturan route virtual → virtual (tanpa CoreAudio).
#include "../../backends/coreaudio/io/route_policy.h"

#include <cstdio>

using audiohub::ChannelLayout;
using audiohub::channelOwner;
using audiohub::routeAllowed;

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("GAGAL %d: %s\n", __LINE__, #c); fails++; } } while (0)

int main() {
    // Pemilik kanal: fisik 2, virtual 1 punya 2 kanal, virtual 2 sisanya
    CHECK(channelOwner(0, 2, 2) == 0);
    CHECK(channelOwner(1, 2, 2) == 0);
    CHECK(channelOwner(2, 2, 2) == 1);
    CHECK(channelOwner(3, 2, 2) == 1);
    CHECK(channelOwner(4, 2, 2) == 2);
    CHECK(channelOwner(5, 2, 2) == 2);

    // Satu perangkat virtual (satu saja): PS22 stereo + perangkat virtual stereo
    ChannelLayout one{2, 2, 0, 2, 2, 0};
    CHECK(routeAllowed(0, 0, one));   // fisik → fisik
    CHECK(routeAllowed(0, 2, one));   // fisik → virtual out (mic ke OBS)
    CHECK(routeAllowed(2, 0, one));   // virtual in → fisik (dengar DAW)
    CHECK(!routeAllowed(2, 2, one));  // virtual in → virtual out, perangkat sama: loop
    CHECK(!routeAllowed(3, 3, one));
    CHECK(!routeAllowed(2, 3, one));
    CHECK(!routeAllowed(3, 2, one));

    // Dua perangkat virtual: V1 (2 in, 2 out) dan V2 (2 in, 2 out)
    ChannelLayout two{2, 2, 2, 2, 2, 2};
    CHECK(routeAllowed(2, 4, two));   // V1 in → V2 out
    CHECK(routeAllowed(4, 2, two));   // V2 in → V1 out
    CHECK(routeAllowed(3, 5, two));
    CHECK(!routeAllowed(2, 2, two));  // V1 in → V1 out: loop
    CHECK(!routeAllowed(3, 3, two));
    CHECK(!routeAllowed(4, 4, two));  // V2 in → V2 out: loop
    CHECK(!routeAllowed(5, 5, two));
    CHECK(routeAllowed(2, 0, two));   // virtual → fisik tetap boleh
    CHECK(routeAllowed(0, 4, two));   // fisik → virtual tetap boleh

    // Perangkat V1 hanya punya input (tanpa output): kanal output 2 milik V2
    ChannelLayout inOnly{2, 2, 0, 2, 0, 2};
    CHECK(routeAllowed(2, 2, inOnly));   // V1 in → V2 out
    CHECK(!routeAllowed(4, 2, inOnly));  // input 4 tidak ada (V2 tanpa input): ditolak indeks

    // Hanya V2 yang dipasang (V1 tidak ada): semua virtual adalah milik V2
    ChannelLayout onlyV2{2, 0, 2, 2, 0, 2};
    CHECK(!routeAllowed(2, 2, onlyV2));  // input V2 → output V2: loop
    CHECK(!routeAllowed(3, 3, onlyV2));
    CHECK(routeAllowed(2, 0, onlyV2));

    // Indeks di luar jumlah kanal selalu ditolak
    CHECK(!routeAllowed(-1, 0, two));
    CHECK(!routeAllowed(0, -1, two));
    CHECK(!routeAllowed(6, 0, two));
    CHECK(!routeAllowed(0, 6, two));
    CHECK(!routeAllowed(4, 0, one));

    if (fails == 0) std::printf("route policy: semua lulus\n");
    else std::printf("route policy: %d gagal\n", fails);
    return fails == 0 ? 0 : 1;
}
