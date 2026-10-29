#include "graph.h"

#include <algorithm>
#include <cmath>
#include <queue>

namespace audiohub {

namespace {

float dbToLin(float db) { return std::pow(10.0f, db / 20.0f); }

// Gain linear pada sampel ke-i dari blok n, interpolasi linear dari a ke b.
float rampAt(float a, float b, int i, int n) {
    return a + (b - a) * (float)(i + 1) / (float)n;
}

}  // namespace

int Graph::addChannel(const std::string& id) {
    nodes_.push_back(Node{NodeKind::Channel, id});
    channelNodes_.push_back((int)nodes_.size() - 1);
    return (int)nodes_.size() - 1;
}

int Graph::addBus(const std::string& id) {
    nodes_.push_back(Node{NodeKind::Bus, id});
    return (int)nodes_.size() - 1;
}

int Graph::addOutput(const std::string& id) {
    nodes_.push_back(Node{NodeKind::Output, id});
    outputNodes_.push_back((int)nodes_.size() - 1);
    return (int)nodes_.size() - 1;
}

bool Graph::connect(int from, int to, float sendDb, bool enabled) {
    if (from < 0 || to < 0 || from >= (int)nodes_.size() || to >= (int)nodes_.size()) return false;
    if (from == to) return false;
    NodeKind fk = nodes_[from].kind;
    NodeKind tk = nodes_[to].kind;
    bool fromOk = fk == NodeKind::Channel || fk == NodeKind::Bus;
    bool toOk = tk == NodeKind::Bus || tk == NodeKind::Output;
    if (!fromOk || !toOk) return false;

    Route r;
    r.from = from;
    r.to = to;
    r.sendDb = sendDb;
    r.enabled = enabled;
    r.targetLin = enabled ? dbToLin(sendDb) : 0.0f;
    r.lin = r.targetLin;
    routes_.push_back(r);
    return true;
}

bool Graph::setRouteEnabled(int route, bool enabled) {
    if (route < 0 || route >= (int)routes_.size()) return false;
    Route& r = routes_[route];
    r.enabled = enabled;
    r.targetLin = enabled ? dbToLin(r.sendDb) : 0.0f;
    return true;
}

bool Graph::setRouteSend(int route, float sendDb) {
    if (route < 0 || route >= (int)routes_.size()) return false;
    Route& r = routes_[route];
    r.sendDb = sendDb;
    r.targetLin = r.enabled ? dbToLin(sendDb) : 0.0f;
    return true;
}

void Graph::setNodeGain(int node, float db) {
    if (node < 0 || node >= (int)nodes_.size()) return;
    nodes_[node].gainDb = db;
    updateNodeTarget(node);
}

void Graph::setNodeMute(int node, bool mute) {
    if (node < 0 || node >= (int)nodes_.size()) return;
    nodes_[node].mute = mute;
    updateNodeTarget(node);
}

void Graph::updateNodeTarget(int node) {
    Node& n = nodes_[node];
    n.targetLin = n.mute ? 0.0f : dbToLin(n.gainDb);
}

std::string Graph::prepare(int maxFrames) {
    maxFrames_ = maxFrames;

    // Urutan topologi bus dengan algoritma Kahn. Bus yang tidak tercapai berarti ada siklus.
    std::vector<int> indegree(nodes_.size(), 0);
    int busTotal = 0;
    for (const Route& r : routes_) {
        if (nodes_[r.from].kind == NodeKind::Bus && nodes_[r.to].kind == NodeKind::Bus) indegree[r.to]++;
    }
    std::queue<int> ready;
    for (int i = 0; i < (int)nodes_.size(); i++) {
        if (nodes_[i].kind != NodeKind::Bus) continue;
        busTotal++;
        if (indegree[i] == 0) ready.push(i);
    }
    busOrder_.clear();
    while (!ready.empty()) {
        int n = ready.front();
        ready.pop();
        busOrder_.push_back(n);
        for (const Route& r : routes_) {
            if (r.from == n && nodes_[r.to].kind == NodeKind::Bus && --indegree[r.to] == 0) ready.push(r.to);
        }
    }
    if ((int)busOrder_.size() != busTotal) {
        busOrder_.clear();
        return "Siklus terdeteksi di antara bus; konfigurasi ditolak";
    }

    incoming_.assign(nodes_.size(), {});
    for (int i = 0; i < (int)routes_.size(); i++) incoming_[routes_[i].to].push_back(i);

    buffers_.assign(nodes_.size(), {});
    for (int i = 0; i < (int)nodes_.size(); i++) {
        if (nodes_[i].kind != NodeKind::Output) buffers_[i].assign(maxFrames, 0.0f);
    }
    return "";
}

void Graph::mixRoutes(int node, float* dst, int frames) {
    for (int ri : incoming_[node]) {
        Route& r = routes_[ri];
        if (r.lin == 0.0f && r.targetLin == 0.0f) continue;
        const float* src = buffers_[r.from].data();
        float a = r.lin;
        float b = r.targetLin;
        for (int i = 0; i < frames; i++) dst[i] += src[i] * rampAt(a, b, i, frames);
        r.lin = b;
    }
}

void Graph::meter(int node, int frames) {
    const float* buf = buffers_[node].data();
    float pk = 0.0f, sq = 0.0f;
    for (int i = 0; i < frames; i++) {
        float v = std::fabs(buf[i]);
        if (v > pk) pk = v;
        sq += buf[i] * buf[i];
    }
    nodes_[node].peak = pk;
    nodes_[node].rms = std::sqrt(sq / (float)frames);
}

void Graph::process(const float* const* inputs, float* const* outputs, int frames) {
    if (maxFrames_ == 0) return;
    if (frames > maxFrames_) frames = maxFrames_;
    if (frames <= 0) return;

    // Channel: salin input dengan gain node (ramp)
    for (int c = 0; c < (int)channelNodes_.size(); c++) {
        int n = channelNodes_[c];
        Node& node = nodes_[n];
        float* dst = buffers_[n].data();
        const float* src = inputs[c];
        float a = node.curLin;
        float b = node.targetLin;
        for (int i = 0; i < frames; i++) dst[i] = src[i] * rampAt(a, b, i, frames);
        node.curLin = b;
        meter(n, frames);
    }

    // Bus: jumlahkan route masuk, lalu gain bus, dalam urutan topologi
    for (int n : busOrder_) {
        float* dst = buffers_[n].data();
        std::fill(dst, dst + frames, 0.0f);
        mixRoutes(n, dst, frames);
        Node& node = nodes_[n];
        float a = node.curLin;
        float b = node.targetLin;
        for (int i = 0; i < frames; i++) dst[i] *= rampAt(a, b, i, frames);
        node.curLin = b;
        meter(n, frames);
    }

    // Output: jumlahkan route masuk langsung ke buffer output
    for (int o = 0; o < (int)outputNodes_.size(); o++) {
        int n = outputNodes_[o];
        float* dst = outputs[o];
        std::fill(dst, dst + frames, 0.0f);
        mixRoutes(n, dst, frames);
        Node& node = nodes_[n];
        float a = node.curLin;
        float b = node.targetLin;
        for (int i = 0; i < frames; i++) dst[i] *= rampAt(a, b, i, frames);
        node.curLin = b;
        float pk = 0.0f, sq = 0.0f;
        for (int i = 0; i < frames; i++) {
            float v = std::fabs(dst[i]);
            if (v > pk) pk = v;
            sq += dst[i] * dst[i];
        }
        node.peak = pk;
        node.rms = std::sqrt(sq / (float)frames);
    }
}

}  // namespace audiohub
