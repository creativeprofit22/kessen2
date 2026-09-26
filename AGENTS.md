# AGENTS.md

Kessen II PS2 static-recompilation port (planned C++/CMake, see `.gg/plans/`).

## Commands
- Build/test/lint: none yet — no `CMakeLists.txt` exists. Once it does, the planned commands are
  `cmake --preset <preset>` → `cmake --build --preset <preset>` → `ctest --preset <preset>`.

## Rules
- CI lives in `.github/workflows/ci.yml` and must stay green.
- Never commit with `--no-verify`.
- Never commit game data (ISO, ELF, IRX, extracted assets) or `generated/` output — the repo is public.
