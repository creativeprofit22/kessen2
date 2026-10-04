# Reach the title screen: IOP sound stack (blocker #7 onward)

## Where things stand (inspected 2026-09-26)

- Phase criteria: none met yet. `docs/BRINGUP.md` #7 is `open`; no IOP trace exists; `k2_boot_smoke` only asserts the three early milestones (`applied 4 fix(es)`, overlay loaded).
- `work/logs/iop-probe.log`: LIBSD (id 5), MODHSYN (6), MODMSIN (7), KOEISND (8) all load as real IRX code. Right after LIBSD, an IOP module prints `yet sif hasn't been init` — most likely MODHSYN's start routine checking SIF init through `sifman` (handled by `rpc.dispatchSifManImport`). First hypothesis to confirm/refute with the trace; SPU2 status (plain value map in `iop_memory.cpp`, only SPU DMA completion modelled) is the second.
- Import dispatch (`iop_emulator.cpp` `dispatchImport`): HLE for sysmem/cdvdman/loadcore/thbase/thsemap/thevent/sifcmd/intrman/secrman/modload/ioman/sifman/vblank/timrman/dmacman(always 0)/stdio/sysclib/heaplib; everything else (libsd, modhsyn, modmsin) jumps to the real export via `imports.resolve`; unresolved → warning + v0=0.
- `dmacman` is blanket `v0=0` — LIBSD uses it for SPU2 DMA setup; possible gap.
- No game-side hook reaches the IOP (`PS2Runtime::m_iopSubsystem` is private), so the trace and any SPU2/IOP fix must be engine patches. Hence build-time patch application (user decision below).

## Decision: build-time patch application (user-approved)

- New explicit series file `patches/ps2recomp/series` — one patch filename per line, `#` comments allowed, applied in listed order. **No globbing.**
- `0001-gs-vsync-callback-cause.patch` stays **proposed-only** (not in `series`); runtime-ext keeps its workaround.
- New `cmake/K2PatchedPs2Recomp.cmake`, called from root `CMakeLists.txt` in place of the direct `add_subdirectory(external/PS2Recomp)`:
  1. Verify the submodule HEAD equals the gitlink SHA recorded in the superproject (`git rev-parse HEAD` in the submodule vs `git ls-tree HEAD external/PS2Recomp`); `FATAL_ERROR` on mismatch or dirty tracked files (`git -C external/PS2Recomp diff --quiet HEAD`).
  2. Read `series`; empty series ⇒ use the submodule directly (current behaviour).
  3. Otherwise export the pinned commit to `${CMAKE_BINARY_DIR}/_deps/ps2recomp-patched/src` with `git -C external/PS2Recomp archive --format=tar <sha>` + `cmake -E tar xf` (no submodule/worktree mutation; PS2Recomp has no nested submodules).
  4. For each listed patch: `git apply --check` against the fresh export → `FATAL_ERROR` naming the patch on failure; then `git apply`. All via `execute_process(COMMAND …)` argument lists.
  5. Stamp file = pinned SHA + SHA256 of `series` and every listed patch; re-export only when the stamp changes. `set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS …)` on `series` and each patch so edits reconfigure.
  6. `add_subdirectory(<patched src> external/PS2Recomp EXCLUDE_FROM_ALL)` — same binary dir as today, so targets/paths are unchanged. `CMAKE_SOURCE_DIR` quirks only affect Recomp/Analyzer/Test/Studio, all OFF here.
- CI (`.github/workflows/ci.yml`, build-windows) already runs `cmake --preset msvc-x64`, which now goes through the same path. Add a cheap `check` job step on ubuntu: `git -C external/PS2Recomp apply --check` for every `series` entry (needs `submodules: recursive` on that checkout) so a stale patch fails fast.
- `patches/ps2recomp/README.md`: rewrite the "not applied" intro; table gains an **Applied** column (`series` = applied at configure, else proposed-only); document how to add a patch (`git diff` in a scratch export, add to `series`), and that configure fails hard on a non-applying patch. Also note the staged tree lives only under the build dir.
- `tools/recomp/run-recomp.sh` / standalone `ps2recomp-tools` build: unaffected (recompiler only; patches touch ps2xIOP/ps2xRuntime).

