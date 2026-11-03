// Backend Windows: WASAPI shared mode, event-driven.
//  - Setiap endpoint dan setiap tap proses punya satu stream float32 pada sample rate engine. Flag AUTOCONVERTPCM
//    dan SRC_DEFAULT_QUALITY membuat WASAPI menyesuaikan format bila endpoint memakai format lain.
//  - Capture fisik adalah master. Setiap paket yang masuk dari sana menjalankan satu blok engine. Stream lain
//    (virtual, tap) hanya mendorong datanya ke ring, dan setiap stream render menarik datanya dari ring sendiri.
//  - Setiap endpoint punya clock sendiri. Ring menyerap selisihnya: underrun diisi diam, overrun dibuang.
//    Akibatnya ada latensi tambahan dan kadang klik kecil saat clock tidak sama. Ini belum diuji di perangkat nyata.
#include "../include/audiohub_io.h"

#include "devices.h"  // memasang NOMINMAX dan windows.h
#include "ring.h"
#include "../../../backends/coreaudio/io/route_policy.h"
#include "../../../engine/api/audiohub_engine.h"

#include <windows.h>
#include <audioclient.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <objbase.h>

#include <wchar.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using ahwin::ComPtr;
using ahwin::FrameRing;

namespace {

constexpr int kMaxCh = 32;
constexpr uint32_t kMaxFrames = 4096;
constexpr uint32_t kRingFrames = 1u << 14;   // sekitar 340 ms pada 48 kHz
constexpr uint32_t kRecFrames = 1u << 19;
constexpr int kTapChannels = 2;

enum class Role { Physical, Virtual1, Virtual2, Virtual3, Tap };

struct Stream {
    bool isCapture = true;
    Role role = Role::Physical;
    int channels = 0;
    int chOffset = 0;                 // kanal pertama stream ini di sisi engine (input atau output)
    ComPtr<IAudioClient> client;
    ComPtr<IAudioCaptureClient> capt;
    ComPtr<IAudioRenderClient> rend;
    HANDLE event = nullptr;
    FrameRing ring;
    std::vector<float> scratch;       // interleaved, kMaxFrames * channels
    std::thread thread;
    std::atomic<uint64_t> underruns{0}, overruns{0}, missing{0};
    uint32_t pid = 0;                 // tap: PID proses
    bool isolated = false;            // tap: sesi proses sedang dibisukan oleh AudioHub
    bool prevMute = false;            // tap: status mute sesi sebelum dibisukan

    ~Stream() {
        if (event) CloseHandle(event);
    }
};

std::string hresultText(const char* msg, HRESULT hr) {
    char buf[192];
    std::snprintf(buf, sizeof buf, "%s (0x%08lX)", msg, (unsigned long)hr);
    return buf;
}

void coInit() {
    // S_FALSE atau RPC_E_CHANGED_MODE berarti COM sudah diinisialisasi di thread ini; keduanya boleh diabaikan.
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
}

uint32_t latencyFrames(IAudioClient* client, uint32_t rate) {
    REFERENCE_TIME hns = 0;
    if (FAILED(client->GetStreamLatency(&hns))) return 0;
    return (uint32_t)((double)hns * rate / 10000000.0);
}

WAVEFORMATEXTENSIBLE floatFormat(int channels, uint32_t rate) {
    WAVEFORMATEXTENSIBLE f{};
    f.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    f.Format.nChannels = (WORD)channels;
    f.Format.nSamplesPerSec = rate;
    f.Format.wBitsPerSample = 32;
    f.Format.nBlockAlign = (WORD)(channels * 4);
    f.Format.nAvgBytesPerSec = rate * channels * 4;
    f.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    f.Samples.wValidBitsPerSample = 32;
    f.dwChannelMask = channels == 1 ? SPEAKER_FRONT_CENTER
                    : channels == 2 ? (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT)
                    : 0;
    f.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    return f;
}

// Memasang event dan layanan capture/render pada client yang sudah diinisialisasi.
bool bindStream(Stream& s, const ComPtr<IAudioClient>& client, std::string& err) {
    s.client = client;
    s.event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!s.event) {
        err = "Gagal membuat event audio";
        return false;
    }
    if (FAILED(client->SetEventHandle(s.event))) {
        err = "Gagal memasang event audio";
        return false;
    }
    HRESULT hr = s.isCapture ? client->GetService(IID_PPV_ARGS(&s.capt))
                             : client->GetService(IID_PPV_ARGS(&s.rend));
    if (FAILED(hr)) {
        err = hresultText("Gagal mengambil layanan audio", hr);
        return false;
    }
    s.ring.init(s.channels, kRingFrames);
    s.scratch.assign((size_t)kMaxFrames * s.channels, 0.0f);
    return true;
}

