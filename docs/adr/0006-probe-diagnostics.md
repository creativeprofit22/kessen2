# ADR-0006: Declarative probe diagnostics (`k2_diag`) and a reverse watchpoint

- **Status:** Accepted
- **Date:** 2026-10-04

## Context

Bring-up (see [BRINGUP.md](../BRINGUP.md)) was debugged with a growing set of ad-hoc environment variables (`K2_TRACE_FUNCS`, `K2_TRACE_WORD`, `K2_TRACE_ARM`, `K2_TRACE_NONZERO`, `K2_WATCH`, `K2_WATCH_EVERY`, `K2_SCREENSHOT_EVERY`, `K2_TRACE_CD`) and with temporary diagnostic code hand-edited into the engine build copy (`out/build/*/_deps/ps2recomp-patched/src`). Those had several problems:

- **Fail-open:** a malformed address was "ignored", and extra entries past the fixed limits (8 functions, 8 words) were silently dropped, so a typo produced a quiet, misleading run.
- **Not reproducible:** a finding was recorded as a command line in prose; the hand-edited engine code existed only in one build tree and was wiped by the next reconfigure.
- **Missing question:** the hardest bring-up questions were "who wrote this word?" (e.g. the stale `sceMpeg` status word of BRINGUP #10), answered by bisecting with `K2_TRACE_WORD` + `K2_TRACE_ARM` across many runs.
- **Wrong home:** the trace code lived in `runtime-ext/src/fixes/`, next to real game fixes, although it is game-agnostic.

DKC1Recomp, another static recompilation project, solves the same problems with a strictly validated trace variable (`runner/headless_main.c`, `DKC1_TRACE_PC`: reject malformed input and exit non-zero) and a reverse watchpoint (`tools/reverse_watch.py`: a bounded, fail-closed range grammar of at most 16 ranges / 4 KiB with no overlaps; replay the deterministic headless build; answer "who last changed this address before frame F" at function granularity). References: `elliotttate/DKC1Recomp@3eb9a10c49cb210df2bd63addafe2a78cf9f4e22` (`tools/reverse_watch.py` L1–60, `runner/headless_main.c` L238–246).

## Decision

### One module, one file format

- New module `diag/` → target **`k2_diag`**, `k2_add_module(k2_diag ALLOWS ps2_runtime)`. It is game-agnostic: no Kessen II addresses or names. `k2_runtime_ext` and the app may link it; platform, render and generated may not (deny-by-default in `cmake/K2Modules.cmake`, covered by `k2_dep_rule_negative`).
- Its pure part (spec parser, write attributor, log writer) has no PS2Recomp headers and is always built and unit-tested. The runtime binding is compiled only when `ps2_runtime` and `k2_generated` exist, like `k2_runtime_ext`'s fixes.
- C++23 per target for `std::expected`, as in ADR-0005.
- Game-specific probe files live in `probes/*.probe`, one per finding. They hold only addresses and probe settings, never game data.

### Probe spec v1

Plain text, `#` comments, one directive per line, loaded from `K2_PROBE=path` (output to `K2_PROBE_LOG=path`, default stderr with the prefix `[k2probe] `):

```
k2probe 1                                   # required first directive
func 0x1b6670 calls=64 nonzero=256 word=0x11ef480
arm 0x1085d0 after=2
watch 0x1113320:4 every=600
attrib 0x1595:34
attrib-max-events 100000
screenshot every=150 from=0
```

| Directive | Meaning |
|---|---|
| `func ADDR [calls=N] [nonzero=N] [word=ADDR]` | wrap the recompiled function at ADDR; log the first `calls` (default 64) entries (`a0`–`a3`, `ra`, `gp`, `sp`, optional `word`) and returns (`pc`, `v0`, optional `word`), then `calls=N` at powers of two; after the window, up to `nonzero` returns with `v0 != 0` |
| `arm ADDR [after=N]` | log nothing until the N-th (default 1) call of ADDR, which must also be a `func`; then every window starts fresh |
| `watch ADDR:LEN [every=N]` | print the words of the range every N (default 600) presented frames |
| `attrib ADDR:LEN` | write attribution: report every change of the range together with the recompiled function that was running |
| `attrib-max-events N` | cap on attribution events (default 100,000) |
| `screenshot every=N [from=F]` | save `k2-frame-NNNNNN.png` every N presented frames starting at F |
| `gsevents every=N [from=F]` | log the GS draws and image transfers recorded since the previous dump (engine ring of 512 events) on the same schedule (added for BRINGUP #11) |
| `gsregs every=N [from=F]` | log the GS privileged display registers (`PMODE`, `SMODE2`, `DISPFB1/2`, `DISPLAY1/2`) and the presenter's last source pick on the same schedule (added for BRINGUP #11) |

Validation is **fail-closed**: any violation is an error that names the line, and the app exits with `kBootProbeInvalid` (5) before the runtime is created. Limits: hex only (`0x` optional); addresses inside EE RDRAM `0x00000000`–`0x01FFFFFF`; `func`/`word`/`watch`/`attrib` addresses 4-aligned except `attrib` (byte ranges allowed); `watch` LEN a multiple of 4 and ≤ 64; ≤ 16 `func` (`calls` ≤ 1024, `nonzero` ≤ 4096), ≤ 1 `arm`; ≤ 16 `watch` ranges and ≤ 256 watched bytes; ≤ 16 `attrib` ranges and ≤ 4096 bytes (DKC1Recomp's numbers); `attrib-max-events` ≤ 1,000,000; every count ≥ 1; no overlapping ranges within a kind; no duplicate functions, unknown directives/options, repeated options, missing or wrong version, or file > 64 KiB. A `func` address with no recompiled function also exits 5, at install time. The other exit-5 causes are environment errors, not spec errors: `K2_PROBE` set but empty, `K2_PROBE` set in a build without recompiled game code, and a `K2_PROBE_LOG` path that cannot be created. Each prints its own stderr line, so a caller such as `reverse_watch` can tell them from a bad spec.

Output is line-oriented, `frame=F seq=N kind=K key=value…`, where `frame` is the presented-frame count at the time of the event and `seq` a global sequence number. Watch rows and the final attribution summary are sorted by address. Summary lines (`last-write`, `attrib-stats`) are written at the end of the run, so their `frame` is the final frame; each `last-write` carries `at_frame=` for the frame the write was seen.

### Hooks

- **Function probes** wrap through the runtime's function table (`PS2Runtime::lookupFunction` + `replaceFunction`, 16 template wrapper slots), exactly like the old `trace_funcs.cpp`. They need no engine change and cost nothing when no probe is loaded. They are installed after `loadELF`, i.e. after the game override's fixes, so they wrap whatever the fixes installed.
- **Write attribution** needs to know which function is running. A generic engine patch, `patches/ps2recomp/0011-dispatch-observer.patch`, adds `PS2Runtime::setDispatchObserver(observer, user)`; `dispatchGuestBranch` (every JAL/JALR/JR to another function) and the `EeScheduler` run loop (thread entry / resume) call it with `Enter` before and `Exit` after the target function. When unset (the default) the cost is one predictable null-pointer test per dispatch. The attributor keeps a per-context call stack and a shadow copy of the watched ranges; at every edge it compares the ranges with the shadow. A change seen at `Enter(T)` is attributed to the caller (stack top) with `site` = the branch source PC; a change at `Exit(T)` is attributed to T. With an empty stack the change is reported `writer=unattributed`.
- **Watches and screenshots** run from the frame callback (`setDebugUiCallbacks`) on the render thread. `k2_diag` does not depend on raylib: it returns "take a screenshot now" and the app takes it.
- **Reverse query** `tools/probe/reverse_watch.py --address ADDR[:LEN] --before-frame F` writes an `attrib` probe, runs `kessen2 --headless --frames N` (N ≤ 5,000), and prints the last writer of each byte at or before frame F. `--repeat 2` reruns and compares.

### Legacy variables are removed

`K2_TRACE_FUNCS/WORD/ARM/NONZERO`, `K2_WATCH`, `K2_WATCH_EVERY`, `K2_SCREENSHOT_EVERY` and `K2_TRACE_CD` are removed, not emulated: if any is set, `kessen2` names it, points to the probe mapping and exits 5 before the game runs (with or without generated code), so an old command line cannot fail open. BRINGUP.md maps each to its directive (`K2_TRACE_CD` → `probes/cd-reads.probe`, a `func` on `sceCdRead`). `PS2X_IOP_TRACE*` (engine-level, patch 0002) is unchanged. Temporary hand edits in the engine build copy are forbidden (AGENTS.md, `patches/ps2recomp/README.md`) and the marker they used is rejected by `tools/guard/check-forbidden.sh`.

## Consequences

- Every bring-up finding can be re-run from a committed `probes/*.probe` file; a bad spec stops the run instead of producing a misleading one.
- **Attribution is function-granular and approximate.** Static `J` tail jumps to a named function call it directly (`control_flow_emitter.cpp`), bypassing the observer, so those writes fold into the caller. HLE stubs and syscalls count as their caller. Writes by IOP, DMA or the render thread land in whatever interval was running on the EE executor. A function that throws (scheduler yield) sends no `Exit`; the stack resyncs on the next matching `Exit`, and is capped at 256.
- **Determinism is approximate.** Patch 0007 makes EE events follow host time, so two runs agree closely but not exactly, and frame tags come from the render thread. The reverse tool's `--repeat` exists to make that visible instead of hiding it.
- **Cost when disabled:** measured as process CPU time of a 5,000-frame Release headless run, 3 runs before and after patch 0011 with no probe loaded (elapsed time is pinned near 60 fps and cannot show it). Results in the "Measurements" section below.
- Revisit if upstream PS2Recomp gains its own tracing/dispatch hook (drop 0011), or if byte-exact attribution is ever needed (would require a store hook in generated code).

## Measurements

Measured 2026-10-04 on a Release build: `kessen2 --headless --frames 5000`, no probe, process CPU time (`TotalProcessorTime`). Every run reported `result=ok frames=5000`.

| Build | CPU s (3 runs) | Mean |
|---|---|---|
| before (series 0002–0010, old diagnostics) | 93.83 / 93.02 / 95.06 | 93.97 |
| after (0011 + `k2_diag`), rested machine, set 1 | 87.45 / 96.06 / 95.22 | 92.91 |
| after, rested machine, set 2 | 96.41 / 94.81 / 95.16 | 95.46 |
| after, right after a 30-minute build (discarded) | 110.89 / 104.06 / 101.63 and 103.50 / 99.84 / 103.47 | 104.1 |

Result: **within run-to-run noise.** On a rested machine the after-build averages 94.19 s, against 93.97 s before (+0.2 %, smaller than the spread inside either set). Wall time is 86.99–87.89 s, against 86.79–86.88 s before. The first after-sets ran straight after a 30-minute full compile and the probe runs, and were about 10 % slower. Re-running on a cooled machine removed the difference, so it came from heat and load, not from the code. Before and after differ in more than 0011 (app built as C++23, links `k2_diag`, trace fix removed), so this bounds the total of all of them, 0011's null-pointer test included, to noise.

With an `attrib` probe loaded (one 4-byte range), the guest runs about 2× slower (1,320 presented frames took about 2,770 vsyncs instead of about 1,450). That is acceptable for a diagnostic run.

Reverse query `--address 1113320:4 --before-frame 1200 --repeat 2`: both runs name `0x108850` (exit edge) as the last writer of the movie frame counter. The value written differs between runs (`0x04` at frame 1185 vs `0xA8` at frame 1199), which shows the approximate determinism described above: the tool reports "writer only" agreement.

## Alternatives considered

- **Keep the env vars, raise their limits** — still fail-open, still not reproducible, no write attribution.
- **Emulate the old env vars on top of probes** — two surfaces to keep in sync for no user; a mapping table in BRINGUP.md is enough.
- **Store hook in generated code (byte-exact write attribution)** — needs a recompiler change and costs on every store; function granularity answered every question so far.
- **Single-step / PC trace like `DKC1_TRACE_PC`** — recompiled code has no per-instruction hook; per-function entry/return is the natural unit here.
- **Put the probe engine in `runtime-ext`** — it is game-agnostic; a separate module keeps it reusable and keeps Kessen addresses out.

Pinned references: PS2Recomp `75d729ce40d7eed9649fd4bb05628dee520f3d0c` (this repo's pin) and upstream `ran-j/PS2Recomp@c5a9d02573410a2085a4b4b831b0b68ba3515440` (`ps2xRuntime/include/ps2_runtime.h` L337–343, `ps2_runtime.cpp` L1397–1399, `Kernel/EeScheduler.cpp` L286–299); DKC1Recomp `3eb9a10c49cb210df2bd63addafe2a78cf9f4e22`.
