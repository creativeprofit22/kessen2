// SPDX-License-Identifier: GPL-3.0-or-later
// Write attributor + log formatting on a synthetic RAM buffer.
#include "check.hpp"

#include "k2/diag/probe_log.h"
#include "k2/diag/write_attribution.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

using k2::diag::Edge;
using k2::diag::ProbeLog;
using k2::diag::Range;
using k2::diag::WriteAttributor;
using k2::diag::WriteEvent;
using k2diagtest::check;

namespace {

constexpr std::uintptr_t kCtxA = 1;
constexpr std::uintptr_t kCtxB = 2;

struct Fixture {
    std::vector<std::uint8_t> ram = std::vector<std::uint8_t>(0x1000, 0);
    WriteAttributor attrib;
    std::vector<WriteEvent> out;

    explicit Fixture(std::vector<Range> ranges, std::uint32_t max_events = 1000)
        : attrib(std::move(ranges), max_events)
    {
        attrib.reset(ram);
    }
    bool edge(Edge e, std::uintptr_t ctx, std::uint32_t target, std::uint32_t source = 0)
    {
        return attrib.on_edge(e, ctx, target, source, ram, out);
    }
};

void enter_exit_attribution()
{
    Fixture f({{0x100, 4}});
    f.edge(Edge::Enter, kCtxA, 0x1000, 0x50);   // main -> A
    f.ram[0x101] = 0xAA;                         // written inside A
    f.attrib.set_frame(42);
    f.edge(Edge::Exit, kCtxA, 0x1000);
    check(!f.out.empty() && f.out[0].frame == 42 && f.attrib.summary()[0].frame == 42, "frame tag");
    check(f.out.size() == 1, "one event");
    check(f.out[0].address == 0x101 && f.out[0].old_value == 0 && f.out[0].new_value == 0xAA,
          "address/old/new");
    check(f.out[0].writer == 0x1000u && f.out[0].edge == Edge::Exit, "exit attributes to callee");
}

void enter_attributes_to_caller_with_site()
{
    Fixture f({{0x100, 4}});
    f.edge(Edge::Enter, kCtxA, 0x1000, 0x50);  // stack: A
    f.ram[0x102] = 7;                           // A writes, then calls B
    f.edge(Edge::Enter, kCtxA, 0x2000, 0x1010);
    check(f.out.size() == 1 && f.out[0].writer == 0x1000u && f.out[0].site == 0x1010 &&
              f.out[0].edge == Edge::Enter,
          "enter: writer = caller, site = call site");
}

void unattributed_on_empty_stack()
{
    Fixture f({{0x100, 4}});
    f.ram[0x100] = 1;
    f.edge(Edge::Enter, kCtxA, 0x1000, 0x50);
    check(f.out.size() == 1 && !f.out[0].writer, "no caller -> unattributed");
    check(f.attrib.unattributed() == 1, "unattributed count");
}

void contexts_have_separate_stacks()
{
    Fixture f({{0x100, 4}});
    f.edge(Edge::Enter, kCtxA, 0x1000, 0x50);
    f.edge(Edge::Enter, kCtxB, 0x3000, 0x60);
    f.ram[0x103] = 9;
    f.edge(Edge::Enter, kCtxA, 0x2000, 0x1004); // seen on A's edge: A's caller
    check(f.out.size() == 1 && f.out[0].writer == 0x1000u, "uses the edge's own context stack");
}

void missing_exit_resyncs()
{
    Fixture f({{0x100, 4}});
    f.edge(Edge::Enter, kCtxA, 0x1000, 0x50);
    f.edge(Edge::Enter, kCtxA, 0x2000, 0x1004); // B never exits (thrown)
    f.edge(Edge::Exit, kCtxA, 0x1000);         // A exits: drop B
    check(f.attrib.resyncs() == 1, "resync counted");
    f.ram[0x100] = 5;
    f.edge(Edge::Enter, kCtxA, 0x4000, 0x60);   // stack must be empty again
    check(f.out.size() == 1 && !f.out[0].writer, "stack emptied after resync");

    Fixture g({{0x100, 4}});
    g.edge(Edge::Exit, kCtxA, 0x1234);         // exit without enter
    check(g.attrib.resyncs() == 1, "unmatched exit counted");
}

void depth_cap()
{
    Fixture f({{0x100, 4}});
    for (std::uint32_t i = 0; i < WriteAttributor::kMaxDepth + 1; ++i) {
        f.edge(Edge::Enter, kCtxA, 0x1000 + 4 * i, 0);
    }
    check(f.attrib.resyncs() == 1, "stack overflow resyncs once");
}

void event_cap_and_truncation()
{
    Fixture f({{0x100, 8}}, 3);
    f.edge(Edge::Enter, kCtxA, 0x1000, 0);
    for (int i = 0; i < 8; ++i) {
        f.ram[0x100 + i] = static_cast<std::uint8_t>(i + 1);
    }
    const bool first = f.edge(Edge::Exit, kCtxA, 0x1000);
    check(f.out.size() == 3, "events capped");
    check(first, "truncation reported");
    f.ram[0x100] = 0x55;
    const bool second = f.edge(Edge::Enter, kCtxA, 0x1000, 0);
    check(!second, "truncation reported only once");
    check(f.attrib.truncated() && f.attrib.changes() == 9, "all changes still counted");
    const auto summary = f.attrib.summary();
    check(summary.size() == 8 && summary[0].address == 0x100 && summary[0].new_value == 0x55,
          "summary keeps tracking after the cap");
}

void summary_sorted_by_address()
{
    Fixture f({{0x300, 2}, {0x100, 2}});
    f.edge(Edge::Enter, kCtxA, 0x1000, 0);
    f.ram[0x301] = 1;
    f.ram[0x100] = 2;
    f.edge(Edge::Exit, kCtxA, 0x1000);
    f.ram[0x100] = 3;
    f.edge(Edge::Enter, kCtxA, 0x2000, 0x10);
    const auto s = f.attrib.summary();
    check(s.size() == 2 && s[0].address == 0x100 && s[1].address == 0x301, "summary sorted");
    check(s[0].new_value == 3 && !s[0].writer, "summary keeps the last write per byte");
    check(f.out[0].address == 0x100 && f.out[1].address == 0x301, "events in address order per edge");
}

void log_format()
{
    std::string captured;
    ProbeLog log([&captured](std::string_view line) { captured += line; });
    WriteEvent ev{.address = 0x1595, .old_value = 0x01, .new_value = 0xff, .writer = 0x1b6670u,
                  .site = 0x1b0004, .edge = Edge::Enter};
    log.write(12, "write", k2::diag::format_write_event(ev));
    ev.writer.reset();
    ev.edge = Edge::Exit;
    log.write(13, "write", k2::diag::format_write_event(ev));
    log.write(13, "end", "");
    check(captured ==
              "frame=12 seq=0 kind=write addr=0x00001595 old=0x01 new=0xff writer=0x001b6670 site=0x001b0004 edge=enter\n"
              "frame=13 seq=1 kind=write addr=0x00001595 old=0x01 new=0xff writer=unattributed edge=exit\n"
              "frame=13 seq=2 kind=end\n",
          "log lines: " + captured);
}

void file_log_flushes_each_line()
{
    const auto path = std::filesystem::temp_directory_path() / "k2diag_probe_log_flush.log";
    {
        auto log = ProbeLog::open(path);
        check(log.has_value(), "open file log");
        (*log)->write(1, "start", "");
        (*log)->write(2, "write", "addr=0x1");
        // No flush(): simulates std::_Exit / crash before ProbeSession::finish().
        std::ifstream in(path, std::ios::binary);
        const std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        check(content == "frame=1 seq=0 kind=start\nframe=2 seq=1 kind=write addr=0x1\n",
              "lines on disk before flush: " + content);
    }
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

} // namespace

int main()
{
    return k2diagtest::run({
        {"enter/exit attribution", enter_exit_attribution},
        {"enter attributes to caller with site", enter_attributes_to_caller_with_site},
        {"unattributed on empty stack", unattributed_on_empty_stack},
        {"contexts have separate stacks", contexts_have_separate_stacks},
        {"missing exit resyncs", missing_exit_resyncs},
        {"depth cap", depth_cap},
        {"event cap and truncation", event_cap_and_truncation},
        {"summary sorted by address", summary_sorted_by_address},
        {"log format", log_format},
        {"file log flushes each line", file_log_flushes_each_line},
    });
}
