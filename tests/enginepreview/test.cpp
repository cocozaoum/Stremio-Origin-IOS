#include "scheduler/stream_demand_plan.hpp"
#include <algorithm>
#include <cstdio>
#include <optional>
using namespace nuvio::scheduler; using nuvio::http::ByteRange;
static const std::uint64_t MB = 1024ULL * 1024ULL, FS = 64 * MB; static const std::uint32_t PS = (std::uint32_t)MB;
static const StreamWindow W{2 * MB, 16, 10 * MB, 0, 0};
static StreamDemand dm(std::uint64_t id, std::uint32_t p, DemandKind k) { return {id, 0, FS, PS, ByteRange{(std::uint64_t)p * PS, ((std::uint64_t)p + 1) * PS - 1}, k, false}; }
static std::optional<PriorityClass> pr(const StreamDemandPlan& pl, std::uint32_t p) { auto f = std::ranges::find(pl.pieces, p, &PiecePriority::piece); return f == pl.pieces.end() ? std::nullopt : std::optional(f->priority); }
static int fails; static void ok(bool c, const char* w) { std::printf("%s %s\n", c ? "ok  " : "FAIL", w); fails += !c; }
int main() {
  { auto pl = build_stream_demand_plan({dm(1, 40, DemandKind::preview)}, W);
    ok(pl.cold, "a preview alone: cold"); ok(pr(pl, 40) == PriorityClass::readahead, "a preview alone: its piece at readahead");
    ok(pl.pieces.size() == 1, "a preview alone: only its own piece"); ok(pl.deadline_order.empty(), "a preview alone: no deadlines"); }
  { auto base = build_stream_demand_plan({dm(1, 5, DemandKind::reader)}, W);
    auto pl = build_stream_demand_plan({dm(1, 5, DemandKind::reader), dm(2, 40, DemandKind::preview)}, W);
    ok(pl.focused_demand_id == 1, "reader + preview: the reader stays focused (preview added last)");
    ok(pr(pl, 40) == PriorityClass::readahead, "reader + preview: the preview piece at readahead");
    ok(pl.deadline_order == base.deadline_order, "reader + preview: the reader's deadlines unchanged");
    bool same = true; for (auto& p : base.pieces) if (pr(pl, p.piece) != p.priority) same = false; ok(same, "reader + preview: every reader piece keeps its priority");
    ok(pl.pieces.size() == base.pieces.size() + 1, "reader + preview: exactly one piece more"); }
  { auto pl = build_stream_demand_plan({dm(1, 5, DemandKind::reader), dm(2, 6, DemandKind::preview)}, W);
    auto base = build_stream_demand_plan({dm(1, 5, DemandKind::reader)}, W);
    ok(pr(pl, 6) == pr(base, 6), "a preview on the reader's own window: the reader's priority wins"); }
  return fails != 0; }
