// SPDX-License-Identifier: GPL-3.0-or-later
#include "k2/diag/probe_session.h"

#include "func_probes.h"

#include "ps2_runtime.h"
#include "runtime/ps2_memory.h"

#include <cstdio>
#include <cstring>
#include <span>
#include <utility>

namespace k2::diag {

struct SessionAccess {
    static void dispatch(void *user, PS2Runtime::DispatchEdge edge, std::uint32_t target_pc, std::uint32_t source_pc,
                         R5900Context *ctx)
    {
        auto &session = *static_cast<ProbeSession *>(user);
        const std::span<const std::uint8_t> ram(session.runtime_.memory().getRDRAM(), PS2_RAM_SIZE);
        session.scratch_.clear();
        session.attrib_->set_frame(session.frame());
        const bool truncated = session.attrib_->on_edge(
            edge == PS2Runtime::DispatchEdge::Enter ? Edge::Enter : Edge::Exit,
            reinterpret_cast<std::uintptr_t>(ctx), target_pc, source_pc, ram, session.scratch_);
        if (session.scratch_.empty() && !truncated) {
            return;
        }
        const std::uint32_t frame = session.frame();
        for (const auto &ev : session.scratch_) {
            session.log_.write(frame, "write", format_write_event(ev));
        }
        if (truncated) {
            session.log_.write(frame, "attrib-truncated",
                               "max_events=" + std::to_string(session.spec_.attrib_max_events));
        }
    }
};

namespace {

std::string range_text(const Range &r)
{
    char buf[48];
    std::snprintf(buf, sizeof(buf), "0x%08x:0x%x", r.address, r.length);
    return buf;
}

} // namespace

ProbeSession::ProbeSession(PS2Runtime &runtime, const ProbeSpec &spec, ProbeLog &log)
    : runtime_(runtime), spec_(spec), log_(log)
{
}

ProbeSession::~ProbeSession()
{
    if (attrib_) {
        runtime_.setDispatchObserver(nullptr, nullptr);
    }
}

std::expected<std::unique_ptr<ProbeSession>, std::string>
ProbeSession::install(PS2Runtime &runtime, const ProbeSpec &spec, ProbeLog &log)
{
    if (runtime.dispatchObserver() != nullptr) {
        return std::unexpected(std::string("a dispatch observer is already installed"));
    }
    std::unique_ptr<ProbeSession> session(new ProbeSession(runtime, spec, log));
    if (auto r = detail::install_func_probes(runtime, session->spec_, log, session->frame_); !r) {
        return std::unexpected(r.error());
    }
    if (!spec.attrib.empty()) {
        session->attrib_.emplace(spec.attrib, spec.attrib_max_events);
        session->attrib_->reset(std::span<const std::uint8_t>(runtime.memory().getRDRAM(), PS2_RAM_SIZE));
        runtime.setDispatchObserver(&SessionAccess::dispatch, session.get());
    }

    std::uint32_t attrib_bytes = 0;
    for (const auto &r : spec.attrib) {
        attrib_bytes += r.length;
    }
    log.write(0, "probe",
              "version=1 funcs=" + std::to_string(spec.funcs.size()) + " watches=" +
                  std::to_string(spec.watches.size()) + " attrib_ranges=" + std::to_string(spec.attrib.size()) +
                  " attrib_bytes=" + std::to_string(attrib_bytes) + " screenshot=" + (spec.screenshot ? "1" : "0"));
    for (const auto &f : spec.funcs) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "fn=0x%08x", f.address);
        log.write(0, "func-installed", buf);
    }
    for (const auto &r : spec.attrib) {
        log.write(0, "attrib-range", "range=" + range_text(r));
    }
    return session;
}

FrameActions ProbeSession::on_presented_frame(PS2Runtime &runtime, std::uint32_t n)
{
    frame_.store(n, std::memory_order_relaxed);
    FrameActions actions;
    const std::uint8_t *rdram = runtime.memory().getRDRAM();
    for (const auto &w : spec_.watches) {
        if (n % w.every != 0) {
            continue;
        }
        for (std::uint32_t off = 0; off < w.range.length; off += 4) {
            const std::uint32_t addr = w.range.address + off;
            std::uint32_t value = 0;
            std::memcpy(&value, rdram + addr, sizeof(value));
            char buf[96];
            std::snprintf(buf, sizeof(buf), "addr=0x%08x value=0x%08x dec=%u", addr, value, value);
            log_.write(n, "watch", buf);
        }
    }
    if (spec_.screenshot && n >= spec_.screenshot->from && n % spec_.screenshot->every == 0) {
        actions.screenshot = true;
        char buf[48];
        std::snprintf(buf, sizeof(buf), "file=k2-frame-%06u.png", n);
        log_.write(n, "screenshot", buf);
    }
    return actions;
}

void ProbeSession::finish()
{
    if (finished_) {
        return;
    }
    finished_ = true;
    const std::uint32_t frame = this->frame();
    if (attrib_) {
        runtime_.setDispatchObserver(nullptr, nullptr);
        for (const auto &ev : attrib_->summary()) {
            log_.write(frame, "last-write", format_write_event(ev) + " at_frame=" + std::to_string(ev.frame));
        }
        log_.write(frame, "attrib-stats",
                   "changes=" + std::to_string(attrib_->changes()) + " events=" + std::to_string(attrib_->events()) +
                       " unattributed=" + std::to_string(attrib_->unattributed()) +
                       " resyncs=" + std::to_string(attrib_->resyncs()) +
                       " truncated=" + (attrib_->truncated() ? "1" : "0"));
    }
    log_.write(frame, "end", "");
    log_.flush();
}

} // namespace k2::diag
