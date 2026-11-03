// Uji ring SPSC (ring.h). Tanpa Windows, jadi bisa dijalankan di macOS: sh windows/io/tests/run.sh
#include "../src/ring.h"

#include <cassert>
#include <cstdio>
#include <thread>
#include <vector>

using ahwin::FrameRing;

static void testPushPopStereo() {
    FrameRing r;
    r.init(2, 8);
    float in[4] = {1, 2, 3, 4};            // 2 frame stereo
    assert(r.push(in, 2) == 2);
    assert(r.available() == 2);

    float out[6] = {9, 9, 9, 9, 9, 9};     // minta 3 frame, hanya 2 yang ada
    assert(r.pop(out, 3) == 2);
    assert(out[0] == 1 && out[1] == 2 && out[2] == 3 && out[3] == 4);
    assert(out[4] == 0 && out[5] == 0);   // sisanya diisi diam
    assert(r.available() == 0);
}

static void testOverflowDropsNewest() {
    FrameRing r;
    r.init(1, 4);
    float in[6] = {1, 2, 3, 4, 5, 6};
    assert(r.push(in, 6) == 4);           // kapasitas 4, sisanya ditolak
    float out[4];
    assert(r.pop(out, 4) == 4);
    assert(out[0] == 1 && out[3] == 4);
}

static void testWrapAround() {
    FrameRing r;
    r.init(1, 4);
    for (int round = 0; round < 10; round++) {
        float in[3] = {(float)(round * 3), (float)(round * 3 + 1), (float)(round * 3 + 2)};
        assert(r.push(in, 3) == 3);
        float out[3];
        assert(r.pop(out, 3) == 3);
        for (int i = 0; i < 3; i++) assert(out[i] == in[i]);
    }
}

static void testResetClears() {
    FrameRing r;
    r.init(1, 4);
    float in[2] = {7, 8};
    r.push(in, 2);
    r.reset();
    assert(r.available() == 0);
}

// Satu producer dan satu consumer di thread berbeda. Urutan nilai harus utuh, tanpa lompatan.
static void testThreadedOrdering() {
    FrameRing r;
    r.init(2, 1024);
    const int total = 200000;
    std::thread producer([&] {
        int next = 0;
        float frame[2];
        while (next < total) {
            frame[0] = (float)next;
            frame[1] = (float)next + 0.5f;
            if (r.push(frame, 1) == 1) next++;
        }
    });
    int expected = 0;
    bool ordered = true;
    float buf[2];
    while (expected < total) {
        if (r.pop(buf, 1) == 1) {
            if (buf[0] != (float)expected || buf[1] != (float)expected + 0.5f) ordered = false;
            expected++;
        }
    }
    producer.join();
    assert(ordered);
}

int main() {
    testPushPopStereo();
    testOverflowDropsNewest();
    testWrapAround();
    testResetClears();
    testThreadedOrdering();
    std::puts("OK: ring SPSC");
    return 0;
}
