# generated — recompiler output (never committed)

**Purpose.** Destination for C++ emitted by `ps2_recomp` from the user's own boot ELF. Only this README is tracked; everything else here is git-ignored and rejected by `tools/guard/check-forbidden.sh`.

**Inputs.** `analysis/` config + boot ELF from `work/`.

**Outputs.** `*.cpp` / `*.h` compiled into the `k2_generated` target, which the root `CMakeLists.txt` only creates when sources exist here. Location is overridable with `-DKESSEN2_GENERATED_DIR=...`. Override only with a path outside the repository — configure fails for any other in-tree path (even git-ignored ones like `out/` or `build/`), since only this folder is guarded.

**Allowed dependencies.** `ps2_runtime`.

**Forbidden.** Being committed or redistributed; being included by `platform/`, `render/` or `runtime-ext/` (enforced by CMake and the guard script). Only the app target links it.
