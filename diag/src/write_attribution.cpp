// SPDX-License-Identifier: GPL-3.0-or-later
#include "k2/diag/write_attribution.h"

#include <algorithm>
#include <cstring>
#include <iterator>
#include <ranges>
#include <utility>

namespace k2::diag {

WriteAttributor::WriteAttributor(std::vector<Range> ranges, std::uint32_t max_events)
    : ranges_(std::move(ranges)), max_events_(max_events)
{
    std::ranges::sort(ranges_, {}, &Range::address);
    shadow_.reserve(ranges_.size());
    for (const auto &r : ranges_) {
        shadow_.emplace_back(r.length, std::uint8_t{0});
    }
}

void WriteAttributor::reset(std::span<const std::uint8_t> ram)
{
    for (std::size_t i = 0; i < ranges_.size(); ++i) {
        const auto &r = ranges_[i];
        if (static_cast<std::size_t>(r.end()) <= ram.size()) {
            std::memcpy(shadow_[i].data(), ram.data() + r.address, r.length);
        }
    }
    stacks_.clear();
}

void WriteAttributor::diff(Edge edge, std::optional<std::uint32_t> writer, std::uint32_t site,
                           std::span<const std::uint8_t> ram, std::vector<WriteEvent> &out,
                           bool &newly_truncated)
{
    for (std::size_t i = 0; i < ranges_.size(); ++i) {
        const auto &r = ranges_[i];
        if (static_cast<std::size_t>(r.end()) > ram.size()) {
            continue;
        }
        const std::uint8_t *now = ram.data() + r.address;
        auto &shadow = shadow_[i];
        if (std::memcmp(shadow.data(), now, r.length) == 0) {
            continue;
        }
        for (std::uint32_t off = 0; off < r.length; ++off) {
            if (shadow[off] == now[off]) {
                continue;
            }
            const WriteEvent ev{.address = r.address + off,
                                .old_value = shadow[off],
                                .new_value = now[off],
                                .writer = writer,
                                .site = site,
                                .edge = edge,
                                .frame = frame_};
            shadow[off] = now[off];
            last_[ev.address] = ev;
            ++changes_;
            if (!writer) {
                ++unattributed_;
            }
            if (events_ < max_events_) {
                ++events_;
                out.push_back(ev);
            } else if (!truncated_) {
                truncated_ = true;
                newly_truncated = true;
            }
        }
    }
}

bool WriteAttributor::on_edge(Edge edge, std::uintptr_t ctx_id, std::uint32_t target_pc,
                              std::uint32_t source_pc, std::span<const std::uint8_t> ram,
                              std::vector<WriteEvent> &out)
{
    bool newly_truncated = false;
    auto &stack = stacks_[ctx_id];
    if (edge == Edge::Enter) {
        const std::optional<std::uint32_t> writer =
            stack.empty() ? std::nullopt : std::optional<std::uint32_t>(stack.back());
        diff(edge, writer, source_pc, ram, out, newly_truncated);
        if (stack.size() >= kMaxDepth) {
            // Exits were lost (e.g. a function left by exception); start over from here.
            stack.clear();
            ++resyncs_;
        }
        stack.push_back(target_pc);
        return newly_truncated;
    }

    // Exit(T): the change happened inside T.
    diff(edge, target_pc, 0, ram, out, newly_truncated);
    if (!stack.empty() && stack.back() == target_pc) {
        stack.pop_back();
    } else if (const auto it = std::ranges::find(std::views::reverse(stack), target_pc);
               it != std::views::reverse(stack).end()) {
        // Missing exits for the frames above T: drop them.
        stack.erase(std::prev(it.base()), stack.end());
        ++resyncs_;
    } else {
        // Exit with no matching enter (enter happened before install, or the stack was reset).
        ++resyncs_;
    }
    return newly_truncated;
}

std::vector<WriteEvent> WriteAttributor::summary() const
{
    std::vector<WriteEvent> out;
    out.reserve(last_.size());
    for (const auto &[addr, ev] : last_) {
        out.push_back(ev);
    }
    return out; // std::map keeps address order
}

} // namespace k2::diag
