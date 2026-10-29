// Uji unit Tahap 1: gain, mute, ramp, send, bus, mix-minus, siklus, meter.
#include "../../engine/graph/graph.h"

#include <cmath>
#include <cstdio>
#include <vector>

using audiohub::Graph;
using Block = std::vector<float>;

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                               \
    do {                                                                          \
        checks++;                                                                 \
        if (!(cond)) {                                                            \
            std::printf("GAGAL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
            failures++;                                                           \
        }                                                                         \
    } while (0)

static bool near(float a, float b, float tol = 1e-3f) { return std::fabs(a - b) <= tol; }

// Menjalankan satu blok dengan input konstan per channel, mengembalikan output per output.
static std::vector<Block> run(Graph& g, const std::vector<float>& inValues, int frames, int outCount) {
    std::vector<Block> in;
    for (float v : inValues) in.push_back(Block(frames, v));
    std::vector<const float*> inPtr;
    for (auto& b : in) inPtr.push_back(b.data());

    std::vector<Block> out(outCount, Block(frames, 0.0f));
    std::vector<float*> outPtr;
    for (auto& b : out) outPtr.push_back(b.data());

    g.process(inPtr.data(), outPtr.data(), frames);
    return out;
}

static void testGainChannel() {
    Graph g;
    int in = g.addChannel("in");
    g.addOutput("out");
    g.connect(in, 1);
    g.setNodeGain(in, -6.0206f);
    g.prepare(256);
    run(g, {1.0f}, 64, 1);                       // blok pertama: ramp
    auto out = run(g, {1.0f}, 64, 1);
    CHECK(near(out[0][63], 0.5f));
}

static void testMuteSilences() {
    Graph g;
    int in = g.addChannel("in");
    g.addOutput("out");
    g.connect(in, 1);
    g.setNodeMute(in, true);
    g.prepare(256);
    run(g, {1.0f}, 64, 1);
    auto out = run(g, {1.0f}, 64, 1);
    float maxAbs = 0.0f;
    for (float v : out[0]) maxAbs = std::fmax(maxAbs, std::fabs(v));
    CHECK(maxAbs < 1e-6f);
}

static void testRampIsLinear() {
    Graph g;
    int in = g.addChannel("in");
    g.addOutput("out");
    g.connect(in, 1);
    g.setNodeMute(in, true);                     // gain 1 → 0 dalam satu blok
    g.prepare(256);
    auto out = run(g, {1.0f}, 64, 1);
    CHECK(near(out[0][0], 1.0f - 1.0f / 64.0f, 1e-4f));
    CHECK(out[0][63] < 1e-6f);
    bool monotonic = true;
    for (int i = 1; i < 64; i++) if (out[0][i] > out[0][i - 1] + 1e-7f) monotonic = false;
    CHECK(monotonic);
}

static void testSendLevel() {
    Graph g;
    int in = g.addChannel("in");
    g.addOutput("out");
    g.connect(in, 1, -20.0f);                    // send −20 dB = 0,1
    g.prepare(256);
    run(g, {1.0f}, 64, 1);
    auto out = run(g, {1.0f}, 64, 1);
    CHECK(near(out[0][63], 0.1f));
}

static void testDisabledRouteSilent() {
    Graph g;
    int in = g.addChannel("in");
    g.addOutput("out");
    g.connect(in, 1, 0.0f, false);
    g.prepare(256);
    auto out = run(g, {1.0f}, 64, 1);
    float maxAbs = 0.0f;
    for (float v : out[0]) maxAbs = std::fmax(maxAbs, std::fabs(v));
    CHECK(maxAbs < 1e-6f);
}

static void testChainOfBuses() {
    Graph g;
    int in = g.addChannel("in");
    int busA = g.addBus("A");
    int busB = g.addBus("B");
    int out = g.addOutput("out");
    g.connect(in, busA);
    g.connect(busA, busB, -6.0206f);
    g.connect(busB, out, -6.0206f);
    g.prepare(256);
    run(g, {1.0f}, 64, 1);
    auto o = run(g, {1.0f}, 64, 1);
    CHECK(near(o[0][63], 0.25f));
}

static void testBusGain() {
    Graph g;
    int in = g.addChannel("in");
    int bus = g.addBus("bus");
    int out = g.addOutput("out");
    g.connect(in, bus);
    g.connect(bus, out);
    g.setNodeGain(bus, -6.0206f);
    g.prepare(256);
    run(g, {1.0f}, 64, 1);
    auto o = run(g, {1.0f}, 64, 1);
    CHECK(near(o[0][63], 0.5f));
}

// Voice, Desktop, Communication → Monitor (semua) dan Virtual Mic (tanpa Communication)
static void testMixMinus() {
    Graph g;
    int voice = g.addChannel("voice");
    int desktop = g.addChannel("desktop");
    int comm = g.addChannel("comm");
    int monitor = g.addBus("monitor");
    int vmic = g.addBus("vmic");
    int monitorOut = g.addOutput("monitor-out");
    int vmicOut = g.addOutput("vmic-out");
    g.connect(voice, monitor);
    g.connect(desktop, monitor);
    g.connect(comm, monitor);
    g.connect(voice, vmic);
    g.connect(desktop, vmic);
    g.connect(monitor, monitorOut);
    g.connect(vmic, vmicOut);
    g.prepare(256);

    run(g, {0.1f, 0.2f, 0.4f}, 64, 2);
    auto o = run(g, {0.1f, 0.2f, 0.4f}, 64, 2);
    CHECK(near(o[0][63], 0.7f));                 // monitor: 0,1 + 0,2 + 0,4
    CHECK(near(o[1][63], 0.3f));                 // vmic: 0,1 + 0,2, tanpa comm

    auto p = run(g, {0.1f, 0.2f, 0.9f}, 64, 2);
    CHECK(near(p[1][63], 0.3f));                 // mengubah comm tidak memengaruhi vmic
}

static void testCycleRejected() {
    Graph g;
    int a = g.addBus("A");
    int b = g.addBus("B");
    g.connect(a, b);
    g.connect(b, a);
    std::string err = g.prepare(256);
    CHECK(!err.empty());
}

static void testInvalidConnections() {
    Graph g;
    int in = g.addChannel("in");
    int bus = g.addBus("bus");
    int out = g.addOutput("out");
    CHECK(!g.connect(out, bus));                 // output tidak boleh jadi sumber
    CHECK(!g.connect(bus, in));                  // channel tidak boleh jadi tujuan
    CHECK(!g.connect(in, in));                   // tidak boleh ke diri sendiri
    CHECK(!g.connect(in, 99));                   // indeks di luar jangkauan
    CHECK(g.connect(in, out));
}

static void testMeterReadsPeakAndRms() {
    Graph g;
    int in = g.addChannel("in");
    g.addOutput("out");
    g.connect(in, 1);
    g.prepare(256);
    run(g, {0.8f}, 64, 1);
    run(g, {0.8f}, 64, 1);
    CHECK(near(g.nodes()[in].peak, 0.8f));
    CHECK(near(g.nodes()[in].rms, 0.8f));
}

// Gain dan mute output (fader output) berlaku setelah mixing.
static void testOutputGainAndMute() {
    Graph g;
    int in = g.addChannel("in");
    g.addOutput("out");
    g.connect(in, 1);
    g.setNodeGain(1, -6.0206f);                  // gain output −6 dB
    g.prepare(256);
    run(g, {1.0f}, 64, 1);
    auto out = run(g, {1.0f}, 64, 1);
    CHECK(near(out[0][63], 0.5f));
    g.setNodeMute(1, true);
    run(g, {1.0f}, 64, 1);
    auto muted = run(g, {1.0f}, 64, 1);
    float maxAbs = 0.0f;
    for (float v : muted[0]) maxAbs = std::fmax(maxAbs, std::fabs(v));
    CHECK(maxAbs < 1e-6f);
    (void)in;
}

int main() {
    testGainChannel();
    testMuteSilences();
    testRampIsLinear();
    testSendLevel();
    testDisabledRouteSilent();
    testChainOfBuses();
    testBusGain();
    testMixMinus();
    testCycleRejected();
    testInvalidConnections();
    testMeterReadsPeakAndRms();
    testOutputGainAndMute();
    std::printf("%d cek, %d gagal\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
