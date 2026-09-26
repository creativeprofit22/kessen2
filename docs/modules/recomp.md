# Recompilation — `ps2_recomp` → `generated/`

Kessen II keeps most of its game code in **overlays** inside `LINKDATA.BNS`, not in the boot ELF (see [BRINGUP.md](../BRINGUP.md) #3). The fixed main overlay is recompiled together with the ELF; the swappable overlays are not yet (needs upstream support).

Full pipeline, from an extracted disc:

```sh
sh tools/recomp/make-combined-elf.sh                                    # ~15 s
sh analysis/run-analysis.sh                                             # ~2.5 min, uses the combined ELF
sh tools/recomp/run-recomp.sh [analysis/kessen2.toml]                    # ~1 min
cmake --preset msvc-x64 && cmake --build --preset msvc-x64-debug --parallel
```

## `make-combined-elf.sh`

Copies the retail boot ELF to `work/recomp/SLUS_202.75.combined.elf` (git-ignored game data) and writes the main overlay (`LINKDATA.BNS` sector `0xFB8B`, `0x1A3` sectors, `MWo3` header, loaded at `0x5A4800`) into the ELF's empty program segment for that address. It checks the overlay header first and fails if the disc differs. The runtime still boots the **unmodified** ELF; the game reads the overlay itself, and runtime-ext checks the header after that read.

## `run-recomp.sh`

1. Builds `ps2_recomp` (PS2Recomp submodule pin, Release) into `out/build/ps2recomp-tools` if it is missing. Override the binary with `K2_PS2_RECOMP`; a set but non-executable override fails immediately instead of triggering the build.
2. Refuses to run unless the TOML's `[general] output` is `<repo>/generated` or a path outside the repository (same rule as `analysis/run-analysis.sh` and the root `CMakeLists.txt`).
3. Refuses to run unless the TOML's `[general] input` is `SLUS_202.75.combined.elf`. A retail-ELF TOML (from `K2_ELF=<retail ELF> sh analysis/run-analysis.sh`) would silently drop the main overlay: 2,385 instead of 4,474 functions and a crash at the call `0x14ce9c → 0x627820` around frame 509 ([BRINGUP.md](../BRINGUP.md) #3). Set `K2_ALLOW_RETAIL_ELF=1` to override.
4. Deletes stale `*.cpp`/`*.h` in the output folder (the tracked `README.md` stays), runs `ps2_recomp`, and logs file count and elapsed time. Full tool log: `analysis/out/reports/ps2_recomp.log`.

Everything written is git-ignored. Reconfigure CMake after each run so `k2_generated` picks up the new file list.

## Output shape (pin `75d729ce`, 2026-09-26)

| Item | Boot ELF only | ELF + main overlay |
|---|---|---|
| Functions / callable code labels | 2,385 / 12,713 | 4,474 / 29,450 |
| Generated `.cpp` files | 15,099 | 33,923 |
| Stubs (runtime-provided SDK calls) | 297 | 297 |
| Unhandled instructions / errors | 0 / 0 | 0 / 0 |
| Unresolved `JR`/`JALR` warnings (fallback dispatch) | 221 | 336 |

Every generated function name carries its address suffix (`printf_0x…`), so SDK names that occur twice in the ELF do not clash.

`register_functions.cpp` fills `g_ps2RecompiledFunctionTable`, which `ps2_runtime` references; the table is how the runtime dispatches guest addresses to host functions.

## Build notes

`k2_generated` is a unity build (batches of 64 files, `register_functions.cpp` excluded) with a precompiled header of the runtime headers, `/bigobj` and `/MP` (MSBuild hands every batch to a single `cl.exe`; without `/MP` it compiles them one by one). See the root `CMakeLists.txt`.

A single oversized generated function makes one batch take tens of minutes; check `ls -S generated | head` if a build stalls. The known cause (Ghidra folding a far tail-jump target into a function body) is fixed during analysis by `K2SplitFarChunks.java`.
