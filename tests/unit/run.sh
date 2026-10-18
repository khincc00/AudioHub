#!/bin/sh
# Menjalankan uji unit engine (graph, DSP, API engine, dan aturan FX). Dari root repo: sh tests/unit/run.sh
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$(mktemp -d)"
clang++ -std=c++17 -O2 -Wall -Wextra -Werror "$ROOT/engine/graph/graph.cpp" "$ROOT/tests/unit/test_graph.cpp" -o "$OUT/test_graph"
clang++ -std=c++17 -O2 -Wall -Wextra -Werror "$ROOT/engine/dsp/dsp.cpp" "$ROOT/tests/unit/test_dsp.cpp" -o "$OUT/test_dsp"
clang++ -std=c++17 -O2 -Wall -Wextra -Werror -I "$ROOT/engine/dsp" "$ROOT/engine/api/audiohub_engine.cpp" "$ROOT/engine/dsp/dsp.cpp" "$ROOT/engine/graph/graph.cpp" "$ROOT/tests/unit/test_engine_api.cpp" -o "$OUT/test_engine_api"
clang++ -std=c++17 -O2 -Wall -Wextra -Werror "$ROOT/tests/unit/test_route_policy.cpp" -o "$OUT/test_route_policy"
clang++ -std=c++17 -O2 -Wall -Wextra -Werror "$ROOT/engine/fx/fx_rules.cpp" "$ROOT/tests/unit/test_fx_rules.cpp" -o "$OUT/test_fx_rules"
"$OUT/test_graph"
"$OUT/test_dsp"
"$OUT/test_engine_api"
"$OUT/test_route_policy"
"$OUT/test_fx_rules"
rm -rf "$OUT"
