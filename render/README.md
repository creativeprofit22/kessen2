# render — host renderer for the PS2 GS/VU output

**Purpose.** Turn the runtime's GS/VU output into host GPU draws (future replacement for the upstream CPU/raylib path).

**Inputs.** `ps2_runtime` GS raster backend / VU interfaces; windowing and GPU context from `k2_platform`.

**Outputs.** Static library `k2_render`.

**Allowed dependencies.** `ps2_runtime`, `k2_platform`.

**Forbidden.** `k2_generated` or any generated header; `k2_runtime_ext`; game-specific logic (that goes in `runtime-ext/`).

_Status: stub (Phase 1)._
