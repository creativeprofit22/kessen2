# AGENTS.md

Kessen II PS2 static-recompilation port (planned C++/CMake, see `.gg/plans/`).

## Commands
- Configure/build/test (Windows, VS 2022): `cmake --preset msvc-x64` → `cmake --build --preset msvc-x64-debug --parallel` → `ctest --preset msvc-x64`.
- Guard: `sh tools/guard/check-forbidden.sh --all`; install the hook once with `sh tools/guard/install-hooks.sh`.
- Module dependency rule is enforced at configure time (`cmake/K2Modules.cmake`, see `docs/ARCHITECTURE.md`).

## Rules
- CI lives in `.github/workflows/ci.yml` and must stay green.
- Never commit with `--no-verify`.
- Never commit game data (ISO, ELF, IRX, extracted assets) or `generated/` output — the repo is public.
