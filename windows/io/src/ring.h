// Ring buffer SPSC untuk sampel float interleaved, dihitung dalam frame. Satu thread menulis (push),
// satu thread membaca (pop). Dipakai antara thread WASAPI dan thread engine.
#ifndef AUDIOHUB_FRAME_RING_H
#define AUDIOHUB_FRAME_RING_H

#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>

namespace ahwin {

class FrameRing {
public:
    void init(int channels, uint32_t capacityFrames) {
        ch_ = channels;
        cap_ = capacityFrames;
        buf_.assign((size_t)cap_ * ch_, 0.0f);
        r_.store(0);
        w_.store(0);
    }

    // Mengosongkan ring. Hanya boleh dipanggil saat tidak ada producer atau consumer yang berjalan.
    void reset() {
        r_.store(0);
        w_.store(0);
    }

    uint32_t available() const {
        return (uint32_t)(w_.load(std::memory_order_acquire) - r_.load(std::memory_order_acquire));
    }

    // Menulis hingga n frame dari src. Mengembalikan jumlah frame yang berhasil ditulis.
    uint32_t push(const float* src, uint32_t n) {
        uint64_t w = w_.load(std::memory_order_relaxed);
        uint64_t r = r_.load(std::memory_order_acquire);
        uint32_t freeFrames = cap_ - (uint32_t)(w - r);
        uint32_t k = n < freeFrames ? n : freeFrames;
        for (uint32_t i = 0; i < k; i++) {
            std::memcpy(&buf_[(size_t)((w + i) % cap_) * ch_], src + (size_t)i * ch_, sizeof(float) * ch_);
        }
        w_.store(w + k, std::memory_order_release);
        return k;
    }

    // Membaca n frame ke dst. Bagian yang belum tersedia diisi nol. Mengembalikan jumlah frame nyata.
    uint32_t pop(float* dst, uint32_t n) {
        uint64_t r = r_.load(std::memory_order_relaxed);
        uint64_t w = w_.load(std::memory_order_acquire);
        uint32_t avail = (uint32_t)(w - r);
        uint32_t k = n < avail ? n : avail;
        for (uint32_t i = 0; i < k; i++) {
            std::memcpy(dst + (size_t)i * ch_, &buf_[(size_t)((r + i) % cap_) * ch_], sizeof(float) * ch_);
        }
        if (k < n) std::memset(dst + (size_t)k * ch_, 0, sizeof(float) * ch_ * (n - k));
        r_.store(r + k, std::memory_order_release);
        return k;
    }

private:
    int ch_ = 1;
    uint32_t cap_ = 1;
    std::vector<float> buf_;
    std::atomic<uint64_t> r_{0};
    std::atomic<uint64_t> w_{0};
};

}  // namespace ahwin

#endif