## IOP trace (patch `0002-iop-trace.patch`, generic → applied)

Env var `PS2X_IOP_TRACE` (generic name, engine-level), comma list of: `imports`, `spu2`, `rpc`, or `all`. Off ⇒ zero behaviour change.

- `imports`: in `dispatchImport`, log `lib:ordinal a0..a3 ra pc` on entry. HLE-handled: log `v0` after. `JumpToGuest`: push `{lib, ordinal, ra, sp}` on a per-`CpuState*` pending stack; in `step`, when `cpu.pc == ra && gpr[29] == sp`, log `v0` and pop. Optional filter `PS2X_IOP_TRACE_LIBS=modhsyn,modmsin,libsd,…` to keep noise down (default: everything except sysclib/stdio).
- `spu2`: in `IopMemory` hardware read/write for `0x1F900000–0x1FA00000` (and SPU DMA `0x1F8010C0–C8`, `0x1F801500–08`): log `R/W width addr value`. Cap at N lines (`PS2X_IOP_TRACE_MAX`, default 200000) to protect disk.
- `rpc`: log each emulated RPC server call (sid, fn, send/recv size, returned status word) in the RPC service, so the KSND fn `0x1E` → `-9` is visible.
- Logs go through `host.log(LogLevel::Info, "[IOP:trace] …")` (already routed to stderr as `[ps2xIOP]`).
- `IopMemory` currently has no host/log handle: give it a trace sink `std::function<void(std::string_view)>` set by the emulator (no globals).
- Document in `docs/BRINGUP.md` Diagnostics table + `run_boot_smoke.cmake` unsets `PS2X_IOP_TRACE*`.

## Root cause and fix (driven by trace output)

1. Run `kessen2.exe --headless --frames 900` with `PS2X_IOP_TRACE=imports,rpc,spu2`, save to `work/logs/` (gitignored). Find the first KSND fn `0x1E` call, the modhsyn/modmsin import inside it that returns nonzero, and walk back into that module's own imports / SPU2 accesses (and MODHSYN's start-time `sif hasn't been init` path) to find the first wrong answer.
2. Cross-check expected behaviour against PCSX2 `pcsx2/SPU2` (`spu2sys.cpp` register read/write side effects, `regs.h`, `Dma.cpp`) and PS2SDK/open headers for sifman/libsd semantics. Behavioural reference only — no code copied (GPL-3 compatible anyway, but keep it clean-room style).
3. Implement the real behaviour in a new patch `0003-…` (e.g. minimal SPU2 core model: `STATX` ready/DMA-busy bits, `ATTR`→`STAT` mirroring incl. DMA mode/busy clear, `ENDX`/`KON`/`KOFF` latching, IRQ status; or correct `sifman` init check / `dmacman` SPU channel behaviour). Unit-test-free upstream tree, so verification = trace shows the call now returns 0 + KSND returns ≥0. Kessen-only behaviour (unlikely) goes to `runtime-ext` instead. No ret0/ret1 stubs.
4. Log #7 root cause/fix in `BRINGUP.md`, then iterate: each next stall → new row (symptom, function, root cause, fix type, where, status) until the title screen.

## Title-screen milestone

- Identify the title-screen entry during bring-up (first draw of the title assets / title state function, found via `K2_TRACE_FUNCS` + GS logs), and add a runtime-ext diagnostic fix that logs `[kessen2] milestone: title screen` once when that guest function first runs (wrap via `replaceFunction`, same pattern as `trace_funcs.cpp`). This bumps the fix count → update `applied N fix(es)` in `run_boot_smoke.cmake` and BRINGUP.md.
- `run_boot_smoke.cmake`: add the milestone, raise `K2_BOOT_SMOKE_FRAMES` default if needed (document the frame at which it appears, Debug), add `\[IOP\] unhandled import` and `execution outside RAM` to fail patterns only if they don't fire in a clean run.
- Windowed check: run without `--headless` and confirm the title renders (screenshot saved to `work/`, not committed); note result in BRINGUP.md "Furthest point reached".

