# analysis — reverse-engineering inputs for the recompiler

**Purpose.** Hold the *human-authored* knowledge about the boot ELF: Ghidra (with the Emotion Engine plugin) findings exported as function lists / recompiler configuration (`*.toml`, `*.csv`) that drive `ps2_recomp`.

**Inputs.** Boot ELF in `work/` (untracked), Ghidra 12.1.3 + ghidra-emotionengine-reloaded (see `docs/DEPENDENCIES.md`).

**Outputs.** Recompiler config and symbol/function tables consumed by `ps2_recomp` to produce `generated/`.

**Allowed dependencies.** None at build time; these are data files.

**Forbidden.** Ghidra project databases (`*.gzf`, `*.rep`, `*.gpr` — ignored), copies of the ELF, disassembly/decompilation dumps of game code, embedded game bytes. Whether derived tables (`*.toml`, `*.csv`) may be committed is **pending a policy decision** — until then keep them out of commits.
