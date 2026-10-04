// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixes.h"

namespace k2::runtime_ext::fixes {

int apply_all(PS2Runtime &runtime)
{
    // BRINGUP.md order.
    int applied = 0;
    applied += fix_cd_disk_ready(runtime);
    applied += fix_cd_overlay_guard(runtime);
    applied += fix_gs_vsync_callback_cause(runtime);
    applied += fix_frame_wait_idle(runtime);
    return applied;
}

} // namespace k2::runtime_ext::fixes
