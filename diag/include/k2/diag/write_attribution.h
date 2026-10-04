// SPDX-License-Identifier: GPL-3.0-or-later
// Function-granular write attribution (ADR-0006). Pure: fed dispatch edges and a RAM view.
//
// At every dispatch edge the watched ranges are compared with a shadow copy. A change seen at
// Enter(T) happened in the caller (the top of that context's call stack), at call site
// `sourcePc`; a change seen at Exit(T) happened inside T (or its callees that bypass dispatch).
// With an empty stack the writer is unknown and reported as unattributed.
#pragma once

#include "k2/diag/probe_spec.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <vector>

namespace k2::diag {

enum class Edge : std::uint8_t { Enter, Exit };

struct WriteEvent {
    std::uint32_t address = 0;     // byte address
    std::uint8_t old_value = 0;
    std::uint8_t new_value = 0;
    std::optional<std::uint32_t> writer; // function entry PC; nullopt = unattributed
    std::uint32_t site = 0;        // call site PC for Enter edges, else 0
    Edge edge = Edge::Enter;
    std::uint32_t frame = 0;       // frame tag current when the change was seen (set_frame)
};

class WriteAttributor {
public:
    static constexpr std::size_t kMaxDepth = 256;

    // Call reset() with the RAM image before the first edge (shadow baseline).
    WriteAttributor(std::vector<Range> ranges, std::uint32_t max_events);

    // Takes the baseline from `ram` (indexed by guest address).
    void reset(std::span<const std::uint8_t> ram);

    // Frame tag copied into subsequent events (the presented frame number).
    void set_frame(std::uint32_t frame) { frame_ = frame; }

    // Processes one edge on context `ctx_id`. Appends changed bytes to `out` (address order)
    // until the event budget is spent. Returns true exactly once: on the call that first drops
    // an event because the budget is spent (the caller logs a "truncated" line).
    bool on_edge(Edge edge, std::uintptr_t ctx_id, std::uint32_t target_pc, std::uint32_t source_pc,
                 std::span<const std::uint8_t> ram, std::vector<WriteEvent> &out);

    // Last attributed write per byte, sorted by address (bytes never changed are omitted).
    [[nodiscard]] std::vector<WriteEvent> summary() const;

    [[nodiscard]] std::uint64_t changes() const { return changes_; } // all changed bytes seen
    [[nodiscard]] std::uint64_t events() const { return events_; }   // changes emitted (capped)
    [[nodiscard]] std::uint64_t unattributed() const { return unattributed_; }
    [[nodiscard]] std::uint64_t resyncs() const { return resyncs_; }
    [[nodiscard]] bool truncated() const { return truncated_; }

private:
    void diff(Edge edge, std::optional<std::uint32_t> writer, std::uint32_t site,
              std::span<const std::uint8_t> ram, std::vector<WriteEvent> &out, bool &newly_truncated);

    std::vector<Range> ranges_;
    std::vector<std::vector<std::uint8_t>> shadow_;
    std::map<std::uintptr_t, std::vector<std::uint32_t>> stacks_;
    std::map<std::uint32_t, WriteEvent> last_;
    std::uint32_t max_events_;
    std::uint32_t frame_ = 0;
    std::uint64_t changes_ = 0;
    std::uint64_t events_ = 0;
    std::uint64_t unattributed_ = 0;
    std::uint64_t resyncs_ = 0;
    bool truncated_ = false;
};

} // namespace k2::diag
