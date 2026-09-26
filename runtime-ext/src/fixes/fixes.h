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

// Diagnostics (no behaviour change; off unless their environment variable is set).
int fix_trace_funcs(PS2Runtime &runtime); // K2_TRACE_FUNCS=0xADDR[,0xADDR...]

} // namespace k2::runtime_ext::fixes
