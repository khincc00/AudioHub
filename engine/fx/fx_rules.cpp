#include "fx_rules.h"

namespace audiohub {
namespace fx {

namespace {

struct Prefix {
    const char* text;
    Kind kind;
};

// VST3: harus dicek sebelum VST: karena keduanya berawalan sama.
const Prefix kPrefixes[] = {
    {"VST3:", Kind::Vst3},
    {"VST2:", Kind::Vst2},
    {"VST:", Kind::Vst},
    {"AU:", Kind::Au},
    {"JS:", Kind::Js},
    {"DX:", Kind::Dx},
};

}  // namespace

ParsedName parseName(const std::string& raw) {
    ParsedName out;
    out.name = raw;
    if (raw.rfind("FXADD:", 0) == 0) {
        out.fxAdd = true;
        out.name = raw.substr(6);
        return out;
    }
    for (const Prefix& p : kPrefixes) {
        std::string text = p.text;
        if (raw.compare(0, text.size(), text) == 0) {
            out.kind = p.kind;
            out.name = raw.substr(text.size());
            return out;
        }
    }
    return out;
}

bool isSupported(Kind kind) {
    return kind == Kind::Au;
}

int resolveInsert(const std::vector<std::string>& chain, const ParsedName& name, int instantiate) {
    if (name.fxAdd || !isSupported(name.kind) || name.name.empty()) return -1;
    const int n = (int)chain.size();

    if (instantiate <= -1000) {
        int pos = -1000 - instantiate;
        return pos <= n ? pos : -1;
    }
    if (instantiate < 0) return n;

    int first = -1;
    for (int i = 0; i < n; i++) {
        if (chain[(size_t)i] == name.name) {
            first = i;
            break;
        }
    }
    if (instantiate == 0) return first;
    return first >= 0 ? first : n;
}

}  // namespace fx
}  // namespace audiohub