// Membuka endpoint sebagai stream float32 pada sample rate engine. Jumlah kanal mengikuti format campuran endpoint.
bool openEndpoint(Stream& s, IMMDevice* dev, uint32_t rate, std::string& err) {
    ComPtr<IAudioClient> client;
    if (FAILED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &client))) {
        err = "Gagal mengaktifkan endpoint";
        return false;
    }
    WAVEFORMATEX* mix = nullptr;
    if (FAILED(client->GetMixFormat(&mix)) || !mix) {
        err = "Gagal membaca format endpoint";
        return false;
    }
    s.channels = mix->nChannels;
    CoTaskMemFree(mix);
    if (s.channels < 1 || s.channels > kMaxCh) {
        err = "Jumlah kanal endpoint tidak didukung";
        return false;
    }
    WAVEFORMATEXTENSIBLE fmt = floatFormat(s.channels, rate);
    DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                  AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
    HRESULT hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 0, 0,
                                    reinterpret_cast<WAVEFORMATEX*>(&fmt), nullptr);
    if (FAILED(hr)) {
        err = hresultText("Gagal membuka endpoint", hr);
        return false;
    }
    return bindStream(s, client, err);
}

// Tangkap audio satu proses lewat process loopback. Selalu stereo float32 pada sample rate engine.
bool openTap(Stream& s, uint32_t pid, uint32_t rate, std::string& err) {
    ComPtr<IAudioClient> client = ahwin::activateProcessLoopback(pid, err);
    if (!client) return false;
    s.channels = kTapChannels;
    WAVEFORMATEXTENSIBLE fmt = floatFormat(kTapChannels, rate);
    DWORD flags = AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                  AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
    HRESULT hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 0, 0,
                                    reinterpret_cast<WAVEFORMATEX*>(&fmt), nullptr);
    if (FAILED(hr)) {
        err = hresultText("Gagal membuka tangkap audio proses", hr);
        return false;
    }
    return bindStream(s, client, err);
}

}  // namespace

struct AHIO {
    // captures[0] = fisik (master), lalu virtual 1..3 yang dipakai, lalu tap. renders[0] = fisik, lalu virtual.
    std::vector<std::unique_ptr<Stream>> captures;
    std::vector<std::unique_ptr<Stream>> renders;
    audiohub::ChannelLayout layout{};
    int nIn = 0, nOut = 0;
    AHEngine* engine = nullptr;
    uint32_t sampleRate = 0;
    uint32_t bufferFrames = 0;             // buffer endpoint fisik capture, dalam frame
    uint32_t inLatency = 0, outLatency = 0;
    std::vector<float> inBuf;              // nIn * kMaxFrames, deinterleaved
    std::vector<float> outBuf;             // nOut * kMaxFrames
    std::vector<const float*> inPtr;
    std::vector<float*> outPtr;
    HANDLE stopEvent = nullptr;
    std::atomic<bool> running{false};
    std::atomic<bool> alive{true};
    std::atomic<uint64_t> callbacks{0};
    bool isolate = false;

    // Rekaman: ring mono, penulis = master, pembaca = thread penulisan berkas.
    FrameRing recRing;
    std::thread recThread;
    std::atomic<bool> recActive{false};
    std::atomic<uint64_t> recFrames{0}, recDropped{0};
    int recOutput = -1;
    FILE* recFile = nullptr;
    uint32_t recDataBytes = 0;

    ~AHIO() {
        if (stopEvent) CloseHandle(stopEvent);
    }

    // Satu blok engine. Hanya dipanggil dari thread capture master.
    void engineStep(uint32_t frames);
};

