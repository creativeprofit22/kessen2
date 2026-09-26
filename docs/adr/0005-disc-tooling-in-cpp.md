# ADR-0005: Disc tooling in C++ (ISO9660 first, no new dependencies)

- **Status:** Accepted
- **Date:** 2026-09-26

## Context

Phase 2 needs a host tool (`tools/disc`, binary `k2disc`) that reads the user's own Kessen II disc image, extracts it into the git-ignored `work/` tree, identifies the boot ELF (serial + CRC32 for PS2Recomp's `PS2_REGISTER_GAME_OVERRIDE`) and IOP modules, catalogues file types, and parses the game's main data container. The roadmap allowed C++20 or Rust.

## Decision

- **C++**, built by the existing CMake superbuild, presets and CI. No second toolchain.
- The disc targets (`k2_disc_core`, `k2disc`, disc tests) set `CXX_STANDARD 23` per target to get `std::expected` for expected failures; the rest of the repo stays on C++20. MSVC 17.x (windows-2022 runner) provides `std::expected` under `/std:c++latest`. If a CI toolchain ever rejects it, swap in a local `Result` type with the same surface and record it here.
- **ISO9660 primary volume** is the filesystem read. PS2 DVDs are ISO9660/UDF bridge discs whose two trees describe the same files and every file is < 4 GiB, so ISO9660 (with multi-extent records) is sufficient. The tool only *detects* the UDF `NSR02/NSR03` descriptor and reports it. A UDF reader is added only if the ISO9660 tree is ever shown to be incomplete.
- **No new dependencies.** CRC-32, ISO9660, `SYSTEM.CNF`, IRX (`.iopmod`) and the container parser are small and written in-house. Capstone is not pulled in unless container RE requires disassembly.
- CRC-32 is the standard reflected CRC-32 (poly `0xEDB88320`, init/xorout `0xFFFFFFFF`) over the whole ELF file — identical to PS2Recomp's `computeFileCrc32`.
- Tests are plain ctest executables with a tiny in-repo check helper. Every fixture (ISO images, IRX ELFs, containers) is **synthesised in memory by test code**; no binary fixture is committed.

## Consequences

- One build, one CI; the tool is always compiled and tested with the rest of the repo.
- The disc image is untrusted input: all offsets/sizes are bounds-checked and extracted paths are sanitised and contained under the output root.
- Mixed C++20/C++23 in one build is fine because `k2_disc_core` is a leaf module that nothing else links.

## Alternatives considered

- **Rust** — good fit for parsing untrusted input, but adds a second toolchain to CI and the Windows build for a small tool.
- **Existing extractors (7-Zip, `isoinfo`, PS2 community tools)** — not scriptable into our build uniformly, and none knows the game's container format.
- **Full UDF reader up front** — redundant on a bridge disc; deferred until evidence requires it.
