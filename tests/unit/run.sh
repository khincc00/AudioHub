#!/bin/sh
# Menjalankan uji unit engine (graph, DSP, dan API engine). Dari root repo: sh tests/unit/run.sh
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$(mktemp -d)"
clang++ -std=c++17 -O2 -Wall -Wextra -Werror "$ROOT/engine/graph/graph.cpp" "$ROOT/tests/unit/test_graph.cpp" -o "$OUT/test_graph"
clang++ -std=c++17 -O2 -Wall -Wextra -Werror "$ROOT/engine/dsp/dsp.cpp" "$ROOT/tests/unit/test_dsp.cpp" -o "$OUT/test_dsp"
clang++ -std=c++17 -O2 -Wall -Wextra -Werror -I "$ROOT/engine/dsp" "$ROOT/engine/api/audiohub_engine.cpp" "$ROOT/engine/dsp/dsp.cpp" "$ROOT/engine/graph/graph.cpp" "$ROOT/tests/unit/test_engine_api.cpp" -o "$OUT/test_engine_api"
"$OUT/test_graph"
"$OUT/test_dsp"
"$OUT/test_engine_api"
rm -rf "$OUT"