void AHIO::engineStep(uint32_t frames) {
    for (auto& up : captures) {
        Stream& s = *up;
        uint32_t got = s.ring.pop(s.scratch.data(), frames);
        if (got < frames) {
            s.underruns++;
            s.missing += frames - got;
        }
        for (int c = 0; c < s.channels; c++) {
            float* dst = inBuf.data() + (size_t)(s.chOffset + c) * kMaxFrames;
            for (uint32_t f = 0; f < frames; f++) dst[f] = s.scratch[(size_t)f * s.channels + c];
        }
    }

    ah_engine_process(engine, inPtr.data(), outPtr.data(), (int)frames);

    for (auto& up : renders) {
        Stream& s = *up;
        for (int c = 0; c < s.channels; c++) {
            const float* src = outBuf.data() + (size_t)(s.chOffset + c) * kMaxFrames;
            for (uint32_t f = 0; f < frames; f++) s.scratch[(size_t)f * s.channels + c] = src[f];
        }
        uint32_t k = s.ring.push(s.scratch.data(), frames);
        if (k < frames) s.overruns += frames - k;
    }

    if (recActive.load(std::memory_order_acquire)) {
        uint32_t k = recRing.push(outBuf.data() + (size_t)recOutput * kMaxFrames, frames);
        if (k < frames) recDropped += frames - k;
    }
    callbacks++;
}

namespace {

void captureLoop(AHIO* io, Stream* s, bool master) {
    coInit();
    HANDLE events[2] = {io->stopEvent, s->event};
    bool ok = true;
    while (ok && WaitForMultipleObjects(2, events, FALSE, INFINITE) == WAIT_OBJECT_0 + 1) {
        UINT32 packet = 0;
        if (FAILED(s->capt->GetNextPacketSize(&packet))) {
            io->alive = false;
            break;
        }
        while (packet > 0) {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            if (FAILED(s->capt->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) {
                io->alive = false;
                ok = false;
                break;
            }
            if (frames > kMaxFrames) frames = kMaxFrames;
            if (frames > 0) {
                size_t bytes = sizeof(float) * (size_t)frames * s->channels;
                if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) || !data) std::memset(s->scratch.data(), 0, bytes);
                else std::memcpy(s->scratch.data(), data, bytes);
            }
            s->capt->ReleaseBuffer(frames);
            if (frames > 0) {
                uint32_t k = s->ring.push(s->scratch.data(), frames);
                if (k < frames) s->overruns += frames - k;
                if (master) io->engineStep(frames);
            }
            if (FAILED(s->capt->GetNextPacketSize(&packet))) {
                io->alive = false;
                ok = false;
                break;
            }
        }
    }
    CoUninitialize();
}

void renderLoop(AHIO* io, Stream* s) {
    coInit();
    HANDLE events[2] = {io->stopEvent, s->event};
    while (WaitForMultipleObjects(2, events, FALSE, INFINITE) == WAIT_OBJECT_0 + 1) {
        UINT32 bufFrames = 0, padding = 0;
        if (FAILED(s->client->GetBufferSize(&bufFrames)) || FAILED(s->client->GetCurrentPadding(&padding))) {
            io->alive = false;
            break;
        }
        UINT32 avail = bufFrames > padding ? bufFrames - padding : 0;
        if (avail > kMaxFrames) avail = kMaxFrames;
        if (avail == 0) continue;
        BYTE* data = nullptr;
        if (FAILED(s->rend->GetBuffer(avail, &data)) || !data) {
            io->alive = false;
            break;
        }
        uint32_t got = s->ring.pop(reinterpret_cast<float*>(data), avail);
        if (got < avail) {
            s->underruns++;
            s->missing += avail - got;
        }
        s->rend->ReleaseBuffer(avail, 0);
    }
    CoUninitialize();
}

// Menghentikan semua thread audio. Dipakai oleh stop dan oleh start yang gagal di tengah jalan.
void shutdownThreads(AHIO* io) {
    SetEvent(io->stopEvent);
    for (auto& c : io->captures)
        if (c->thread.joinable()) c->thread.join();
    for (auto& r : io->renders)
        if (r->thread.joinable()) r->thread.join();
}

// Mengembalikan status mute sesi proses tap yang dibisukan saat start.
void restoreTapMutes(AHIO* io) {
    for (auto& c : io->captures) {
        if (c->isolated) {
            ahwin::setSessionMute(c->pid, c->prevMute, nullptr);
            c->isolated = false;
        }
    }
}

