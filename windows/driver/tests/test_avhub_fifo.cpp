// Unit test for the AVHub loopback FIFO (src/Source/Main/avhub_loopback.cpp).
// Runs the real driver code against a simple reference model with random operations.
// Build and run: sh windows/driver/tests/run.sh
#include "common.h"            // tests/shim (first on the include path)
#include "avhub_loopback.h"    // src/Source/Inc

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <random>
#include <vector>

namespace {

// Reference model with the same rules as the driver (see avhub_loopback.cpp).
struct Model {
    std::deque<uint8_t> q;
    unsigned long cap;
    unsigned long align = 0;

    explicit Model(unsigned long capacity) : cap(capacity) {}

    void write(const uint8_t* s, unsigned long n, unsigned long a) {
        if (n == 0 || a == 0) return;
        if (align != a) { q.clear(); align = a; }
        if (n > cap) { s += n - cap; n = cap; }
        unsigned long freeBytes = cap - (unsigned long)q.size();
        if (n > freeBytes) {
            unsigned long drop = n - freeBytes;
            drop = ((drop + a - 1) / a) * a;
            if (drop > q.size()) drop = (unsigned long)q.size();
            q.erase(q.begin(), q.begin() + drop);
        }
        q.insert(q.end(), s, s + n);
    }

    unsigned long read(uint8_t* d, unsigned long n, unsigned long a) {
        unsigned long c = 0;
        if (n > 0 && align == a && !q.empty()) {
            c = n < q.size() ? n : (unsigned long)q.size();
            for (unsigned long i = 0; i < c; i++) d[i] = q[i];
            q.erase(q.begin(), q.begin() + c);
        }
        for (unsigned long i = c; i < n; i++) d[i] = 0;
        return c;
    }
};

void checkEmptyAndSilence() {
    AVHUB_FIFO f;
    assert(AvhubFifoInit(&f, 64) == STATUS_SUCCESS);
    uint8_t out[16];
    std::memset(out, 0xAA, sizeof out);
    assert(AvhubFifoRead(&f, out, 16, 4) == 0);            // empty -> silence
    for (uint8_t b : out) assert(b == 0);
    AvhubFifoFree(&f);
}

void checkRoundTrip() {
    AVHUB_FIFO f;
    assert(AvhubFifoInit(&f, 64) == STATUS_SUCCESS);
    uint8_t in[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    AvhubFifoWrite(&f, in, 8, 4);
    uint8_t out[8] = {0};
    assert(AvhubFifoRead(&f, out, 8, 4) == 8);
    for (int i = 0; i < 8; i++) assert(out[i] == in[i]);
    AvhubFifoFree(&f);
}

void checkFormatMismatchIsSilence() {
    AVHUB_FIFO f;
    assert(AvhubFifoInit(&f, 64) == STATUS_SUCCESS);
    uint8_t in[8] = {9, 9, 9, 9, 9, 9, 9, 9};
    AvhubFifoWrite(&f, in, 8, 4);                          // 16-bit stereo
    uint8_t out[8];
    std::memset(out, 0xEE, sizeof out);
    assert(AvhubFifoRead(&f, out, 8, 8) == 0);             // reader expects 32-bit stereo
    for (uint8_t b : out) assert(b == 0);
    assert(AvhubFifoRead(&f, out, 8, 4) == 8);            // audio was not consumed
    AvhubFifoFree(&f);
}

void checkOverflowKeepsNewest() {
    AVHUB_FIFO f;
    assert(AvhubFifoInit(&f, 16) == STATUS_SUCCESS);
    uint8_t in[24];
    for (int i = 0; i < 24; i++) in[i] = (uint8_t)i;
    AvhubFifoWrite(&f, in, 24, 4);                         // larger than capacity
    uint8_t out[16];
    assert(AvhubFifoRead(&f, out, 16, 4) == 16);
    for (int i = 0; i < 16; i++) assert(out[i] == (uint8_t)(i + 8));  // newest 16 bytes
    AvhubFifoFree(&f);
}

void checkRandomAgainstModel(unsigned long capacity, unsigned seed, int steps) {
    AVHUB_FIFO f;
    assert(AvhubFifoInit(&f, capacity) == STATUS_SUCCESS);
    Model m(capacity);
    std::mt19937 rng(seed);
    std::vector<uint8_t> src(capacity * 2 + 64), got(capacity + 64), want(capacity + 64);
    uint8_t next = 0;

    for (int step = 0; step < steps; step++) {
        // Mostly 16-bit stereo (align 4); sometimes a format change (align 8).
        unsigned long align = (rng() % 20 == 0) ? 8 : 4;
        if (rng() % 2 == 0) {
            unsigned long n = rng() % (capacity * 2 + 1);
            for (unsigned long i = 0; i < n; i++) src[i] = next++;
            AvhubFifoWrite(&f, src.data(), n, align);
            m.write(src.data(), n, align);
        } else {
            unsigned long n = rng() % (capacity + 9);
            std::memset(got.data(), 0xCC, got.size());
            std::memset(want.data(), 0xCC, want.size());
            unsigned long c1 = AvhubFifoRead(&f, got.data(), n, align);
            unsigned long c2 = m.read(want.data(), n, align);
            assert(c1 == c2);
            for (unsigned long i = 0; i < n; i++) assert(got[i] == want[i]);
        }
        assert(f.Count == m.q.size());
        assert(f.Count <= capacity);
    }
    AvhubFifoFree(&f);
}

void checkFrameAlignedReads() {
    // With frame-aligned writes and reads, every read returns whole frames.
    AVHUB_FIFO f;
    assert(AvhubFifoInit(&f, 64) == STATUS_SUCCESS);
    std::mt19937 rng(7);
    uint8_t buf[64];
    for (int step = 0; step < 5000; step++) {
        unsigned long frames = 1 + rng() % 8;
        AvhubFifoWrite(&f, buf, frames * 4, 4);
        unsigned long c = AvhubFifoRead(&f, buf, (1 + rng() % 8) * 4, 4);
        assert(c % 4 == 0);
    }
    AvhubFifoFree(&f);
}

}  // namespace

int main() {
    checkEmptyAndSilence();
    checkRoundTrip();
    checkFormatMismatchIsSilence();
    checkOverflowKeepsNewest();
    checkFrameAlignedReads();
    checkRandomAgainstModel(64, 1, 200000);
    checkRandomAgainstModel(AVHUB_FIFO_CAPACITY_BYTES, 2, 200000);
    std::puts("OK: AVHub loopback FIFO");
    return 0;
}
