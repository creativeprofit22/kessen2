# Kessen II bring-up log

Blocker log for getting the recompiled SLUS-20275 build to boot. Addresses, SDK names and symptoms only — no game code or assets.

## Setup

| Item | Value |
|---|---|
| Boot ELF | `SLUS_202.75`, entry `0x00100008`, CRC32 `0xBAFBDE3F` (zlib/IEEE) |
| PS2Recomp | `75d729ce` (see [DEPENDENCIES.md](DEPENDENCIES.md)) |
| Analysis | Ghidra 12.1.3 + EE plugin v2.1.37, `analysis/kessen2.toml` (297 stubs, 33 untracked stubs, `patch_syscalls=false`) |
| Game override | `runtime-ext/src/kessen2_overrides.cpp` — `PS2_REGISTER_GAME_OVERRIDE("Kessen II (SLUS-20275)", "SLUS_202.75", 0x00100008, 0xBAFBDE3F, …)` |

Reproduce from a cold start:

```sh
sh tools/recomp/make-combined-elf.sh # boot ELF + main overlay -> work/recomp/
K2_ELF=work/recomp/SLUS_202.75.combined.elf sh analysis/run-analysis.sh
sh tools/recomp/run-recomp.sh        # generated/
cmake --preset msvc-x64 && cmake --build --preset msvc-x64-debug --parallel
out/build/msvc-x64/app/Debug/kessen2.exe --headless --frames 900 --timeout-s 120
ctest --preset msvc-x64 -L boot      # pass/fail version of the run above
```

`k2_boot_smoke` (`tests/cmake/run_boot_smoke.cmake`) runs 900 frames (`K2_BOOT_SMOKE_FRAMES`) and passes only if kessen2 exits 0, logs none of the hard-failure lines (unimplemented stub/syscall, `guest-branch:missing-target`, `overlay: MISMATCH`, …), and prints all three boot milestones: `[kessen2] game override matched`, `[kessen2] applied 4 fix(es)`, and `[kessen2] overlay: main overlay loaded at 0x005a4800 (recompiled)`. The overlay line first appears between frames 500 and 525 in a Debug build, so a clean exit alone (which a guest stuck in the disc-ready loop also gets) no longer passes. An existing build tree keeps its cached `K2_BOOT_SMOKE_FRAMES`; reconfigure with `-DK2_BOOT_SMOKE_FRAMES=900` if it still says 300.

Fix preference: real implementation (runtime-ext binding or generic upstream patch) > TOML change (boundaries, `skip`) > temporary `ret0`/`ret1`/`reta0` stub (triage only, tracked below).

## Blockers