void writeWavHeader(FILE* f, uint32_t sampleRate, uint32_t dataBytes) {
    uint8_t h[44];
    auto u32 = [](uint8_t* p, uint32_t v) {
        p[0] = v & 0xff; p[1] = (v >> 8) & 0xff; p[2] = (v >> 16) & 0xff; p[3] = (v >> 24) & 0xff;
    };
    auto u16 = [](uint8_t* p, uint16_t v) { p[0] = v & 0xff; p[1] = (v >> 8) & 0xff; };
    std::memcpy(h, "RIFF", 4); u32(h + 4, 36 + dataBytes);
    std::memcpy(h + 8, "WAVE", 4);
    std::memcpy(h + 12, "fmt ", 4); u32(h + 16, 16);
    u16(h + 20, 3);                    // IEEE float
    u16(h + 22, 1);                    // mono
    u32(h + 24, sampleRate);
    u32(h + 28, sampleRate * 4);
    u16(h + 32, 4);
    u16(h + 34, 32);
    std::memcpy(h + 36, "data", 4); u32(h + 40, dataBytes);
    std::fwrite(h, 1, 44, f);
}

void recordWriter(AHIO* io) {
    std::vector<float> chunk(4096);
    while (true) {
        uint32_t avail = io->recRing.available();
        if (avail == 0) {
            if (!io->recActive.load()) break;
            Sleep(5);
            continue;
        }
        uint32_t n = std::min<uint32_t>(avail, (uint32_t)chunk.size());
        io->recRing.pop(chunk.data(), n);
        std::fwrite(chunk.data(), sizeof(float), n, io->recFile);
        io->recFrames += n;
        io->recDataBytes += n * sizeof(float);
    }
}

int copyText(const std::string& s, char* buf, int len) {
    if (!buf || len <= 0) return (int)s.size();
    int n = (int)std::min<size_t>(s.size(), (size_t)len - 1);
    std::memcpy(buf, s.data(), n);
    buf[n] = '\0';
    return n;
}

}  // namespace

// MARK: - Buka dan tutup

