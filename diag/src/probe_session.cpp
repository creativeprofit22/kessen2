// SPDX-License-Identifier: GPL-3.0-or-later
#include "k2/diag/probe_session.h"

#include "func_probes.h"

#include "ps2_runtime.h"
#include "runtime/gs/gs_frontend.h"
#include "runtime/ps2_memory.h"

#include <cstdio>
#include <cstring>
#include <span>
#include <utility>
#include <vector>

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

bool scheduled(std::uint32_t n, std::uint32_t every, std::uint32_t from)
{
    return n >= from && n % every == 0;
}

struct LitRows {
    std::uint32_t count = 0;
    std::uint32_t first = 0;
    std::uint32_t last = 0;
};

// Rows of the latched host frame (tightly packed RGBA) with any non-black pixel.
LitRows lit_rows(const std::vector<std::uint8_t> &pixels, std::uint32_t width, std::uint32_t height)
{
    LitRows out;
    const std::size_t row_bytes = static_cast<std::size_t>(width) * 4u;
    for (std::uint32_t y = 0; y < height && (y + 1u) * row_bytes <= pixels.size(); ++y) {
        const std::uint8_t *row = pixels.data() + y * row_bytes;
        bool lit = false;
        for (std::size_t x = 0; x < row_bytes && !lit; x += 4u) {
            lit = row[x] != 0u || row[x + 1u] != 0u || row[x + 2u] != 0u;
        }
        if (lit) {
            out.first = out.count == 0 ? y : out.first;
            out.last = y;
            ++out.count;
        }
    }
    return out;
}

// One log line per GS draw or image transfer the engine recorded; register and present events
// are skipped because they carry no geometry.
void log_gs_events(ProbeLog &log, std::uint32_t frame, const std::vector<GSDebugHistoryEntry> &events)
{
    for (const GSDebugHistoryEntry &e : events) {
        char buf[320];
        if (e.kind == GSDebugEventKind::Draw) {
            std::snprintf(buf, sizeof(buf),
                          "seq=%llu gs_frame=%u prim=%d tme=%d verts=%u fbp=0x%x fbw=%u tbp0=0x%x tbw=%u tpsm=0x%x "
                          "tw=%u th=%u x=%.1f..%.1f y=%.1f..%.1f",
                          static_cast<unsigned long long>(e.seq), e.frameIndex, static_cast<int>(e.prim.type),
                          e.prim.tme ? 1 : 0, e.vertexCount, e.frame.fbp, e.frame.fbw, e.tex0.tbp0, e.tex0.tbw,
                          e.tex0.psm, e.tex0.tw, e.tex0.th, e.xMin, e.xMax, e.yMin, e.yMax);
            log.write(frame, "gsdraw", buf);
        } else if (e.kind == GSDebugEventKind::Transfer) {
            std::snprintf(buf, sizeof(buf),
                          "seq=%llu gs_frame=%u dir=%u dbp=0x%x dbw=%u dpsm=0x%x dsa=%u,%u rr=%ux%u pixels=%u",
                          static_cast<unsigned long long>(e.seq), e.frameIndex, e.trxdir, e.bitbltbuf.dbp,
                          e.bitbltbuf.dbw, e.bitbltbuf.dpsm, e.trxpos.dsax, e.trxpos.dsay, e.trxreg.rrw,
                          e.trxreg.rrh, e.transferPixels);
            log.write(frame, "gsxfer", buf);
        }
    }
}

// Privileged display registers as the presenter sees them, plus what it chose to show.
std::string gsregs_text(PS2Runtime &runtime)
{
    const GSRegisters &regs = runtime.memory().gs();
    const GSDebugSnapshot snap = runtime.gs().getDebugSnapshot();
    std::vector<std::uint8_t> pixels;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    const bool latched = runtime.gs().copyLatchedHostPresentationFrame(pixels, width, height);
    const LitRows lit = latched ? lit_rows(pixels, width, height) : LitRows{};
    char buf[640];
    std::snprintf(buf, sizeof(buf),
                  "pmode=0x%llx smode2=0x%llx dispfb1=0x%llx display1=0x%llx dispfb2=0x%llx display2=0x%llx "
                  "present_fbp=0x%x source_fbp=0x%x preferred=%d w=%u h=%u latched=%d lit_rows=%u "
                  "lit_first=%u lit_last=%u ctx0_fbp=0x%x ctx0_scissor_y=%u..%u ctx0_ofy=%u "
                  "ctx1_fbp=0x%x ctx1_scissor_y=%u..%u ctx1_ofy=%u",
                  static_cast<unsigned long long>(regs.pmode), static_cast<unsigned long long>(regs.smode2),
                  static_cast<unsigned long long>(regs.dispfb1), static_cast<unsigned long long>(regs.display1),
                  static_cast<unsigned long long>(regs.dispfb2), static_cast<unsigned long long>(regs.display2),
                  snap.hostPresentationDisplayFbp, snap.hostPresentationSourceFbp,
                  snap.hostPresentationUsedPreferred ? 1 : 0, snap.hostPresentationWidth,
                  snap.hostPresentationHeight, latched ? 1 : 0, lit.count, lit.first, lit.last,
                  snap.ctx[0].frame.fbp, snap.ctx[0].scissor.y0, snap.ctx[0].scissor.y1,
                  snap.ctx[0].xyoffset.ofy, snap.ctx[1].frame.fbp, snap.ctx[1].scissor.y0,
                  snap.ctx[1].scissor.y1, snap.ctx[1].xyoffset.ofy);
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
    if (spec.gsevents) {
        // The engine's GS event ring starts paused (it exists for the debug panel).
        runtime.gs().clearDebugHistory();
        runtime.gs().setDebugHistoryPaused(false);
    }

    std::uint32_t attrib_bytes = 0;
    for (const auto &r : spec.attrib) {
        attrib_bytes += r.length;
    }
    log.write(0, "probe",
              "version=1 funcs=" + std::to_string(spec.funcs.size()) + " watches=" +
                  std::to_string(spec.watches.size()) + " attrib_ranges=" + std::to_string(spec.attrib.size()) +
                  " attrib_bytes=" + std::to_string(attrib_bytes) + " screenshot=" + (spec.screenshot ? "1" : "0") +
                  " gsregs=" + (spec.gsregs ? "1" : "0") + " gsevents=" + (spec.gsevents ? "1" : "0"));
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
    if (spec_.gsregs && scheduled(n, spec_.gsregs->every, spec_.gsregs->from)) {
        log_.write(n, "gsregs", gsregs_text(runtime));
    }
    if (spec_.gsevents && scheduled(n, spec_.gsevents->every, spec_.gsevents->from)) {
        log_gs_events(log_, n, runtime.gs().getDebugHistory());
        runtime.gs().clearDebugHistory();
    }
    if (spec_.screenshot && scheduled(n, spec_.screenshot->every, spec_.screenshot->from)) {
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
