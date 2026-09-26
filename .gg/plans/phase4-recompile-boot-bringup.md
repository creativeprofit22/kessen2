# Phase 4 — Recompile and boot bring-up

## Audit (current state, 2026-09-26, HEAD `ac1a0f9`)

Nothing for this phase exists yet. Status: **not started**. Every doneWhen criterion is unmet:

| Criterion | State | Evidence |
|---|---|---|
| `ps2_recomp` on `analysis/kessen2.toml` → `generated/` builds + links with ps2xRuntime + runtime-ext | ❌ | `generated/` holds only `README.md`. `ps2_recomp` never built (DEPENDENCIES.md: "Not yet verified; do so in Phase 4"). `out/build/ps2recomp-tools` exists (configured with `PS2X_BUILD_RECOMP=ON`, only `ps2_analyzer` built). |
| Override via `PS2_REGISTER_GAME_OVERRIDE` with real ELF name/entry/CRC32 | ❌ | `runtime-ext/src/kessen2_overrides.cpp` registers nothing; `override_count()` returns 0. Known: ELF `SLUS_202.75`, entry `0x00100008`. CRC32 not computed yet. |
| Title screen reached, or furthest point documented | ❌ | `app/src/main.cpp` never creates a `PS2Runtime`; prints "game boot is not implemented yet". |
| `docs/BRINGUP.md` blocker log | ❌ | File missing. |
| Headless N-frame smoke test | ❌ | No test; upstream `run()` always opens a raylib window. |

Ready inputs: `analysis/kessen2.toml` (297 stubs, 33 untracked stubs, 2,385 functions, `patch_syscalls=false`), `analysis/out/kessen2_functions.csv`, extracted disc at `work/disc/SLUS-20275/iso/` (ELF + `LINKDATA.*` + `MODULES/`), superbuild already wires `k2_generated` (globs `generated/*.cpp`) into `kessen2`.

## Key facts from PS2Recomp @ `75d729ce` (inspected)

- `ps2_recomp` writes per-function `.cpp`, `ps2_recompiled_functions.h`, `ps2_recompiled_stubs.h` and `register_functions.cpp` (defines `g_ps2RecompiledFunctionTable` + a static initializer). `ps2_runtime` references that table, so the object is always pulled from the static lib.
- Upstream runner (`ps2xRuntime/src/main.cpp`): `PS2Runtime rt; rt.initialize(title); rt.loadELF(path); rt.run();`. `loadELF` computes the ELF CRC32 and calls `ps2_game_overrides::applyMatching(elfPath, entry, crc32)`.
- `run()` spins a raylib present loop on the main thread and runs the guest on a game thread; ends on `requestStop()`, window close, or game-thread exit. Exceptions on the game thread are caught and logged as `Error during program execution: …` — `run()` still returns normally.
- Hard-failure signals: `Unimplemented PS2 stub called` (throws), `Unimplemented PS2 syscall called` / `Unknown syscallId` (returns 0, keeps going), missing functions via `MissingFunctionPolicy` (default `ContinueToTarget`; `Stop` exists for CI).
- Per-frame hook without patching upstream: `setDebugUiCallbacks(init, draw, shutdown, user)` — `draw` is invoked once per presented frame. Usable as a frame counter.
- `initialize()` calls `SetConfigFlags(FLAG_WINDOW_RESIZABLE)` then `InitWindow`. raylib's `SetConfigFlags` ORs flags, so calling `SetConfigFlags(FLAG_WINDOW_HIDDEN)` before `initialize()` should hide the window (verify at implementation time).
- WinAPI/raylib `CloseWindow` clash: upstream `ps2EntryRunner` links with `/FORCE:MULTIPLE`; `kessen2` will need the same once it drives `run()`.
- Game overrides register through a static `AutoRegister` in a static lib — must stay in the same TU as a symbol `app` references (`override_count()`), or the MSVC linker drops it.

## Design

**Recompiler tool.** Build `ps2_recomp` in the existing `out/build/ps2recomp-tools` tree (`--target ps2_recomp`, Release). Wrap in `tools/recomp/run-recomp.sh`: builds the tool if missing, checks the TOML's `output` is exactly `<repo>/generated` (same rule as analysis + CMake), clears stale `generated/*.cpp|*.h` (keeping `README.md`), runs `ps2_recomp analysis/kessen2.toml`, logs elapsed time and file count.