AHIO* ah_io_open(const AHIOOpenConfig* cfg, char* errBuf, int errLen) {
    auto fail = [&](const std::string& msg) -> AHIO* {
        if (errBuf && errLen > 0) std::snprintf(errBuf, errLen, "%s", msg.c_str());
        return nullptr;
    };
    if (!cfg || !cfg->physicalName || !cfg->physicalName[0]) return fail("Nama perangkat fisik kosong");
    if (cfg->tapCount > 0 && !cfg->tapPids) return fail("Daftar proses tangkap kosong");
    coInit();

    std::wstring physName = ahwin::toWide(cfg->physicalName);
    ComPtr<IMMDevice> physCap = ahwin::findEndpoint(eCapture, physName, nullptr);
    if (!physCap) return fail("Input fisik tidak ditemukan: " + std::string(cfg->physicalName));
    ComPtr<IMMDevice> physRen = ahwin::findEndpoint(eRender, physName, nullptr);
    if (!physRen) return fail("Output fisik tidak ditemukan: " + std::string(cfg->physicalName));

    uint32_t rate = 0;
    if (!ahwin::mixFormat(physCap.Get(), nullptr, &rate) || rate == 0) return fail("Gagal membaca format input fisik");

    std::unique_ptr<AHIO> io(new AHIO());
    io->sampleRate = rate;
    io->isolate = cfg->tapIsolate != 0;
    std::string err;

    // Endpoint fisik: capture (master) dan render.
    auto physC = std::make_unique<Stream>();
    physC->isCapture = true;
    physC->role = Role::Physical;
    if (!openEndpoint(*physC, physCap.Get(), rate, err)) return fail("Input fisik: " + err);
    auto physR = std::make_unique<Stream>();
    physR->isCapture = false;
    physR->role = Role::Physical;
    if (!openEndpoint(*physR, physRen.Get(), rate, err)) return fail("Output fisik: " + err);
    io->captures.push_back(std::move(physC));
    io->renders.push_back(std::move(physR));

    // Perangkat virtual AVHub: setiap perangkat punya endpoint capture dan render.
    const char* virtNames[3] = {cfg->virt1Name, cfg->virt2Name, cfg->virt3Name};
    const Role virtRoles[3] = {Role::Virtual1, Role::Virtual2, Role::Virtual3};
    for (int v = 0; v < 3; v++) {
        if (!virtNames[v] || !virtNames[v][0]) continue;
        std::wstring vname = ahwin::toWide(virtNames[v]);
        ComPtr<IMMDevice> vcap = ahwin::findEndpoint(eCapture, vname, nullptr);
        ComPtr<IMMDevice> vren = ahwin::findEndpoint(eRender, vname, nullptr);
        if (!vcap || !vren) return fail("Perangkat virtual tidak lengkap atau tidak ditemukan: " + std::string(virtNames[v]));
        auto c = std::make_unique<Stream>();
        c->isCapture = true;
        c->role = virtRoles[v];
        if (!openEndpoint(*c, vcap.Get(), rate, err)) return fail(std::string(virtNames[v]) + ": " + err);
        auto r = std::make_unique<Stream>();
        r->isCapture = false;
        r->role = virtRoles[v];
        if (!openEndpoint(*r, vren.Get(), rate, err)) return fail(std::string(virtNames[v]) + ": " + err);
        io->captures.push_back(std::move(c));
        io->renders.push_back(std::move(r));
    }

    // Tap aplikasi: satu stream per PID.
    for (int t = 0; t < cfg->tapCount; t++) {
        auto s = std::make_unique<Stream>();
        s->isCapture = true;
        s->role = Role::Tap;
        s->pid = cfg->tapPids[t];
        if (!openTap(*s, s->pid, rate, err)) return fail(err);
        io->captures.push_back(std::move(s));
    }

    // Offset kanal per sisi, sama dengan urutan di route_policy.h.
    int off = 0;
    for (auto& s : io->captures) { s->chOffset = off; off += s->channels; }
    io->nIn = off;
    off = 0;
    for (auto& s : io->renders) { s->chOffset = off; off += s->channels; }
    io->nOut = off;
    if (io->nIn < 1 || io->nOut < 1) return fail("Perangkat tidak punya input dan output");
    if (io->nIn > kMaxCh || io->nOut > kMaxCh) return fail("Jumlah kanal melebihi batas");

    audiohub::ChannelLayout L{};
    for (auto& s : io->captures) {
        switch (s->role) {
            case Role::Physical: L.physIn = s->channels; break;
            case Role::Virtual1: L.virt1In = s->channels; break;
            case Role::Virtual2: L.virt2In = s->channels; break;
            case Role::Virtual3: L.virt3In = s->channels; break;
            case Role::Tap: L.tapIn += s->channels; break;
        }
    }
    for (auto& s : io->renders) {
        switch (s->role) {
            case Role::Physical: L.physOut = s->channels; break;
            case Role::Virtual1: L.virt1Out = s->channels; break;
            case Role::Virtual2: L.virt2Out = s->channels; break;
            case Role::Virtual3: L.virt3Out = s->channels; break;
            case Role::Tap: break;
        }
    }
    io->layout = L;

    io->engine = ah_engine_create_rate(io->nIn, io->nOut, kMaxFrames, rate);
    if (!io->engine) return fail("Gagal membuat engine");
    io->inBuf.assign((size_t)io->nIn * kMaxFrames, 0.0f);
    io->outBuf.assign((size_t)io->nOut * kMaxFrames, 0.0f);
    io->inPtr.resize(io->nIn);
    for (int i = 0; i < io->nIn; i++) io->inPtr[i] = io->inBuf.data() + (size_t)i * kMaxFrames;
    io->outPtr.resize(io->nOut);
    for (int i = 0; i < io->nOut; i++) io->outPtr[i] = io->outBuf.data() + (size_t)i * kMaxFrames;

    io->recRing.init(1, kRecFrames);
    io->stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!io->stopEvent) return fail("Gagal membuat event berhenti");

    UINT32 bf = 0;
    if (SUCCEEDED(io->captures[0]->client->GetBufferSize(&bf))) io->bufferFrames = bf;
    io->inLatency = latencyFrames(io->captures[0]->client.Get(), rate);
    io->outLatency = latencyFrames(io->renders[0]->client.Get(), rate);

    return io.release();
}

