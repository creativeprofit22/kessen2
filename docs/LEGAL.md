# Legal

_Not legal advice. This describes the project's rules, not a legal opinion._

## Bring your own disc

This project contains **no part of Kessen II**. To use it you must supply your own legally owned copy of the game and produce every game-derived file locally on your own machine.

## Never in the repository or in releases

- Disc images (`.iso`, `.bin`, `.img`, `.cue`, `.mdf`, `.nrg`, `.cso`, `.chd`, …)
- Game executables and modules: boot ELF (`SLPM_*`, `SLUS_*`, …), any `.elf`, `.irx`
- Extracted or converted assets (`work/`, `extracted/`, `dump/`, `assets_dumped/`)
- Recompiler output (`generated/`, except its README) — it is a translation of the game's code
- Disassembly/decompilation dumps and Ghidra project databases
- Save data / memory card images (`.mc2`, `.ps2`)
- BIOS or other console firmware (BIOS dumps and their side files: `.bin`, `.rom0`, `.rom1`, `.rom2`, `.erom`, `.nvm`, `.mec`)

Release binaries contain only this project's code and its open-source dependencies; they cannot run the game without the user's own locally generated files.

## Guardrails

| Layer | What it does |
|---|---|
| `.gitignore` | Ignores the file types and directories above that have a recognisable name or extension (disassembly dumps don't — never add them) |
| `tools/guard/check-forbidden.sh` | Rejects forbidden paths, ELF / ISO9660 / PS2 memory card content regardless of file name, files > 20 MB, and generated-code includes in game-agnostic modules |
| `pre-commit` hook | Runs the check on staged files. Install once per clone: `sh tools/guard/install-hooks.sh`. Never bypass it with `--no-verify`. |
| CI (`.github/workflows/guard.yml`, `ci.yml`) | `guard.yml` runs `check-forbidden.sh --all` and its self-test on pushes and PRs to `main`; `ci.yml` adds a separate, narrower file-name check |
| `ctest` `k2_guard` | Proves the check still catches each case |

If something forbidden is ever pushed, do not just delete it in a new commit — history must be rewritten and the maintainers informed.

## Licences

- This project: **GPL-3.0-or-later** ([LICENSE](../LICENSE), [ADR-0003](adr/0003-project-license-gpl-3.0-or-later.md)).
- PS2Recomp is GPL-3.0; binaries that link `ps2_runtime` are distributed under GPL-3.0 terms, which our licence is compatible with.
- Third-party components and their licences: [DEPENDENCIES.md](DEPENDENCIES.md).

## Trademarks

"Kessen" is a trademark of Koei Tecmo. "PlayStation" and "PS2" are trademarks of Sony Interactive Entertainment. This is an unofficial fan project, not affiliated with or endorsed by either company.