**Build.** `k2_generated` stays as is, plus MSVC `/bigobj` and `/FS`, and unity build (batch ~32) to keep build time sane. `kessen2` gets `/FORCE:MULTIPLE` on MSVC only when `k2_generated` exists (document in DEPENDENCIES.md, same upstream issue).

**App (composition root).** `app/src/main.cpp` gains a boot path when `K2_HAS_GENERATED`:
```
kessen2 [--elf <path>] [--headless] [--frames N] [--timeout-s S]
```
- default ELF: `work/disc/SLUS-20275/iso/SLUS_202.75` relative to CWD, else required via `--elf`.
- builds a `PS2Runtime`, sets frame-counter callbacks via `setDebugUiCallbacks`, `--headless` → `SetConfigFlags(FLAG_WINDOW_HIDDEN)` before `initialize()`, and `MissingFunctionPolicy::Stop`.
- `--frames N`: the draw callback calls `requestStop()` after N frames; a watchdog thread calls `requestStop()` after `--timeout-s` and marks timeout.
- exit codes: 0 = reached N frames; 3 = game thread ended/stopped before N frames; 4 = timeout; 1 = init/load failure.
- Boot logic lives in a small `app/src/boot.cpp` (`k2::app::boot(const BootOptions&) -> int`) so `main.cpp` stays thin. `K2_HAS_GENERATED=0` keeps today's scaffold behaviour (CI builds without game data).

**runtime-ext.** `kessen2_overrides.cpp` registers `PS2_REGISTER_GAME_OVERRIDE("Kessen II (SLUS-20275)", "SLUS_202.75", 0x00100008, <crc32>, applyKessen2)`. `applyKessen2` binds guest addresses via `ps2_game_overrides::bindAddressHandler` / `runtime.replaceFunction`; each fix is a named function in `runtime-ext/src/fixes/*.cpp` with a comment linking its BRINGUP.md entry. `override_count()` returns the real count. The CRC32 is a constant (non-secret, identifies the retail ELF); it's also printed by the runtime at load for cross-check. No Kessen-specific code in `external/PS2Recomp`; generic upstream fixes go as patch files in `patches/ps2recomp/NNNN-*.patch` with a README, applied only if unavoidable (and logged).

**Smoke test.** `tests/CMakeLists.txt`: register `k2_boot_smoke` only when `TARGET k2_generated` and the ELF exist (so CI, which has neither, is unaffected). Command: `kessen2 --headless --frames 300 --timeout-s 120 --elf <iso>/SLUS_202.75`. `FAIL_REGULAR_EXPRESSION`: `Unimplemented PS2 (stub|syscall)`, `Unknown syscallId`, `Error during program execution`, `fatal exception`, `\[terminate\]`, missing-function report text. Non-zero exit (crash included) fails. Label `boot` so it can be run alone (`ctest --preset msvc-x64 -L boot`).

**Bring-up loop** (per upstream README): cold boot → read first hard blocker (missing function, syscall TODO, unimplemented stub, CD I/O) → fix → rebuild → cold boot again. Fix preference: real implementation (runtime-ext binding or generic upstream patch) > TOML change (function boundaries, `skip`) > temporary `ret0/ret1/reta0` stub (tracked for promotion). Each batch appended to `docs/BRINGUP.md`. If a fix needs PS2Recomp core-design changes: log it as `needs-upstream`, continue on other blockers.

**BRINGUP.md format.** Header: tool pins, ELF CRC32, how to reproduce. Then a table: `# | date | symptom (log line, PC/RA) | function/syscall | root cause | fix type (real / temp-stub ret0|ret1|reta0 / skip / toml / upstream-patch) | where (file) | status (open/fixed/promote-pending/needs-upstream)`. Separate "Temporary stubs to promote" section, one row per stub. "Furthest point reached" section updated each iteration (last function/syscall, frames presented, screenshot description).

**Legal/repo hygiene.** `generated/`, `work/`, logs with guest dumps stay git-ignored; guard already blocks `generated/` includes outside `app`. BRINGUP.md only records addresses, SDK names and symptoms — no game code or assets.

## Risks