void ah_io_close(AHIO* io) {
    if (!io) return;
    ah_io_record_stop(io);
    ah_io_stop(io);
    if (io->engine) ah_engine_destroy(io->engine);
    delete io;
}

// MARK: - Start dan stop

int ah_io_start(AHIO* io) {
    if (!io) return -1;
    if (io->running) return 0;
    ResetEvent(io->stopEvent);
    io->alive = true;

    // Ring render diisi diam dua periode dulu, dan buffer render diisi diam sebelum Start (syarat event-driven).
    uint32_t prefill = std::max<uint32_t>(io->bufferFrames * 2, 512);
    for (auto& r : io->renders) {
        r->ring.reset();
        std::vector<float> zeros((size_t)prefill * r->channels, 0.0f);
        r->ring.push(zeros.data(), prefill);
        UINT32 bf = 0;
        BYTE* d = nullptr;
        if (SUCCEEDED(r->client->GetBufferSize(&bf)) && bf > 0 && SUCCEEDED(r->rend->GetBuffer(bf, &d)) && d) {
            std::memset(d, 0, sizeof(float) * (size_t)bf * r->channels);
            r->rend->ReleaseBuffer(bf, AUDCLNT_BUFFERFLAGS_SILENT);
        }
    }
    for (auto& c : io->captures) c->ring.reset();

    for (size_t i = 0; i < io->captures.size(); i++) {
        io->captures[i]->thread = std::thread(captureLoop, io, io->captures[i].get(), i == 0);
    }
    for (auto& r : io->renders) r->thread = std::thread(renderLoop, io, r.get());

    bool ok = true;
    for (auto& c : io->captures) ok = SUCCEEDED(c->client->Start()) && ok;
    for (auto& r : io->renders) ok = SUCCEEDED(r->client->Start()) && ok;
    if (!ok) {
        shutdownThreads(io);
        for (auto& c : io->captures) c->client->Stop();
        for (auto& r : io->renders) r->client->Stop();
        return -1;
    }

    if (io->isolate) {
        for (auto& c : io->captures) {
            if (c->role != Role::Tap) continue;
            bool prev = false;
            if (ahwin::setSessionMute(c->pid, true, &prev) > 0) {
                c->isolated = true;
                c->prevMute = prev;
            }
        }
    }
    io->running = true;
    return 0;
}

void ah_io_stop(AHIO* io) {
    if (!io || !io->running) return;
    shutdownThreads(io);
    for (auto& c : io->captures) {
        c->client->Stop();
        c->client->Reset();
    }
    for (auto& r : io->renders) {
        r->client->Stop();
        r->client->Reset();
    }
    restoreTapMutes(io);
    io->running = false;
}

int ah_io_is_alive(AHIO* io) {
    return io && io->alive ? 1 : 0;
}

// MARK: - Kanal dan route

int ah_io_inputs(AHIO* io) { return io ? io->nIn : 0; }
int ah_io_outputs(AHIO* io) { return io ? io->nOut : 0; }
int ah_io_physical_inputs(AHIO* io) { return io ? io->layout.physIn : 0; }
int ah_io_physical_outputs(AHIO* io) { return io ? io->layout.physOut : 0; }

int ah_io_route_allowed(AHIO* io, int input, int output) {
    if (!io) return 0;
    return audiohub::routeAllowed(input, output, io->layout) ? 1 : 0;
}

void ah_io_channel_owner(AHIO* io, int output, int channel, int* owner, int* local) {
    if (owner) *owner = -1;
    if (local) *local = 0;
    if (!io) return;
    const audiohub::ChannelLayout& L = io->layout;
    int phys = output ? L.physOut : L.physIn;
    int v1 = output ? L.virt1Out : L.virt1In;
    int v2 = output ? L.virt2Out : L.virt2In;
    int v3 = output ? L.virt3Out : L.virt3In;
    int tap = output ? 0 : L.tapIn;
    if (channel < 0 || channel >= phys + v1 + v2 + v3 + tap) return;
    int o = audiohub::channelOwner(channel, phys, v1, v2, v3);
    if (owner) *owner = o;
    int start = o == 0 ? 0 : o == 1 ? phys : o == 2 ? phys + v1 : o == 3 ? phys + v1 + v2 : phys + v1 + v2 + v3;
    if (local) *local = channel - start + 1;
}

