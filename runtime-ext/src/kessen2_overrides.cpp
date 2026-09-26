// SPDX-License-Identifier: GPL-3.0-or-later
// Kessen II game-specific runtime overrides.
//
// The override is keyed by the retail boot ELF (name + entry point + CRC32) and is applied by
// PS2Recomp's loader (ps2_game_overrides::applyMatching) right after the ELF is loaded.
// Each fix lives in its own function under src/fixes/ with a link to its docs/BRINGUP.md row.
// Fixes bind guest addresses to runtime handlers; they must not include generated code.
//
// Registration must stay in this translation unit: override_count() is referenced by the app,
// which keeps the static AutoRegister object below from being dropped by the linker.
#include "k2/runtime_ext.h"

#include <cstdint>

namespace k2::runtime_ext {

// Retail SLUS-20275 boot ELF identity (not secret; printed by the runtime at load time).
inline constexpr const char *kKessen2ElfName = "SLUS_202.75";
inline constexpr std::uint32_t kKessen2Entry = 0x00100008u;
inline constexpr std::uint32_t kKessen2Crc32 = 0xBAFBDE3Fu;

} // namespace k2::runtime_ext

#if K2_HAS_GAME_OVERRIDE
#include "fixes/fixes.h"
#include "game_overrides.h"

#include <cstdio>
#include <type_traits>

static_assert(std::is_same_v<ps2_game_overrides::ApplyFn, void (*)(PS2Runtime &)>,
              "PS2Recomp game override API changed");

namespace {

void applyKessen2(PS2Runtime &runtime)
{
    // Upstream logs matches only when PS2_RUNTIME_LOGS is on; always report ours so the boot
    // smoke test and BRINGUP.md runs can see that the override was actually applied.
    std::fprintf(stderr, "[kessen2] game override matched: %s entry=0x%08x crc32=0x%08X\n",
                 k2::runtime_ext::kKessen2ElfName, k2::runtime_ext::kKessen2Entry,
                 k2::runtime_ext::kKessen2Crc32);
    const int applied = k2::runtime_ext::fixes::apply_all(runtime);
    std::fprintf(stderr, "[kessen2] applied %d fix(es)\n", applied);
}

} // namespace

PS2_REGISTER_GAME_OVERRIDE("Kessen II (SLUS-20275)", k2::runtime_ext::kKessen2ElfName,
                           k2::runtime_ext::kKessen2Entry, k2::runtime_ext::kKessen2Crc32,
                           applyKessen2)

namespace k2::runtime_ext {
int override_count() { return 1; }
} // namespace k2::runtime_ext

#else

namespace k2::runtime_ext {
int override_count() { return 0; }
} // namespace k2::runtime_ext

#endif
