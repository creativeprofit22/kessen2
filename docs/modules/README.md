# Module index

Each module's README has the same sections: Purpose / Inputs / Outputs / Allowed dependencies / Forbidden. The dependency rule is enforced by `cmake/K2Modules.cmake`; see `../ARCHITECTURE.md`.

| Module | Target | README |
|---|---|---|
| Disc tooling | `k2_disc_core`, `k2disc` | [tools/disc](../../tools/disc/README.md) · usage: [disc.md](disc.md) |
| Analysis inputs | _(data + scripts)_ | [analysis](../../analysis/README.md) · usage: [analysis.md](analysis.md) |
| Generated code | `k2_generated` (conditional) | [generated](../../generated/README.md) |
| Runtime extensions | `k2_runtime_ext` | [runtime-ext](../../runtime-ext/README.md) |
| Renderer | `k2_render` | [render](../../render/README.md) |
| Platform | `k2_platform` | [platform](../../platform/README.md) |
| App (composition root) | `kessen2` | [app](../../app/README.md) |
| Guardrails | _(scripts)_ | [tools/guard](../../tools/guard/README.md) |
