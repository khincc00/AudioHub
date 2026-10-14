// Routing graph Tahap 1: channel → bus → output, dengan gain/mute per node dan send per route.
// Tanpa siklus antar-bus. Tidak mengalokasikan memori saat proses; semua buffer dibuat di prepare().
#pragma once

#include <string>
#include <vector>

namespace audiohub {

enum class NodeKind { Channel, Bus, Output };

struct Node {
    NodeKind kind;
    std::string id;
    float gainDb = 0.0f;
    bool mute = false;
    float curLin = 1.0f;     // gain linear saat ini (untuk ramp)
    float targetLin = 1.0f;  // gain linear tujuan
    float peak = 0.0f;       // meter blok terakhir, linear
    float rms = 0.0f;
};

struct Route {
    int from = 0;
    int to = 0;
    float sendDb = 0.0f;
    bool enabled = true;
    float lin = 1.0f;       // send linear saat ini (untuk ramp)
    float targetLin = 1.0f; // send linear tujuan
};

class Graph {
public:
    int addChannel(const std::string& id);
    int addBus(const std::string& id);
    int addOutput(const std::string& id);

    // Sumber harus Channel atau Bus; tujuan harus Bus atau Output. Mengembalikan false jika ditolak.
    bool connect(int from, int to, float sendDb = 0.0f, bool enabled = true);
    bool setRouteEnabled(int route, bool enabled);
    bool setRouteSend(int route, float sendDb);
    void setNodeGain(int node, float db);
    void setNodeMute(int node, bool mute);

    // Menyusun urutan bus dan mengalokasikan buffer. Mengembalikan "" jika valid, atau pesan error.
    std::string prepare(int maxFrames);

    // Memproses satu blok. inputs: satu pointer per channel; outputs: satu pointer per output.
    // Jumlah frame dibatasi maxFrames dari prepare().
    void process(const float* const* inputs, float* const* outputs, int frames);

    const std::vector<Node>& nodes() const { return nodes_; }
    const std::vector<Route>& routes() const { return routes_; }
    int channelCount() const { return (int)channelNodes_.size(); }
    int outputCount() const { return (int)outputNodes_.size(); }

private:
    void updateNodeTarget(int node);
    void mixRoutes(int node, float* dst, int frames);
    void meter(int node, int frames);

    std::vector<Node> nodes_;
    std::vector<Route> routes_;
    std::vector<int> channelNodes_;
    std::vector<int> outputNodes_;
    std::vector<int> busOrder_;
    std::vector<std::vector<int>> incoming_;  // indeks route masuk per node
    std::vector<std::vector<float>> buffers_; // buffer per node (channel dan bus)
    int maxFrames_ = 0;
};

}  // namespace audiohub
