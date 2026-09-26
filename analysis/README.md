# analysis — reverse-engineering inputs for the recompiler

**Purpose.** Hold the *human-authored* knowledge about the boot ELF, plus the scripts that regenerate the derived part. Ghidra (with the Emotion Engine plugin) findings are exported as function lists / recompiler configuration (`*.toml`, `*.csv`) that drive `ps2_recomp`.

**Contents.**
- `run-analysis.sh` — one-command headless pipeline: SDK signature scan → Ghidra import/analysis → SDK naming → reports → PS2Recomp's `ExportPS2Functions.java`. Usage, setup and findings: [docs/modules/analysis.md](../docs/modules/analysis.md).
- `ghidra/` — the project's Ghidra scripts (`K2SetAnalysisOptions`, `K2ApplySdkNames`, `K2Report`).

**Inputs.** Boot ELF in `work/` (untracked), Ghidra 12.1.3 + ghidra-emotionengine-reloaded v2.1.37, JDK 21 (see `docs/DEPENDENCIES.md`).

**Outputs.** `analysis/kessen2.toml` (recompiler config) and `analysis/out/` (function map CSV, reports, Ghidra project), all git-ignored. `ps2_recomp` consumes them to produce `generated/`.

**Allowed dependencies.** None at build time; these are data files and offline scripts.

**Forbidden.** Ghidra project databases (`*.gzf`, `*.rep`, `*.gpr` — ignored), copies of the ELF, disassembly/decompilation dumps of game code, embedded game bytes. Whether derived tables (`*.toml`, `*.csv`) may be committed is **pending a policy decision**. Until then they are ignored and regenerated with `run-analysis.sh`.
