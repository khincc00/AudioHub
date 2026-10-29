// Uji aturan route virtual → virtual (tanpa CoreAudio).
#include "../../backends/coreaudio/io/route_policy.h"

#include <cstdio>

using audiohub::ChannelLayout;
using audiohub::channelOwner;
using audiohub::routeAllowed;
using audiohub::routeLoops;

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("GAGAL %d: %s\n", __LINE__, #c); fails++; } } while (0)

int main() {
    // Pemilik kanal: fisik 2, virtual 1 2 kanal, virtual 2 2 kanal, virtual 3 2 kanal, lalu tap
    CHECK(channelOwner(0, 2, 2, 2, 2) == 0);
    CHECK(channelOwner(1, 2, 2, 2, 2) == 0);
    CHECK(channelOwner(2, 2, 2, 2, 2) == 1);
    CHECK(channelOwner(3, 2, 2, 2, 2) == 1);
    CHECK(channelOwner(4, 2, 2, 2, 2) == 2);
    CHECK(channelOwner(5, 2, 2, 2, 2) == 2);
    CHECK(channelOwner(6, 2, 2, 2, 2) == 3);
    CHECK(channelOwner(7, 2, 2, 2, 2) == 3);
    CHECK(channelOwner(8, 2, 2, 2, 2) == 4);  // kanal tap aplikasi
    // Tanpa virtual 2 dan 3: kanal setelah virtual 1 langsung milik tap
    CHECK(channelOwner(4, 2, 2, 0, 0) == 4);

    // Satu perangkat virtual: PS22 stereo + perangkat virtual stereo
    ChannelLayout one{2, 2, 0, 0, 2, 2, 0, 0};
    CHECK(routeAllowed(0, 0, one));   // fisik → fisik
    CHECK(routeAllowed(0, 2, one));   // fisik → virtual out (mic ke OBS)
    CHECK(routeAllowed(2, 0, one));   // virtual in → fisik (dengar DAW)
    CHECK(routeAllowed(2, 2, one));   // virtual in → virtual out, perangkat sama: diizinkan, tapi loop
    CHECK(!routeLoops(0, 2, one));    // fisik → virtual bukan loop
    CHECK(!routeLoops(2, 0, one));    // virtual → fisik bukan loop
    CHECK(routeLoops(2, 2, one));
    CHECK(routeLoops(3, 3, one));
    CHECK(routeLoops(2, 3, one));
    CHECK(routeLoops(3, 2, one));

    // Dua perangkat virtual: V1 (2 in, 2 out) dan V2 (2 in, 2 out)
    ChannelLayout two{2, 2, 2, 0, 2, 2, 2, 0};
    CHECK(routeAllowed(2, 4, two) && !routeLoops(2, 4, two));  // V1 in → V2 out: antar perangkat, bukan loop
    CHECK(routeAllowed(4, 2, two) && !routeLoops(4, 2, two));  // V2 in → V1 out
    CHECK(routeAllowed(3, 5, two) && !routeLoops(3, 5, two));
    CHECK(routeLoops(2, 2, two));     // V1 in → V1 out: loop
    CHECK(routeLoops(3, 3, two));
    CHECK(routeLoops(4, 4, two));     // V2 in → V2 out: loop
    CHECK(routeLoops(5, 5, two));
    CHECK(routeAllowed(2, 0, two) && !routeLoops(2, 0, two));  // virtual → fisik tetap bukan loop
    CHECK(routeAllowed(0, 4, two) && !routeLoops(0, 4, two));  // fisik → virtual tetap bukan loop

    // Tiga perangkat virtual: V1, V2, dan Loopback (V3), masing-masing 2 in dan 2 out
    ChannelLayout three{2, 2, 2, 2, 2, 2, 2, 2};
    CHECK(routeAllowed(6, 2, three) && !routeLoops(6, 2, three));  // Loopback in → V1 out
    CHECK(routeAllowed(6, 4, three) && !routeLoops(6, 4, three));  // Loopback in → V2 out
    CHECK(routeAllowed(2, 6, three) && !routeLoops(2, 6, three));  // V1 in → Loopback out
    CHECK(routeAllowed(4, 6, three) && !routeLoops(4, 6, three));  // V2 in → Loopback out
    CHECK(routeLoops(6, 6, three));   // Loopback in → Loopback out: loop
    CHECK(routeLoops(7, 7, three));
    CHECK(routeLoops(6, 7, three));
    CHECK(routeAllowed(6, 0, three) && !routeLoops(6, 0, three));
    CHECK(routeAllowed(0, 6, three) && !routeLoops(0, 6, three));
    CHECK(!routeAllowed(8, 0, three)); // input 8 tidak ada

    // Perangkat V1 hanya punya input (tanpa output): kanal output 2 milik V2
    ChannelLayout inOnly{2, 2, 0, 0, 2, 0, 2, 0};
    CHECK(routeAllowed(2, 2, inOnly) && !routeLoops(2, 2, inOnly));  // V1 in → V2 out
    CHECK(!routeAllowed(4, 2, inOnly));  // input 4 tidak ada (V2 tanpa input): ditolak indeks

    // Tap aplikasi (input saja): dua kanal tap setelah tiga virtual
    ChannelLayout tap{2, 2, 2, 2, 2, 2, 2, 0, 2};
    CHECK(routeAllowed(8, 2, tap) && !routeLoops(8, 2, tap));   // tap → virtual out 1: bukan loop
    CHECK(routeAllowed(9, 0, tap) && !routeLoops(9, 0, tap));   // tap → fisik
    CHECK(!routeAllowed(10, 0, tap));                           // input 10 tidak ada
    CHECK(!routeAllowed(0, 10, tap));

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