- **Build size/time**: 2,385 functions of generated C++ under MSVC Debug may be slow/huge; mitigate with unity build and building `k2_generated` in RelWithDebInfo if Debug is impractical (document).
- **TOML duplicates** (`printf`, `iWakeupThread`, `sceSifSendCmd` appear twice at different addresses): may produce name clashes; fix in the analysis export fix-up if `ps2_recomp` rejects it.
- **Headless window**: if `FLAG_WINDOW_HIDDEN` doesn't hold, fall back to running minimized; the smoke test is local-only anyway.
- **Title screen may be out of reach** within this phase (IOP/CD streaming, VU1, MPEG FMV). Criterion allows documenting the exact furthest point.
- **ROM-dependent**: nothing in CI can boot the game; CI must keep passing with `K2_HAS_GENERATED=0`.

## Verification

- `sh tools/recomp/run-recomp.sh` exits 0; `generated/` has `register_functions.cpp` + function files.
- `cmake --preset msvc-x64 && cmake --build --preset msvc-x64-debug --parallel` links `kessen2` with `k2_generated`; `kessen2 --version` shows `generated code: linked` and `runtime-ext: 1 override(s)`.
- Runtime log shows the Kessen override matched (ELF name + CRC).
- `ctest --preset msvc-x64 -L boot` runs the smoke test; all other ctest tests still pass; `sh tools/guard/check-forbidden.sh --all` passes.
- CI (no generated code) stays green — check with `gh run list` after push (push only if the user asks).
- BRINGUP.md lists every blocker hit, and every temp stub has a promotion row.

## Steps

1. Build `ps2_recomp` standalone in `out/build/ps2recomp-tools` (Release) and record the verified command in `docs/DEPENDENCIES.md` (remove "Not yet verified").
2. Add `tools/recomp/run-recomp.sh` (path check, stale-output cleanup, runs `ps2_recomp analysis/kessen2.toml`, logs count/elapsed) and a `docs/modules/recomp.md` usage page linked from `docs/modules/README.md`.
3. Run the script on `analysis/kessen2.toml`; fix any recompiler-input errors (e.g. duplicate stub names) in the analysis fix-up step rather than by hand-editing output; log them in a new `docs/BRINGUP.md`.
4. Make `k2_generated` build under MSVC: add `/bigobj` + `/FS`, unity build batching, and `/FORCE:MULTIPLE` on `kessen2` when generated code is linked; configure + build until `kessen2` links against `ps2_runtime` + `k2_generated` + `k2_runtime_ext`.
5. Compute the CRC32 of `SLUS_202.75` and register the Kessen II override in `runtime-ext/src/kessen2_overrides.cpp` with `PS2_REGISTER_GAME_OVERRIDE("Kessen II (SLUS-20275)", "SLUS_202.75", 0x00100008, <crc32>, applyKessen2)`; make `override_count()` return the registered count; keep registration in the TU `app` references.
6. Add `app/src/boot.{h,cpp}` with `BootOptions` and `boot()` (PS2Runtime init/load/run, frame-counter via `setDebugUiCallbacks`, `--headless` hidden window, `--frames`, `--timeout-s` watchdog, `MissingFunctionPolicy::Stop` in headless mode, exit codes 0/1/3/4); wire the CLI in `app/src/main.cpp` behind `K2_HAS_GENERATED`, keeping the scaffold path when it's 0.
7. Add the `k2_boot_smoke` ctest (label `boot`, only when `k2_generated` and the ELF exist) with the fail regexes for unimplemented stubs/syscalls, game-thread exceptions, terminate and missing functions.
8. Cold-boot once, confirm the override matches in the log, and record the baseline furthest point in `docs/BRINGUP.md` (header, blocker table, temp-stub table, furthest-point section).
9. Iterate the bring-up loop: fix the first hard blocker (real implementation in `runtime-ext/src/fixes/` or TOML/`skip`, temp ret0/ret1/reta0 stub only for triage), rebuild, cold-boot, append the BRINGUP.md row; repeat until the title screen or a blocker needing upstream design discussion (log it as `needs-upstream` and continue on others).
10. Promote each temporary stub to a real implementation where possible, updating its BRINGUP.md promotion row; put any generic PS2Recomp fixes as patch files under `patches/ps2recomp/` with a README instead of editing the submodule.
11. Final verification: full configure/build, `ctest --preset msvc-x64` (all tests incl. `-L boot`), guard script, `kessen2 --version` output; update `docs/ARCHITECTURE.md`/module docs for the new boot path and report the furthest point reached against the phase criteria.
