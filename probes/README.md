# probes/ — Kessen II probe files

Declarative bring-up diagnostics for `kessen2` (format and limits: [ADR-0006](../docs/adr/0006-probe-diagnostics.md); engine: [`diag/`](../diag/README.md)). Each file holds only guest addresses and probe settings, never game data.

```sh
K2_PROBE=probes/bringup-10-mpeg-create-status.probe K2_PROBE_LOG=out/probe/b10.log \
  out/build/msvc-x64/app/Release/kessen2.exe --headless --frames 2400
```

Without `K2_PROBE_LOG`, lines go to stderr with the prefix `[k2probe] `. Exit 5 means the probe could not be set up, and the game does not run. Causes: a malformed or unreadable spec; a `func` address with no recompiled function; `K2_PROBE` set but empty, or set in a build without recompiled game code; `K2_PROBE_LOG` cannot be created. The stderr line names which one. Keep runs at 5,000 frames or fewer. Screenshots go to the working directory.

## Format (v1)

```
k2probe 1                                   # required first directive
func 0x1b6670 calls=64 nonzero=256 word=0x11ef480
arm 0x1085d0 after=2                        # must also be a func
watch 0x1113320:4 every=600                 # ADDR:LEN, LEN hex, multiple of 4, <= 0x40
attrib 0x11ef480:4                          # write attribution, LEN hex, <= 0x1000 total
attrib-max-events 100000
screenshot every=150 from=0
gsregs every=600 from=0                     # GS display registers + presenter choice
gsevents every=1 from=2400                  # GS draws/uploads since the last dump
```

All numbers in addresses and lengths are hex (`0x` optional); counts (`calls`, `nonzero`, `after`, `every`, `from`, `attrib-max-events`) are decimal.

## Output

One line per event, `frame=F seq=N kind=K key=value…` (`frame` = presented frames so far):

| kind | from | fields |
|---|---|---|
| `probe`, `func-installed`, `attrib-range` | install | what was loaded |
| `call` / `ret` | `func` | `fn n a0 a1 a2 a3 ra gp sp [word[ADDR]]` / `fn n pc v0 [word[ADDR]]`; `nonzero=1` past the window |
| `calls` | `func` | `fn calls=N` at each power of two past the window |
| `armed` | `arm` | `fn call` |
| `watch` | `watch` | `addr value dec`, in address order |
| `write` | `attrib` | `addr old new writer=FN|unattributed [site=PC] edge=enter|exit` |
| `attrib-truncated` | `attrib` | event cap reached (the summary keeps tracking) |
| `last-write` | end of run | per-byte last writer, in address order: `addr old new writer=FN\|unattributed [site=PC] edge=enter\|exit at_frame=F`; `at_frame` is the frame the write was seen, the line's own `frame=` is the end of the run |
| `attrib-stats` | end of run | totals: `changes events unattributed resyncs truncated=0\|1` |
| `screenshot` | `screenshot` | `file` |
| `gsevents` | `gsdraw` / `gsxfer` | one line per GS draw (`prim tme verts fbp fbw tbp0 tbw tpsm tw th x y` ranges) or image transfer (`dir dbp dbw dpsm dsa rr pixels`) recorded since the previous dump; the engine keeps only the last 512 GS events, so dump every frame for busy scenes |
| `gsregs` | `gsregs` | `pmode smode2 dispfb1 display1 dispfb2 display2` (raw 64-bit), then the presenter's last pick: `present_fbp source_fbp preferred=0\|1 w h`, and rows of that latched frame with any non-black pixel: `latched=0\|1 lit_rows lit_first lit_last` |
| `end` | end of run | — |

"Who last wrote ADDR before frame F": `python tools/probe/reverse_watch.py --address 1113320:4 --before-frame 1200 --repeat 2`.

## Files

| File | Use |
|---|---|
| `bringup-08-movie-vblank.probe` | BRINGUP #8: vblank handler rate, movie frame counter |
| `bringup-09-mpeg-nodata.probe` | BRINGUP #9: Nodata callback / ring feed / `sceMpegIsEnd`, logo screenshots |
| `bringup-10-mpeg-create-status.probe` | BRINGUP #10: libmpeg status word across `sceMpegCreate`, its last writer |
| `bringup-11-movie-display.probe` | BRINGUP #11 (open): movie screenshots and frame counter |
| `cd-reads.probe` | first 1,024 `sceCdRead` calls in full (LSN `a0`, sectors `a1`, destination `a2`, caller `ra`), then counts at powers of two (replaces `K2_TRACE_CD=1`); for later reads add `arm 0x114f40 after=N` to start the window at read N |
| `smoke.probe` | `k2_probe_smoke` test: every probe kind on a short run |
