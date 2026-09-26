// SPDX-License-Identifier: GPL-3.0-or-later
// BRINGUP.md #6: the main loop (FUN_0014d6e0) waits for the end of a frame by spinning on
//     do { v0 = get_frame_busy(); /* 0x14D960, reads gp-0x6B60 */ } while (v0 != 0);
// until the VSync handler (0x0014D590) clears the flag. PS2Recomp paces guest VBLANK by
// accounted EE cycles (~4.9M per frame) and charges ~40 cycles per spin iteration, so a
// recompiled spin burns host CPU for a long time per emulated frame (seconds in Debug).
//
// Idle-loop fix (same idea as PCSX2's idle-loop detection): when get_frame_busy() is called
// from that loop and the flag is still set, block the thread until the next VBLANK and resume
// at the poll itself (0x14D754), so the flag is re-read. Other callers are unchanged.
#include "fixes.h"

#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace k2::runtime_ext::fixes {
namespace {

constexpr std::uint32_t kGetFrameBusy = 0x0014D960u;
constexpr std::uint32_t kIdlePollCall = 0x0014D754u; // jal get_frame_busy in the main loop
constexpr std::uint32_t kIdlePollReturn = kIdlePollCall + 8u;
constexpr std::int32_t kFrameBusyGpOffset = -0x6B60;

PS2Runtime::RecompiledFunction g_original = nullptr;

void get_frame_busy_idle(std::uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    if (GPR_U32(ctx, 31) == kIdlePollReturn) {
        const std::uint32_t flag_addr =
            (GPR_U32(ctx, 28) + static_cast<std::uint32_t>(kFrameBusyGpOffset)) & PS2_RAM_MASK;
        std::uint32_t busy = 0;
        std::memcpy(&busy, rdram + flag_addr, sizeof(busy));
        if (busy != 0u) {
            runtime->eeWaitVSyncTicks(1u, kIdlePollCall); // does not return
        }
    }
    g_original(rdram, ctx, runtime);
}

} // namespace

int fix_frame_wait_idle(PS2Runtime &runtime)
{
    g_original = runtime.lookupFunction(kGetFrameBusy);
    if (!runtime.hasFunction(kGetFrameBusy) || g_original == nullptr ||
        !runtime.replaceFunction(kGetFrameBusy, &get_frame_busy_idle)) {
        std::fprintf(stderr, "[kessen2] fix: could not bind frame-wait idle at 0x%08x\n",
                     kGetFrameBusy);
        return 0;
    }
    return 1;
}

} // namespace k2::runtime_ext::fixes
