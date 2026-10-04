// SPDX-License-Identifier: GPL-3.0-or-later
// BRINGUP.md #3: Kessen II loads code overlays from LINKDATA.BNS with sceCdRead.
// - The fixed "main" overlay (0x5A4800) is recompiled from work/recomp/*.combined.elf
//   (tools/recomp/make-combined-elf.sh). After the game reads it, check its header so a
//   different disc image cannot silently run mismatched recompiled code.
// - Overlays read into the swap slots (0x7F1800, 0x860000) are not recompiled yet
//   (needs per-overlay dispatch upstream); log each load so the blocker is visible.
// To log reads (first 1,024 in full), load probes/cd-reads.probe (K2_PROBE): it wraps this guard.
#include "fixes.h"
#include "hle_bind.h"

#include "ps2_stubs.h"

#include <cstdio>
#include <cstring>

namespace k2::runtime_ext::fixes {
namespace {

constexpr std::uint32_t kSceCdRead = 0x00114F40u;
constexpr std::uint32_t kMainOverlay = 0x005A4800u;
// Swap-slot ranges come from the retail ELF (SLUS_202.75) NOLOAD overlay segments:
// largest memsz at 0x7F1800 is 0x6E800 (ends exactly at slot B), at 0x860000 it is 0xFD000.
// Match whole ranges so chunked loads that do not start at the slot base are logged too.
constexpr std::uint32_t kSwapSlotA = 0x007F1800u;
constexpr std::uint32_t kSwapSlotAEnd = 0x00860000u;
constexpr std::uint32_t kSwapSlotB = 0x00860000u;
constexpr std::uint32_t kSwapSlotBEnd = 0x0095D000u;

// "MWo3" header of the overlay recompiled into generated/ (see make-combined-elf.sh).
constexpr std::uint32_t kMagic = 0x336F574Du;
constexpr std::uint32_t kMainTextSize = 0x000CBF40u;
constexpr std::uint32_t kMainDataSize = 0x00005180u;

std::uint32_t guest_u32(const std::uint8_t *rdram, std::uint32_t address)
{
    std::uint32_t value = 0;
    std::memcpy(&value, rdram + (address & PS2_RAM_MASK), sizeof(value));
    return value;
}

void check_main_overlay(const std::uint8_t *rdram)
{
    const std::uint32_t magic = guest_u32(rdram, kMainOverlay);
    const std::uint32_t load = guest_u32(rdram, kMainOverlay + 8);
    const std::uint32_t text = guest_u32(rdram, kMainOverlay + 12);
    const std::uint32_t data = guest_u32(rdram, kMainOverlay + 16);
    if (magic != kMagic || load != kMainOverlay || text != kMainTextSize || data != kMainDataSize) {
        std::fprintf(stderr,
                     "[kessen2] overlay: MISMATCH at 0x%08x (magic=0x%08x load=0x%08x text=0x%x "
                     "data=0x%x); recompiled code does not match this disc\n",
                     kMainOverlay, magic, load, text, data);
        return;
    }
    std::fprintf(stderr, "[kessen2] overlay: main overlay loaded at 0x%08x (recompiled)\n",
                 kMainOverlay);
}

void guarded_cd_read(std::uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const std::uint32_t lsn = GPR_U32(ctx, 4);
    const std::uint32_t sectors = GPR_U32(ctx, 5);
    const std::uint32_t buf = GPR_U32(ctx, 6) & PS2_RAM_MASK;
    ps2_stubs::sceCdRead(rdram, ctx, runtime);

    if (buf == kMainOverlay) {
        check_main_overlay(rdram);
    } else if ((buf >= kSwapSlotA && buf < kSwapSlotAEnd) || (buf >= kSwapSlotB && buf < kSwapSlotBEnd)) {
        std::fprintf(stderr,
                     "[kessen2] overlay: load into swap slot 0x%08x (lsn=0x%x sectors=0x%x) is "
                     "not recompiled\n",
                     buf, lsn, sectors);
    }
}

} // namespace

int fix_cd_overlay_guard(PS2Runtime &runtime)
{
    return bind_hle<&guarded_cd_read>(runtime, kSceCdRead, "sceCdRead (overlay guard)");
}

} // namespace k2::runtime_ext::fixes
