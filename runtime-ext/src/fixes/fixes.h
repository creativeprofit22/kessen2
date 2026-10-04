// SPDX-License-Identifier: GPL-3.0-or-later
// Kessen II bring-up fixes. One function per fix, each in its own file under src/fixes/,
// each documented by a row in docs/BRINGUP.md.
#pragma once

class PS2Runtime;

namespace k2::runtime_ext::fixes {

// Applies every fix to a freshly loaded runtime; returns how many were applied.
int apply_all(PS2Runtime &runtime);

// Individual fixes (BRINGUP.md row in each file). Each returns 1 if applied, else 0.
int fix_cd_disk_ready(PS2Runtime &runtime);
int fix_cd_overlay_guard(PS2Runtime &runtime);
int fix_gs_vsync_callback_cause(PS2Runtime &runtime);
int fix_frame_wait_idle(PS2Runtime &runtime);

// Diagnostics are not fixes: use a probe file (K2_PROBE, probes/README.md, ADR-0006).

} // namespace k2::runtime_ext::fixes
