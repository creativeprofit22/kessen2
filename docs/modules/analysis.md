# Static analysis — Ghidra + PS2Recomp config

One command analyses the combined ELF (boot ELF + main code overlay) headlessly (no Ghidra GUI) and produces the TOML config and CSV function map that `ps2_recomp` consumes:

```sh
sh tools/recomp/make-combined-elf.sh   # once per disc extraction, see recomp.md
sh analysis/run-analysis.sh            # ~2.5 min after the first run
```

All outputs are git-ignored (see [Outputs](#outputs)). Script sources live in [`analysis/`](../../analysis/README.md).

## Setup (exact versions)

Everything is portable (unzipped, no installers, no system `PATH`/`JAVA_HOME` changes). Pins and digests are also in [DEPENDENCIES.md](../DEPENDENCIES.md).

| Tool | Version / asset | sha256 | Where |
|---|---|---|---|
| Ghidra | 12.1.3 — `ghidra_12.1.3_PUBLIC_20260817.zip` | `93a5d11a9ad510622acaaf908c556a7b9b764d338e78a7567f3689bf5081fd54` | `E:/Tools/ghidra_12.1.3_PUBLIC` |
| ghidra-emotionengine-reloaded | v2.1.37 — `ghidra_12.1.3_PUBLIC_20260825_ghidra-emotionengine-reloaded.zip` | `3af7641174b470bf19ec32b72b3929dbd21ec3f11d66894d2316be044fac1258` | unzipped into `<ghidra>/Ghidra/Extensions/` (install-level, so headless loads it) |
| Temurin JDK | 21.0.12.1+1 — `OpenJDK21U-jdk_x64_windows_hotspot_21.0.12.1_1.zip` | `f9d6e191ab098c0d416e7d588a24420a8621cd2f4720dab2459b8b7b2d2d8b4e` | `E:/Tools/jdk-21.0.12.1+1` (Ghidra 12.1.3 needs `application.java.min=21`) |
| `ps2_analyzer` | PS2Recomp submodule pin `75d729ce` | — | built by the script into `out/build/ps2recomp-tools` if missing |

Install steps:

1. Download the three zips from their GitHub releases and check `sha256sum` against the table.
2. Unzip Ghidra and the JDK into `E:/Tools/` (or anywhere — see the environment variables below).
3. `unzip <ee-zip> -d <ghidra>/Ghidra/Extensions` — the result must be `<ghidra>/Ghidra/Extensions/ghidra-emotionengine-reloaded/extension.properties`.
4. Extract the boot ELF with [`k2disc`](disc.md) so it is at `work/disc/SLUS-20275/iso/SLUS_202.75`.

Why 12.1.3 and not 12.1.4 (released 2026-09-21): the EE extension's latest release, v2.1.37, only ships a 12.1.3 build. Extensions are tied to an exact Ghidra version.

Environment variables (all optional):

| Variable | Default |
|---|---|
| `GHIDRA_INSTALL_DIR` | `E:/Tools/ghidra_12.1.3_PUBLIC` |
| `K2_JAVA_HOME` | `E:/Tools/jdk-21.0.12.1+1` |
| `K2_ELF` | `work/recomp/SLUS_202.75.combined.elf` — built by `tools/recomp/make-combined-elf.sh` (boot ELF + the main code overlay, see [recomp.md](recomp.md)); the script fails if it is missing. Relative to the repo root or absolute. Set `K2_ELF=work/disc/SLUS-20275/iso/SLUS_202.75` explicitly to analyse the retail boot ELF alone (the findings below); `run-recomp.sh` then refuses the TOML unless `K2_ALLOW_RETAIL_ELF=1` |
| `K2_PS2_ANALYZER` | first existing of `out/build/ps2recomp-tools/ps2xAnalyzer/Release/ps2_analyzer(.exe)` (Release/ subdir on multi-config generators) and `out/build/ps2recomp-tools/ps2xAnalyzer/ps2_analyzer` (single-config generators) |
| `K2_GENERATED_DIR` | `generated` (repo root) — written to the TOML's `[general] output`; set it to the same path as CMake's `KESSEN2_GENERATED_DIR` |

The script refuses to run if the Ghidra version isn't 12.1.3, the EE extension is missing, the JDK isn't 21, the ELF or PS2Recomp submodule is missing, or `K2_GENERATED_DIR` is inside the repository but isn't exactly `<repo>/generated` (the same rule CMake enforces for `KESSEN2_GENERATED_DIR`).

## What the script does

1. **SDK signature scan.** `ps2_analyzer` (PS2Recomp) matches functions against its embedded SCE SDK database (masked-hash signatures of libkernl, libcdvd, libgraph, libdma, libpad, libmc, libmpeg, libvu0, …). Every `name@0xADDR` selector it emits becomes `analysis/out/sdk-names.csv`.
2. **Ghidra headless** (one `analyzeHeadless` call):
   - import with language **`r5900:LE:32:default`** (processor `MIPS-R5900`, little-endian, 32-bit, compiler spec `default`, from the EE extension)
   - preScript `K2SetAnalysisOptions.java` pins the options below and dumps all 132 effective options to `reports/analyzer-options.txt`
   - auto-analysis
   - `K2ApplySdkNames.java` applies the SDK names (source `IMPORTED`). It only renames default `FUN_*` functions or creates missing ones, and never overwrites a symbol-table or STABS name.
   - `K2SplitFarChunks.java` detaches body chunks that Ghidra folded into a function across other functions (a shared `jr $ra` tail-jump target). Without it the exporter's `[entry, max body address]` range turned one 60-byte function into a 160 KB span and a 12 MB C++ function. Log: `reports/split-far-chunks.txt`.
   - `K2Report.java` writes the reports
   - PS2Recomp's `ps2xRecomp/tools/ghidra/ExportPS2Functions.java` (unmodified). Its two `askFile` prompts are answered by script arguments in headless mode.
3. **Config fix-up.** Rewrites `[general] input`/`output` in the TOML to this checkout's ELF and `generated/`.

Ghidra scripts are copied into one staging directory, because `cmd.exe` would split a `;`-separated `-scriptPath`.

### Analyzer options

Only three options are changed from Ghidra 12.1.3 + EE defaults:

| Option | Value | Why |
|---|---|---|
| `STABS` | on | EE analyzer that imports `.mdebug` STABS symbols/types; a no-op when there is no `.mdebug` |
| `MIPS-R5900 Constant Reference Analyzer` | on | R5900 constant propagation: creates references for `lui`/`addiu` pairs and GP-relative accesses |
| `Decompiler Parameter ID` | **off** | EE extension README advises disabling it when decompilation fails; it also dominates run time |

Effective top-level analyzers for the recorded run (sub-options are in `reports/analyzer-options.txt`):

| On | Off |
|---|---|
| ASCII Strings, Apply Data Archives, Call Convention ID, Call-Fixup Installer, Create Address Tables, Data Reference, Decompiler Switch Analysis, Demangler GNU, Disassemble Entry Points, Embedded Media, External Entry References, Function ID, Function Start Search, MIPS-R5900 Constant Reference Analyzer, MIPS-R5900 Unaligned Instruction Fix, Non-Returning Functions - Discovered, Non-Returning Functions - Known, Reference, STABS, Shared Return Calls, Stack, Subroutine References | Aggressive Instruction Finder, Condense Filler Bytes, Decompiler Parameter ID, ELF Scalar Operand References, Variadic Function Signature Override |

## Outputs

| Path | Content |
|---|---|
| `analysis/kessen2.toml` | PS2Recomp config: `[general]` input/output/ghidra_output, `stubs`, `untracked_stubs`, `[ghidra_export]` counts |
| `analysis/out/kessen2_functions.csv` | Function map (`Name,Start,End,Size`), referenced by the TOML's `ghidra_output` |
| `analysis/out/ps2_analyzer.toml`, `sdk-names.csv` | Raw SDK scan and the extracted name list |
| `analysis/out/reports/summary.txt` | Memory blocks, ELF section types, `.mdebug`/`.symtab` findings, function counts by source, SDK families, VU0 totals |
| `analysis/out/reports/sdk-functions.csv` | `address,name,source` of every SDK-named function |
| `analysis/out/reports/vu0-sites.csv` / `vu0-functions.csv` | Every COP2 instruction; per-function counts (most sites first) |
| `analysis/out/reports/analyzer-options.txt`, `sdk-naming.txt`, `*.log` | Effective options, naming stats, Ghidra and script logs |
| `analysis/out/ghidra/` | Ghidra project (for opening in the GUI later) |

These are derived from game code. They stay **uncommitted** until the derived-data policy is decided ([analysis/README.md](../../analysis/README.md)). Only aggregate numbers and Sony library identifiers are recorded below.

## Findings (SLUS-20275, 1,371,008-byte boot ELF)

Two consecutive clean runs produced byte-identical outputs (TOML, CSV and every report).

**Encryption/packing: none.** It's a plain MIPS ELF, and Ghidra loads it as one RWX segment at `0x00100000`–`0x0024C57F`, entry `0x00100008`.

**Symbols: stripped; no `.mdebug`/STABS.** The section header table has 122 entries: 117 `PROGBITS`, 2 `STRTAB`, 1 `SYMTAB`, 1 `MIPS_REGINFO` and the null entry. `.symtab` and `.strtab` are present but **0 bytes**. No section has type `SHT_MIPS_DEBUG` (0x70000005) or the name `.mdebug`, so the STABS analyzer (enabled) has nothing to import. All names therefore come from signature matching.

**Functions:**

| | Count |
|---|---|
| Total functions | 2,385 |
| Named | 574 — 520 SDK signature names + `entry` (`IMPORTED` 521) + 53 names generated by Ghidra's own analyzers (`ANALYSIS`) |
| Unnamed (`FUN_*`) | 1,811 |
| SDK names applied | 520: 461 renamed default functions + 59 newly created, 0 conflicts, 0 failures |

`ps2_analyzer` reports 521 matches; the 521st is the ELF entry point, which it doesn't emit as a selector. `ExportPS2Functions` classifies 297 names as `stubs` (runtime has a handler) and 33 as `untracked_stubs`. It exports 15,098 CSV records: functions plus code labels.

**SDK families named** (function count):

| Family | # | Family | # |
|---|---|---|---|
| `sceSif*` (incl. `_sceSifLoadModule`, `_sceSifLoadModuleBuffer`, RPC, IOP heap) | 39 | `sceMpeg*` | 13 |
| `scePad*` | 28 | `sceDeci2*` | 9 |
| `sceVu0*` | 28 | `sceFs*` | 6 |
| `sceCd*` (incl. `sceCdRead`, `sceCdSt*` streaming) | 26 | `sceIpu*`, `sceTty*` | 4 each |
| `sceMc*` | 20 | `sceRpc*` | 2 |
| `sceGs*` | 14 (+`sceGszbufaddr`) | single-prefix `sce*` (file I/O: `sceOpen`, `sceClose`, `sceLseek`, …) | 29 |
| `sceDma*` | 14 | non-`sce` kernel syscalls / libc / crt | 284 |

Gap: the public wrapper `sceSifLoadModule` is not matched, although the `_sceSifLoadModule` implementation it calls is. That wasn't worth a string-cross-reference fallback. Most of the 1,811 unnamed functions are game code.

**COP2 / VU0 macro usage** (first signal for VU work). Sites are classified by raw opcode: COP2 primary op `0x12` with the CO bit set = macro op, `LQC2`/`SQC2` = load/store, `QMFC2`/`QMTC2`/`CFC2`/`CTC2` = transfer. No `bc2*` branches or other COP2 forms were found.

| | Count |
|---|---|
| COP2 instructions (total) | 2,862 |
| macro-mode VU0 ops (`vadd`, `vmadd`, `vmul`, `vnop`, …) | 1,587 |
| `lqc2` / `sqc2` | 1,064 |
| register transfers | 211 |
| functions containing COP2 | 101 (88 with macro ops); 28 are the named `sceVu0*` library |
| sites outside any function | 56 |

Usage is concentrated: the top two functions (unnamed game code) hold 691 of the sites. The per-address list is in `vu0-functions.csv` for the VU phase.

## Follow-ups

- **Game-function naming.** The SDK database only covers Sony libraries. Bulk naming of the 1,811 unnamed functions could be driven by an AI agent through [ghidra-mcp](https://github.com/bethington/ghidra-mcp) or [reverse-engineering-assistant](https://github.com/cyberkaida/reverse-engineering-assistant) against `analysis/out/ghidra/kessen2.gpr`. Not needed for recompilation.
- **Commit policy.** Decide whether `analysis/kessen2.toml` / the CSV may be committed; until then, the config is regenerated locally by the script.
