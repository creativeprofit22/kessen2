# tools/disc — disc image tooling (`k2disc`)

**Purpose.** Read the user's own Kessen II disc image (ISO9660) and extract what later stages need into the untracked `work/` directory. That covers the boot ELF named in `SYSTEM.CNF` (serial + CRC32 for PS2Recomp), the IOP module list, a file-type catalogue and the `LINKDATA.BNS` container members. Usage and output layout: [docs/modules/disc.md](../../docs/modules/disc.md). Decisions: [ADR-0005](../../docs/adr/0005-disc-tooling-in-cpp.md).

**Inputs.** Path to a user-supplied, legally owned disc image (kept *outside* the repo or in `work/`), or a directory/container already extracted by `k2disc`.

**Outputs.** Files under `work/disc/<SERIAL>/` (ISO tree, `manifest.tsv`, `archives/…`, `index.tsv`), plus TSV/Markdown reports on stdout. All of it is git-ignored and never committed.

**Targets.** `k2_disc_core` (static library: parsers and extractors, C++23 for `std::expected`) and `k2disc` (CLI: argument parsing and output only). Tests live in `tools/disc/tests/` and are registered from `tests/CMakeLists.txt`.

**Allowed dependencies.** C++ standard library, CMake. Host tools only; no link to runtime modules.

**Forbidden.** Writing anything outside `work/` by default; committing any extracted file or real-data fixture; network access; linking `ps2_runtime`, `k2_*` runtime modules or generated code.
