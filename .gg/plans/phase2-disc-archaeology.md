# Phase 2 — Disc archaeology: ISO extraction and asset format map

## Audit (current state, 2026-09-26)

- `tools/disc/` holds only a placeholder `CMakeLists.txt` and a README (Purpose/Inputs/Outputs/Allowed/Forbidden). No targets, no tests, no `docs/formats/`, no `docs/modules/disc.md`.
- No progress, blockers or verification recorded for this phase. **All 6 doneWhen criteria are unmet.**
- Existing infrastructure to reuse: `cmake/K2Modules.cmake` (`k2_add_module`), `tests/CMakeLists.txt` (ctest, no unit-test framework yet), `.gitignore` already ignores `work/`, `*.elf`, `*.irx`, `SLPM_*`/`SLUS_*`, `*.bin`; guard script rejects ELF/ISO9660 content and `work/` paths in the index.
- PS2Recomp's ELF CRC (`ps2xRuntime/src/lib/ps2_runtime.cpp` `computeFileCrc32`) is standard reflected CRC-32 (poly `0xEDB88320`, init/xorout `0xFFFFFFFF`) over the **whole ELF file** — ours must match exactly.
- ISO present: `E:\Descargas\Kessen II\Kessen II.iso` (4.1 GB, DVD). Plan mode can't read its bytes, so the actual container format is **unknown until implementation**; the RE steps below are exploratory by design.

## Decisions

- **Language: C++** (ADR-0005). Same toolchain/CMake/presets/CI as the rest of the repo; no second build system. Disc targets set `CXX_STANDARD 23` per target to get `std::expected` (MSVC 17.x supports it under `/std:c++latest`); the rest of the repo stays on C++20. Fallback if the CI toolchain rejects it: `tl::expected`-style local `Result` — decide at first build, record in the ADR.
- **Filesystem: ISO9660 primary volume** (with multi-extent support). PS2 DVDs are ISO9660+UDF bridge discs where both trees describe the same files; files are < 4 GB so ISO9660 suffices. The tool detects and reports the UDF `NSR0x` descriptor, and cross-checks no UDF-only files exist is **out of scope** unless the ISO9660 tree turns out incomplete (then add a minimal UDF reader — noted as a risk).
- **No new dependencies.** CRC32, ISO9660, SYSTEM.CNF, IRX parsing are small and written in-house. Capstone only if container RE needs ELF disassembly for a TOC; prefer reading data tables first.
- **Tests: plain ctest executables** with a tiny in-repo check helper (no Catch2/doctest fetch). All fixtures (mini ISO images, synthetic containers) are **built in memory by test code** — no fixture files committed, so nothing can trip the guard's ISO9660/ELF magic check or the `*.bin` ignore rule.
- **Untrusted input:** the ISO and archives are untrusted. Every offset/size is bounds-checked against the image/file size; extracted names are sanitized (reject `..`, absolute paths, drive letters, NUL/control chars, strip `;1`) and the final path must stay under the output root. Default output root is `<repo>/work/disc/<SERIAL>/`; writing elsewhere requires explicit `--out`.
- **Deterministic output:** directory listings, manifests and catalogues sorted by path; no timestamps in manifests.

## CLI design (`k2disc`)

```
k2disc info    <iso>                         volume ID, UDF present?, SYSTEM.CNF BOOT2/VER, serial, boot ELF size + CRC32 (hex)
k2disc extract <iso> [--out DIR]             full tree -> DIR/<SERIAL>/iso/...; writes DIR/<SERIAL>/manifest.tsv (path, lba, size, crc32)
k2disc irx     <iso|extracted-dir>           every *.IRX: path, size, IOP module name+version from .iopmod section
k2disc scan    <extracted-dir> [--markdown]  catalogue by extension + magic: count, total bytes, sample paths
k2disc archive list    <container> [...]     members: index, offset, size, (compressed?) , detected magic
k2disc archive extract <container> [--out DIR]
```

Exit codes: 0 ok, 1 input/format error (message names the offset and reason), 2 usage.

Output layout (all git-ignored):
```
work/disc/<SERIAL>/iso/…            raw ISO tree
work/disc/<SERIAL>/manifest.tsv
work/disc/<SERIAL>/archives/<CONTAINER>/NNNNN.<ext-by-magic>
```

## Code layout

```
tools/disc/
  CMakeLists.txt          k2_disc_core (STATIC) + k2disc (exe); k2_add_module(k2_disc_core) with no ALLOWS
  src/bytes.hpp           bounds-checked little-endian reads over std::span
  src/crc32.{hpp,cpp}
  src/iso9660.{hpp,cpp}   PVD, path/dir records, multi-extent, UDF detection, safe path join
  src/system_cnf.{hpp,cpp}  BOOT2 = cdrom0:\SLxx_nnn.nn;1  -> elf name + serial "SLxx-nnnnn"
  src/irx.{hpp,cpp}       ELF32 LE, .iopmod section -> module name/version
  src/catalogue.{hpp,cpp} magic table (ELF, IRX, PSS/MPEG-PS 00 00 01 BA, VAG "VAGp", TIM2 "TIM2", SShd/SSbd, TOC-style, …) + aggregation
  src/archive_<name>.{hpp,cpp}  the reverse-engineered main container (name fixed after RE)
  src/main.cpp            argument parsing and output only
tools/disc/tests/         test_crc32, test_system_cnf, test_iso9660, test_irx, test_catalogue, test_archive (+ test_support.hpp builders)
```
Tests registered from `tests/CMakeLists.txt` via `add_subdirectory` of the disc test dir (keeps the existing single ctest entry point).

