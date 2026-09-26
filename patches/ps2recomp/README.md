# PS2Recomp patches (proposed upstream, not applied)

Generic fixes found during Kessen II bring-up that belong in [ran-j/PS2Recomp](https://github.com/ran-j/PS2Recomp), not in this repo. The build **does not apply them**: the submodule stays at the pinned commit and `runtime-ext` carries an equivalent Kessen-scoped workaround until upstream takes the fix.

Each patch is made with `git diff` inside `external/PS2Recomp` against the pinned commit (see [DEPENDENCIES.md](../../docs/DEPENDENCIES.md)). Check that one still applies:

```sh
git -C external/PS2Recomp apply --check ../../patches/ps2recomp/NNNN-name.patch
```

| Patch | Fixes | Workaround in runtime-ext | BRINGUP.md |
|---|---|---|---|
| `0001-gs-vsync-callback-cause.patch` | `sceGsSyncVCallback` callbacks got `a0` = vsync tick; libgraph installs them as INTC VBLANK_S handlers, so the argument is the interrupt cause (`2`). Handlers that check the cause never ran. | `src/fixes/gs_vsync_callback_cause.cpp` | #5 |

When upstream merges a fix: bump the submodule, delete the patch row and file, remove the workaround, and mark the BRINGUP.md row `fixed (upstream)`.
