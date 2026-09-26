// SPDX-License-Identifier: GPL-3.0-or-later
// Routes an unnamed guest function to one of PS2Recomp's HLE handlers (ps2_stubs::*).
#pragma once

#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"

#include <cstdint>
#include <cstdio>

namespace k2::runtime_ext::fixes {

// Same shape as the wrappers ps2_recomp emits for [general] stubs: return to $ra, then run
// the HLE handler (which sets $v0).
template <PS2Runtime::RecompiledFunction Handler>
void hle_return_to_ra(std::uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    ctx->pc = GPR_U32(ctx, 31);
    Handler(rdram, ctx, runtime);
}

// Replaces the recompiled function at `address` with `Handler`. Returns 1 on success, else
// logs and returns 0 (so apply_all's count shows a missing fix instead of failing silently).
template <PS2Runtime::RecompiledFunction Handler>
int bind_hle(PS2Runtime &runtime, std::uint32_t address, const char *name)
{
    if (!runtime.replaceFunction(address, &hle_return_to_ra<Handler>)) {
        std::fprintf(stderr, "[kessen2] fix: could not bind %s at 0x%08x\n", name, address);
        return 0;
    }
    return 1;
}

} // namespace k2::runtime_ext::fixes