## Risks

- Several blockers may follow #7 (graphics, pad, more IOP gaps); phase finishes only at the title screen — report honest partial progress otherwise.
- `k2_boot_smoke` needs local game data; CI skips it (as today). Verification of the milestone is local-only — say so.
- Build time: patched export means a first-configure copy of PS2Recomp sources (small; FetchContent deps unchanged).
- Upstream drift: patches are pinned to `75d729ce`; bumping the pin requires regenerating them (configure will fail loudly by design).

## Verification

- Configure with empty `series` → identical to today; with `series` listing a deliberately broken patch → configure fails naming it (manual check, then revert).
- `cmake --preset msvc-x64`, `cmake --build --preset msvc-x64-debug --parallel`, `ctest --preset msvc-x64` (incl. `k2_boot_smoke` locally), `sh tools/guard/check-forbidden.sh --all`, `sh tools/guard/test_guard.sh` if guard touched.
- Trace off: boot log identical in content to before (no `[IOP:trace]` lines).

## Steps

1. Add `patches/ps2recomp/series` (initially only comments) and `cmake/K2PatchedPs2Recomp.cmake` implementing pin check, `git archive` export into the build dir, per-patch `git apply --check` with hard `FATAL_ERROR`, apply, stamp, and configure-depends; switch the root `CMakeLists.txt` PS2Recomp block to use it.
2. Update `.github/workflows/ci.yml`: add a `check`-job step (with submodule checkout) that runs `git apply --check` for every `series` entry against the pinned submodule; build-windows keeps using the preset (same path).
3. Rewrite `patches/ps2recomp/README.md` for applied-vs-proposed patches (Applied column, `series` workflow, 0001 marked proposed-only) and note the mechanism in `docs/DEPENDENCIES.md`.
4. Write `patches/ps2recomp/0002-iop-trace.patch` (import call/return trace with per-CPU pending returns, SPU2/SPU-DMA register trace via an injected sink in `IopMemory`, RPC server call/result trace, `PS2X_IOP_TRACE`/`_LIBS`/`_MAX` env vars), add it to `series`, reconfigure, build.
5. Document the trace variables in the `docs/BRINGUP.md` Diagnostics table and unset them in `tests/cmake/run_boot_smoke.cmake`.
6. Run a headless 900-frame boot with `PS2X_IOP_TRACE=imports,rpc,spu2`, identify the failing MODHSYN/MODMSIN call under KSND fn `0x1E` and the IOP/SPU2 behaviour behind it (checking MODHSYN's `sif hasn't been init` path and `dmacman`), cross-referenced with PCSX2 `pcsx2/SPU2`.
7. Implement the real fix as `patches/ps2recomp/0003-<name>.patch` (or in `runtime-ext` if Kessen-specific), add to `series`, rebuild, and confirm via trace that KSND fn `0x1E` no longer returns `-9`; update BRINGUP.md row #7 with root cause, fix type, where, status.
8. Continue booting; for each further stall, diagnose, fix with a real implementation (patch or runtime-ext), and add a BRINGUP.md blocker row, until the title screen is reached.
9. Add a runtime-ext milestone fix logging `[kessen2] milestone: title screen` on the first call of the identified title-screen function; register it in `apply_all.cpp`.
10. Update `tests/cmake/run_boot_smoke.cmake` (new fix count, title-screen milestone, frame budget) and the smoke description + "Furthest point reached" in BRINGUP.md.
11. Verify windowed run shows the title screen; run `ctest --preset msvc-x64` and `sh tools/guard/check-forbidden.sh --all`; report results and update the roadmap phase status with evidence.
