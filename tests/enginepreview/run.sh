#!/bin/bash
# The torrent engine's preview reads (tools/engine/engine.patch): planned at the lowest priority, only their own
# pieces, never a deadline, and never changing what playback asks for. Built against the patched engine source that
# tools/build-engine.sh leaves in $ORIGIN_BUILD/engine/src.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"; ORIGIN_BUILD="${ORIGIN_BUILD:-$HOME/origin-build}"; E="$ORIGIN_BUILD/engine/src"
grep -q "DemandKind::preview" "$E/src/scheduler/stream_demand_plan.cpp" || { echo "the engine source is not patched (tools/build-engine.sh mac first)"; exit 1; }
OUT="$(mktemp -d)"; trap 'rm -rf "$OUT"' EXIT
c++ -std=c++20 -O1 -I"$E/src" -I"$E/include" "$ROOT/tests/enginepreview/test.cpp" "$E/src/scheduler/stream_demand_plan.cpp" "$E/src/scheduler/piece_scheduler.cpp" "$E/src/http/byte_range.cpp" -o "$OUT/t"
"$OUT/t" && echo "enginepreview: all passed"