## Container reverse-engineering method

1. `k2disc extract` + `k2disc scan` on the real ISO; rank files by size. Kessen II is a Koei title: expect a few huge data files (Koei `LINKDATA`/`*.BIN`+index style), MPEG-PS movies, and small index files.
2. For the top unknown container: hex-inspect header and look for (a) a header TOC (count + offset/size table), (b) a companion index file, (c) a TOC embedded in the ELF (search ELF `.data` for monotonically increasing sector offsets matching the container size). Validate the hypothesis by checking every member lands inside the file, members tile without overlap, and member magics become recognisable.
3. Detect per-member compression (Koei LZ variants are common); if members are compressed, document the header and, if feasible within the phase, implement decompression — otherwise mark "extractable (compressed payload), codec unknown".
4. Write `docs/formats/<container>.md` (byte-level spec table, evidence, confidence, open questions) and implement `archive list/extract`.
5. Re-run `scan` over extracted members to fill `docs/formats/INDEX.md`; anything not identified after the main container is extractable is marked **unknown** and left.

Only structural facts (names, sizes, magic bytes, field layouts) go into docs — never file contents, dumps or extracted data.

## Risks

- Container TOC lives in the ELF → requires ELF section/address mapping (still no disassembly needed if the table is found by value search).
- Unknown compression could block "extract members" in usable form; criterion requires list + extract of members, which raw extraction satisfies — decompression is best-effort, documented.
- `std::expected` availability on the windows-2022 runner MSVC → fallback noted above.
- If the ISO9660 tree omits files that UDF shows → add UDF reader (extra work, flagged).

## Verification

- `cmake --preset msvc-x64` → `cmake --build --preset msvc-x64-debug --parallel` → `ctest --preset msvc-x64`: all new disc tests + existing tests pass.
- Real-disc run (local only): `k2disc info` prints serial, boot ELF name, CRC32; cross-check CRC with `certutil`/`crc32` or Python `zlib.crc32` on the extracted ELF. `k2disc irx` lists all IRX. `k2disc archive list` totals match container size.
- `sh tools/guard/check-forbidden.sh --all` passes; `git status` shows nothing under `work/`.
- Docs exist: ADR-0005, `docs/formats/INDEX.md`, `docs/formats/<container>.md`, `docs/modules/disc.md`; module index + `tools/disc/README.md` updated.

## Steps

1. Report phase `in-progress` via roadmap_status.
2. Write `docs/adr/0005-disc-tooling-in-cpp.md` (C++ over Rust, per-target C++23 for `std::expected`, no new deps, ISO9660-first).
3. Add `tools/disc` CMake targets `k2_disc_core` + `k2disc`, register with `k2_add_module`, and a minimal test harness wired from `tests/CMakeLists.txt`; confirm configure/build on MSVC.
4. Implement `bytes.hpp` bounds-checked readers and `crc32` (PS2Recomp-compatible) with tests (`"123456789"` → `0xCBF43926`, empty, chunked == one-shot).
5. Implement ISO9660 reader (PVD, directory records, multi-extent, UDF descriptor detection, safe name sanitation/containment) with tests over in-memory synthetic ISO images, including traversal-name and out-of-range-extent rejection.
6. Implement SYSTEM.CNF parser (BOOT2 path → ELF name, serial, VER) with tests for SLUS/SLPM/SLES forms, CRLF, missing key.
7. Implement `k2disc info` and `k2disc extract` (default `work/disc/<SERIAL>/`, manifest.tsv sorted); run on the real ISO and record serial/ELF/CRC32 (cross-checked with an independent CRC tool).
8. Implement IRX parser (.iopmod name/version) and `k2disc irx`, with a synthetic IRX-ELF test; run on the real disc and record the IRX list in `docs/formats/INDEX.md` (IOP modules section).
9. Implement magic catalogue + `k2disc scan --markdown` with tests; run over the extracted tree to produce the first draft of `docs/formats/INDEX.md` (extension/magic, count, total size, status).
10. Reverse-engineer the main container per the method above; write `docs/formats/<container>.md` with a byte-level spec, evidence and open questions.
11. Implement the container parser + `k2disc archive list/extract`, with synthetic-fixture tests (valid, truncated, overlapping/out-of-range entries, zero members, compressed-flag handling).
12. Extract container members on the real disc, re-run `scan` on them, and finalise `docs/formats/INDEX.md` marking every type identified / partially understood / unknown.
13. Write `docs/modules/disc.md` (usage, commands, output layout, safety rules) and update `tools/disc/README.md` and `docs/modules/README.md`.
14. Full build + ctest + `check-forbidden.sh --all`; confirm no game data is staged; report verification evidence via roadmap_status and ask the user before marking Done.
