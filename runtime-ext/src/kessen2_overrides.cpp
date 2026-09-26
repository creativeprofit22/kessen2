// SPDX-License-Identifier: GPL-3.0-or-later
// Kessen II game-specific runtime overrides.
//
// Overrides are registered with PS2_REGISTER_GAME_OVERRIDE(name, elfName, entry, crc32, applyFn)
// from PS2Recomp's game_overrides.h and bind guest addresses to runtime handlers — they must
// not include generated code. None are registered yet: the boot ELF name/entry/CRC are
// identified in a later phase.
#include "k2/runtime_ext.h"

#if K2_HAS_PS2_RUNTIME
#include "game_overrides.h"
#include <type_traits>
static_assert(std::is_same_v<ps2_game_overrides::ApplyFn, void (*)(PS2Runtime &)>,
              "PS2Recomp game override API changed");
#endif

namespace k2::runtime_ext {

int override_count() { return 0; }

} // namespace k2::runtime_ext