int ah_io_set_route(AHIO* io, int input, int output, int enabled, float sendDb) {
    if (!io) return -1;
    if (!audiohub::routeAllowed(input, output, io->layout)) return -1;
    // Route dalam perangkat virtual yang sama tidak mengirim audio (lihat route_policy.h).
    if (audiohub::routeLoops(input, output, io->layout)) enabled = 0;
    ah_engine_set_route(io->engine, input, output, enabled, sendDb);
    return 0;
}

void ah_io_set_channel(AHIO* io, int input, float gainDb, int mute) {
    if (io) ah_engine_set_channel(io->engine, input, gainDb, mute);
}

void ah_io_set_channel_pan(AHIO* io, int input, float pan) {
    if (io) ah_engine_set_channel_pan(io->engine, input, pan);
}

void ah_io_set_output(AHIO* io, int output, float gainDb, int mute) {
    if (io) ah_engine_set_output(io->engine, output, gainDb, mute);
}

void ah_io_set_input_fx(AHIO* io, int input, const AHGateParams* gate, const AHEqParams* eq, const AHCompParams* comp) {
    if (io) ah_engine_set_input_fx(io->engine, input, gate, eq, comp);
}

void ah_io_set_input_delay(AHIO* io, int input, const AHDelayParams* delay) {
    if (io) ah_engine_set_input_delay(io->engine, input, delay);
}

void ah_io_set_output_limiter(AHIO* io, int output, const AHLimiterParams* limiter) {
    if (io) ah_engine_set_output_limiter(io->engine, output, limiter);
}

void ah_io_peaks(AHIO* io, float* inPeaks, float* outPeaks) {
    if (io) ah_engine_peaks(io->engine, inPeaks, outPeaks);
}

void ah_io_stats(AHIO* io, AHIOStats* out) {
    if (!io || !out) return;
    uint64_t dropouts = 0, missing = 0;
    for (auto& c : io->captures) {
        dropouts += c->underruns + c->overruns;
        missing += c->missing;
    }
    for (auto& r : io->renders) {
        dropouts += r->underruns + r->overruns;
        missing += r->missing;
    }
    out->callbacks = io->callbacks;
    out->dropouts = dropouts;
    out->missingFrames = missing;
    out->inputs = io->nIn;
    out->outputs = io->nOut;
    out->bufferFrames = io->bufferFrames;
    out->sampleRate = io->sampleRate;
    out->inputLatencyFrames = io->inLatency;
    out->outputLatencyFrames = io->outLatency;
}

// MARK: - Rekaman

int ah_io_record_start(AHIO* io, int output, const char* utf8Path) {
    if (!io || !utf8Path || output < 0 || output >= io->nOut) return -1;
    if (io->recActive.load()) return -1;
    io->recFile = _wfopen(ahwin::toWide(utf8Path).c_str(), L"wb");
    if (!io->recFile) return -1;
    writeWavHeader(io->recFile, io->sampleRate, 0);
    io->recDataBytes = 0;
    io->recFrames = 0;
    io->recDropped = 0;
    io->recOutput = output;
    io->recActive.store(true, std::memory_order_release);
    io->recThread = std::thread(recordWriter, io);
    return 0;
}

void ah_io_record_stop(AHIO* io) {
    if (!io || !io->recActive.load()) return;
    io->recActive.store(false, std::memory_order_release);
    if (io->recThread.joinable()) io->recThread.join();
    if (io->recFile) {
        // Tulis ulang header dengan ukuran data yang sebenarnya.
        std::fseek(io->recFile, 0, SEEK_SET);
        writeWavHeader(io->recFile, io->sampleRate, io->recDataBytes);
        std::fclose(io->recFile);
        io->recFile = nullptr;
    }
}

void ah_io_record_stats(AHIO* io, uint64_t* written, uint64_t* dropped) {
    if (!io) return;
    if (written) *written = io->recFrames.load();
    if (dropped) *dropped = io->recDropped.load();
}

// MARK: - Daftar perangkat dan proses

int ah_io_list_devices(char* buf, int len) {
    coInit();
    return copyText(ahwin::listEndpointsText(), buf, len);
}

int ah_io_list_processes(char* buf, int len) {
    coInit();
    return copyText(ahwin::listProcessesText(), buf, len);
}
