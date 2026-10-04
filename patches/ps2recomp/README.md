# PS2Recomp patches

Generic fixes found during Kessen II bring-up that belong in [ran-j/PS2Recomp](https://github.com/ran-j/PS2Recomp), not in this repo. Each patch is either:

- **applied** — listed in [`series`](series). Configure (`cmake/K2PatchedPs2Recomp.cmake`) exports the pinned commit with `git archive` to `<build>/_deps/ps2recomp-patched/src`, applies every listed patch in order, and builds that tree. The submodule itself is never modified; the patched tree exists only under the build dir.
- **proposed-only** — present here but not in `series`. Never applied; `runtime-ext` carries an equivalent Kessen-scoped workaround until upstream takes the fix.

Configure **fails hard** (naming the patch) if a listed patch does not apply to the pinned commit, and also if the submodule is not at the superproject's pinned commit or has local changes. CI's `check` job applies every `series` entry in order to a `git archive` export of the pin, the same way. An empty `series` builds the submodule directly.

## Adding a patch

1. Make the change in a scratch copy of the pinned commit, never in `external/PS2Recomp` (e.g. `git -C external/PS2Recomp worktree add ../../work/ps2recomp-scratch HEAD`, edit, `git -C work/ps2recomp-scratch diff > patches/ps2recomp/NNNN-name.patch`, then `git -C external/PS2Recomp worktree remove ../../work/ps2recomp-scratch`). Patches must apply in `series` order, so make later patches on top of earlier ones.
2. Check it by reconfiguring with it listed in `series` (step 3): configure applies the whole series in order and names the first patch that fails. A lone `git apply --check` against `external/PS2Recomp` is not enough: later patches depend on earlier ones, and a Windows checkout may have CRLF sources.
3. Add the filename to `series` to apply it (edits to `series` or any listed patch reconfigure automatically), add a row below, and log it in BRINGUP.md.

| Patch | Applied | Fixes | Workaround in runtime-ext | BRINGUP.md |
|---|---|---|---|---|
| `0001-gs-vsync-callback-cause.patch` | proposed-only | `sceGsSyncVCallback` callbacks got `a0` = vsync tick; libgraph installs them as INTC VBLANK_S handlers, so the argument is the interrupt cause (`2`). Handlers that check the cause never ran. | `src/fixes/gs_vsync_callback_cause.cpp` | #5 |
| `0002-iop-trace.patch` | series | Diagnostic only: `PS2X_IOP_TRACE` (`imports`, `spu2`, `rpc`), `PS2X_IOP_TRACE_LIBS`, `PS2X_IOP_TRACE_MAX` — IRX import calls/returns, SPU2 + SPU DMA register accesses, EE→IOP RPC calls and SIF DMA. No behaviour change when unset. | — | #7 |
| `0003-iop-shared-sysmem-pool.patch` | series | IRX images had a private arena below `0x120000` and `AllocSysMemory` a bump allocator in `0x120000`–`0x1F0000` that never reused freed blocks, so large allocations (KOEISND's `0x87800`) failed. Now one first-fit sysmem pool for both; `QueryTotalFreeMemSize` sums free gaps. | — | #7 |
| `0004-cd-read-iopm-to-iop-ram.patch` | series | `sceCdReadIOPm` forwarded to `sceCdRead` and wrote into EE RAM; the buffer is an IOP address, so data now goes to IOP RAM. | — | #7 |
| `0005-mpeg-stream-type-values.patch` | series | `sceMpegStr*` stream-type constants were off by one (libmpeg: M2V=0, IPU=1, PCM=2, ADPCM=3), so ADPCM/PCM audio callbacks never matched. | — | #8 |
| `0006-runtime-internal-heap.patch` | series | Runtime-internal guest allocations (MPEG callback data, SIF/GS/font packets, IOP host buffers) shared the game's malloc heap and failed once a game claimed it all; they now come from a reserved arena `0x1F40000`–`0x1F60000` (`PS2Runtime::internalMalloc`), falling back to the game heap. | — | #8 |
| `0007-ee-events-follow-host-time.patch` | series | Vblank/alarm events needed both the EE cycle deadline and the host deadline; when the guest runs slower than real time, vblank slowed ~50×. Events whose host deadline has passed now advance the EE clock to their cycle. | — | #8 |
| `0008-ipu-to-dma-drains.patch` | series | A DMA channel 4 (toIPU) start never completed (the IPU is not emulated), so `CHCR.STR` stayed set forever. It now drains at once: MADR/TADR advance over the source chain, STR clears, the channel IRQ is raised. | — | #9 |
| `0009-mpeg-nodata-callback.patch` | series | The HLE `sceMpegGetPicture` never raised `sceMpegCbNodata`, so games that feed their own IPU ring from that callback never consumed it and hung at end of movie. It now queues the game's Nodata callbacks (one round in flight per `sceMpeg`). | — | #9 |
| `0010-mpeg-create-resets-status.patch` | series | The HLE `sceMpegCreate` did not finish with libmpeg's `sceMpegReset`, so a reused work area kept a stale decoder status word at `priv+0` and the next movie was reported finished immediately. It now clears `priv+0x00..0x08`. | — | #10 |
| `0011-dispatch-observer.patch` | series | Diagnostic hook only: `PS2Runtime::setDispatchObserver` reports `Enter`/`Exit` around every function call made through `dispatchGuestBranch` and every function the EE scheduler enters. Used by `k2_diag` write attribution ([ADR-0006](../../docs/adr/0006-probe-diagnostics.md)). When unset (default) the cost is one null-pointer test per dispatch; no behaviour change. | — | Diagnostics |

When upstream merges a fix: bump the submodule, delete the patch row and file (and its `series` entry), remove any workaround, and mark the BRINGUP.md row `fixed (upstream)`.