| # | Date | Symptom (log line, PC/RA) | Function / syscall | Root cause | Fix type | Where | Status |
|---|---|---|---|---|---|---|---|
| 1 | 2026-09-26 | Recompile: none — `ps2_recomp` finished with 0 errors, 0 unhandled instructions, 221 unresolved `JR`/`JALR` warnings (fallback dispatch) | — | — | — | — | fixed (nothing to do) |
| 2 | 2026-09-26 | Cold boot: 3,600 frames presented, no GS traffic; every `[run:tick]` after `sceCdSearchFile("\LINKDATA.BNS;1")` sits at PC `0x114D20` (calibrated busy-wait) | `sceCdDiskReady` at `0x00114C80` (unnamed `FUN_00114c80`) | Ghidra did not name this libcdvd function, so it was recompiled instead of routed to the runtime. It retries `sceSifBindRpc(SID 0x8000059A)` forever; that CDVDFSV server lives in the IOP ROM, which the IOP emulator does not provide. The only other unnamed CDVDFSV binders (`0x114518`, `0x114830`) are reachable only from already-stubbed `sceCdRead`/`sceCdReadIOPm`. | real (HLE binding to `ps2_stubs::sceCdDiskReady`) | `runtime-ext/src/fixes/cd_disk_ready.cpp` | fixed |
| 3 | 2026-09-26 | `[guest-branch:missing-target] kind=DirectCall op=JAL source=0x14ce9c target=0x627820`, game thread returns after ~500 frames | code at `0x627820` (called from boot routine `FUN_0014cd90`) | Kessen II loads code **overlays** from `LINKDATA.BNS` (Metrowerks `MWo3` header). The ELF lists them as empty NOLOAD segments: 1 fixed overlay at `0x5A4800` (read at boot: LINKDATA LSN `+0xFB8B`, `0x1A3` sectors), 2 alternates at `0x7F1800`, 111 alternates at `0x860000`. None of that code was in the ELF, so none was recompiled. | toml/analysis input: `tools/recomp/make-combined-elf.sh` writes the fixed overlay into its segment of a work copy of the ELF, and analysis + `ps2_recomp` run on that (2,385 → 4,472 functions). Runtime guard checks the loaded overlay header and logs swap-slot loads. | `tools/recomp/make-combined-elf.sh`, `runtime-ext/src/fixes/cd_overlay_guard.cpp` | fixed (fixed overlay) |
| 3a | 2026-09-26 | Build: one `k2_generated` unity batch compiled for >40 min (single `cl.exe`, 2 GB) | `FUN_005d10e0` → 12 MB `.cpp` | Ghidra folded a shared `jr $ra` tail-jump target (`0x5F83F0`) into the function body across ~160 KB of other functions; the exporter emits `[entry, max body address]`. | analysis: `K2SplitFarChunks.java` splits far chunks into their own functions (2 functions). Build: `/MP` on `k2_generated`. | `analysis/ghidra/K2SplitFarChunks.java`, `CMakeLists.txt` | fixed |
| 4 | 2026-09-26 | Not hit yet. Will show as `[kessen2] overlay: load into swap slot … is not recompiled`, then a missing-function stop | overlays sharing an address (`0x7F1800`, `0x860000`) | PS2Recomp has one global address→function table (`g_ps2RecompiledFunctionTable`) and emits one function per address; several overlays at the same address need per-overlay function sets swapped on load (plus a name/namespace prefix in `ps2_recomp` output). That is a core-design change. | — | — | needs-upstream |
| 5 | 2026-09-26 | Main loop stuck at PC `0x14D754` (`FUN_0014d6e0` polls `0x14D960` until the frame-busy flag at `gp-0x6B60` clears); no GS traffic after ~600 frames | VSync handler `0x0014D590` (via game dispatcher `0x001B67B0`, registered with `sceGsSyncVCallback` `0x00101020`) | PS2Recomp calls the `sceGsSyncVCallback` callback with `a0` = vsync tick. libgraph installs it as the INTC VBLANK_S handler, so on hardware `a0` is the cause, `2`; the game's handler returns early unless `a0 == 2`. | real (runtime-ext wrapper sets `a0 = 2` for scheduler invocations) + generic upstream patch | `runtime-ext/src/fixes/gs_vsync_callback_cause.cpp`, `patches/ps2recomp/0001-gs-vsync-callback-cause.patch` | fixed (patch proposed upstream) |
| 6 | 2026-09-26 | After #5 the main loop advanced only about once per 120 presented frames; the handler ran ~6 times in 900 frames | spin loop `0x14D754` → `0x14D960` | Guest VBLANK is paced by accounted EE cycles; a recompiled spin costs ~40 cycles per iteration, so one guest frame of spinning burns seconds of host time in Debug. | real (idle-loop skip: while the flag is set, block until the next VBLANK and re-run the poll) | `runtime-ext/src/fixes/frame_wait_idle.cpp` | fixed |
| 7 | 2026-09-26 | After #6: 19 more `sceCdRead`s (assets, then disc sector `0x13`), GS packets, then the main thread spins at PC `0x151CD4` for the rest of the run. No error lines. | memory-card/sound state machine `FUN_00152110` (worker thread woken by the Timer 1 handler `0x152050`), state 12 at `0x152930` → `0x1B8340` → `0x1B7F20` = `sceSifCallRpc` to Koei's sound driver (`KOEISND.IRX`, RPC SIDs `'KSND'/'KSNE'/'KSNF'`, fn `0x1E`, 0x18-byte request) | The RPC completes, but the IOP-side server returns `-9` every time; the state machine resets to state 1 and retries forever, and the main thread waits for it (`FUN_00151c90`). Timer 1 interrupts, the worker thread, semaphores and the RPC transport all work (traced). The `-9` comes from inside `KOEISND.IRX` running on PS2Recomp's IOP interpreter; likely an emulation gap (SPU2/`LIBSD`, IOP memory transfer, or an earlier KSND call that failed silently). One IOP module prints `yet sif hasn't been init` at load. | — | — | open (next: trace KSND request/response and IOP-side `LIBSD` calls) |

## Temporary stubs to promote

| Stub | Address | Kind | Added in # | Promotion plan | Status |
|---|---|---|---|---|---|
| _(none)_ — no `ret0`/`ret1`/`reta0` stubs were needed so far; every fix above is a real implementation or an analysis/input change | | | | | |

## Furthest point reached

| Iteration | Frames | Last guest activity | Blocker |
|---|---|---|---|
| baseline (no fixes) | 3,600 of 3,600 (timer only) | PC `0x114D20` busy-wait inside `FUN_00114c80`; no GS writes | #2 |
| after #2 | ~500 | main overlay read into `0x5A4800`; first GS packet (`sceGsSyncVCallback` set, clear sprite); jump to `0x627820` | #3 |
| after #3 | ~600 | main overlay runs from recompiled code; 14 asset reads; main loop stalls on the frame-busy flag | #5 |
| after #5, #6 (current) | 1,800 of 1,800 in 45 s (Debug, headless) | 33 disc reads in total (main overlay, 4 large asset blocks, sound/data blocks, disc sector `0x13`), 16 GIF transfers, 51 DMA transfers, memory card 1 detected (formatted, 8 MB free). Stuck in the sound-driver RPC retry loop; the main thread waits at PC `0x151CD4`. **Title screen not reached**; nothing visible yet beyond the cleared frame. | #7 |

## Diagnostics

Both are off unless the environment variable is set; neither changes behaviour.

| Variable | Effect |
|---|---|
| `K2_TRACE_CD=1` | logs every `sceCdRead` (LSN, sectors, destination, caller) |
| `K2_TRACE_FUNCS=0xADDR[,…]` | wraps up to 8 recompiled functions and logs their first 64 calls (`a0`–`a2`, `ra`, `gp`, `sp`) and returns (`pc`, `v0`) |
