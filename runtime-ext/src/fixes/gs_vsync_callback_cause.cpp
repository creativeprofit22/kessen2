// SPDX-License-Identifier: GPL-3.0-or-later
// BRINGUP.md #5: libgraph's sceGsSyncVCallback installs the callback as the INTC handler for
// VBLANK start (the retail code calls DisableIntc(2) / AddIntcHandler(2, cb, 0)), so on hardware
// the callback runs as cb(cause = 2). PS2Recomp (75d729ce, EeScheduler::processEvent) calls it
// with a0 = vsync tick count instead. Kessen II's VSync handler (0x0014D590) returns early unless
// a0 == 2, so its "frame done" flag is never cleared and the main loop spins forever.
//
// Fix without touching PS2Recomp: route sceGsSyncVCallback through a wrapper that also wraps the
// registered guest callback; when the scheduler invokes it (ra == 0, no guest caller) a0 is set
// to INTC_VBLANK_S. Direct guest calls to the same function are left untouched. Upstream fix:
// patches/ps2recomp/0001-gs-vsync-callback-cause.patch.
#include "fixes.h"
#include "hle_bind.h"

#include "ps2_stubs.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <mutex>

namespace k2::runtime_ext::fixes {
namespace {

constexpr std::uint32_t kSceGsSyncVCallback = 0x00101020u;
constexpr std::uint32_t kIntcVblankStart = 2u;

struct Wrapped {
    std::uint32_t address = 0;
    PS2Runtime::RecompiledFunction original = nullptr;
};

// Few distinct callbacks are ever registered; guarded because registration and invocation may
// happen on different guest threads.
std::mutex g_mutex;
std::array<Wrapped, 8> g_wrapped{};

PS2Runtime::RecompiledFunction find_original(std::uint32_t address)
{
    std::lock_guard lock(g_mutex);
    for (const Wrapped &w : g_wrapped) {
        if (w.address == address) {
            return w.original;
        }
    }
    return nullptr;
}

void vsync_callback_with_cause(std::uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const auto original = find_original(ctx->pc);
    if (original == nullptr) {
        std::fprintf(stderr, "[kessen2] vsync: no original for 0x%08x\n", ctx->pc);
        ctx->pc = GPR_U32(ctx, 31);
        return;
    }
    if (GPR_U32(ctx, 31) == 0u) { // invoked by the runtime as an interrupt handler
        SET_GPR_U32(ctx, 4, kIntcVblankStart);
    }
    original(rdram, ctx, runtime);
}

void wrap_callback(PS2Runtime &runtime, std::uint32_t address)
{
    if (address == 0u || !runtime.hasFunction(address)) {
        return;
    }
    const auto current = runtime.lookupFunction(address);
    if (current == &vsync_callback_with_cause) {
        return;
    }
    std::lock_guard lock(g_mutex);
    for (Wrapped &w : g_wrapped) {
        if (w.address == 0u) {
            w = {address, current};
            runtime.replaceFunction(address, &vsync_callback_with_cause);
            std::fprintf(stderr, "[kessen2] vsync: callback 0x%08x gets a0=INTC_VBLANK_S\n", address);
            return;
        }
    }
    std::fprintf(stderr, "[kessen2] vsync: too many callbacks, 0x%08x not wrapped\n", address);
}

void sync_v_callback(std::uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    wrap_callback(*runtime, GPR_U32(ctx, 4));
    ps2_stubs::sceGsSyncVCallback(rdram, ctx, runtime);
}

} // namespace

int fix_gs_vsync_callback_cause(PS2Runtime &runtime)
{
    return bind_hle<&sync_v_callback>(runtime, kSceGsSyncVCallback, "sceGsSyncVCallback (cause)");
}

} // namespace k2::runtime_ext::fixes
