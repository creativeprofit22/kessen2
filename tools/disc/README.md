# tools/disc — disc image tooling

**Purpose.** Read the user's own Kessen II disc image (ISO9660) and extract what later stages need — chiefly the boot ELF named in `SYSTEM.CNF` — into the untracked `work/` directory.

**Inputs.** Path to a user-supplied, legally owned disc image (kept *outside* the repo or in `work/`).

**Outputs.** Files under `work/` (boot ELF, IRX modules, raw asset files). All of it is git-ignored and never committed.

**Allowed dependencies.** C++ standard library, CMake. Host tools only; no link to runtime modules.

**Forbidden.** Writing anything outside `work/` by default; committing any extracted file; network access; linking `ps2_runtime`, `k2_*` modules or generated code.

_Status: placeholder (Phase 1). No targets yet._
