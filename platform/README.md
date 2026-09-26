# platform — host OS abstraction (SDL3)

**Purpose.** Window, input, audio device, timing and filesystem paths on the host, wrapped behind `include/k2/platform.h`.

**Inputs.** SDL3 (pinned in `docs/DEPENDENCIES.md`).

**Outputs.** Static library `k2_platform`.

**Allowed dependencies.** SDL3 only.

**Forbidden.** `ps2_runtime`, `k2_render`, `k2_runtime_ext`, `k2_generated`, generated headers, anything game-specific.
