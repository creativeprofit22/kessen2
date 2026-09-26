# runtime-ext — Kessen II–specific runtime extensions

**Purpose.** Game-specific glue for PS2Recomp's runtime: overrides registered via `PS2_REGISTER_GAME_OVERRIDE` (see `external/PS2Recomp/ps2xRuntime/include/game_overrides.h`), syscall/stub bindings, patches. This is where Kessen II–specific runtime logic lives — never inside the submodule.

**Inputs.** `ps2_runtime` public API.

**Outputs.** Static library `k2_runtime_ext`, linked by the app.

**Allowed dependencies.** `ps2_runtime`.

**Forbidden.** `k2_platform`, `k2_render`, `k2_generated` and generated headers (bind to guest code by *address* through the runtime, not by including generated symbols); SDL.
