# app — composition root (`kessen2` executable)

**Purpose.** The *only* target allowed to link everything: wires `k2_platform`, `k2_render`, `k2_runtime_ext`, `ps2_runtime` and (when present) `k2_generated` into the `kessen2` executable. Replaces upstream `ps2EntryRunner`, which globs generated code from inside the submodule.

**Inputs.** All module libraries.

**Outputs.** `kessen2.exe` (+ runtime DLLs copied beside it).

**Allowed dependencies.** Every module and guarded external.

**Forbidden.** Business logic — keep it thin; logic belongs in the modules.
