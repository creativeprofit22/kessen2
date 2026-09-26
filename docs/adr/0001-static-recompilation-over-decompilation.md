# ADR-0001: Static recompilation over decompilation

- **Status:** Accepted
- **Date:** 2026-09-26

## Context

We want Kessen II running natively on modern PCs. The two established approaches are a matching **decompilation** (hand-rewriting every function into C that recompiles to the original bytes) and **static recompilation** (mechanically translating the game's MIPS R5900 code to C++ and linking it against a runtime that emulates the hardware surface).

## Decision

Use static recompilation with PS2Recomp (`ps2_recomp` + `ps2_runtime`). Human effort goes into analysis (function boundaries, config in `analysis/`), a game-specific runtime extension layer (`runtime-ext/`) and a host renderer/platform.

## Consequences

- Playable results long before every function is understood; a decompilation of a PS2 title is multi-year work.
- Generated code is a mechanical translation of the game and must stay local ([LEGAL.md](../LEGAL.md)); the repo ships only the pipeline.
- Correctness depends on PS2Recomp's runtime fidelity (GS, VU, IOP). Fixes that are general go upstream; Kessen II–specific ones go in `runtime-ext/`.
- Mods/enhancements work at the address/override level rather than on readable source.

## Alternatives considered

- **Matching decompilation** — best long-term moddability, but far too slow for a small team and needs a period-accurate compiler toolchain.
- **Emulator (PCSX2) with patches** — already exists; not a native port and offers no path to engine-level improvements.
