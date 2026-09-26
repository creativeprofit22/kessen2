# kessen2

An unofficial native PC port of **Kessen II** (PS2, 2001) by static recompilation with [PS2Recomp](https://github.com/ran-j/PS2Recomp).

> **No game content is included.** You need your own legally owned copy of the game; every game-derived file is produced locally and never committed. See [docs/LEGAL.md](docs/LEGAL.md).

**Status:** Phase 1 — foundation only (module skeleton, build, guardrails). The game does not boot yet.

## Build (Windows)

Requirements: Visual Studio 2022 (Build Tools OK) with MSVC x64, CMake ≥ 3.21, Git.

```sh
git clone --recurse-submodules https://github.com/creativeprofit22/kessen2.git
cd kessen2
sh tools/guard/install-hooks.sh          # once per clone: blocks game data commits
cmake --preset msvc-x64
cmake --build --preset msvc-x64-debug
ctest --preset msvc-x64
out/build/msvc-x64/app/Debug/kessen2.exe --version
```

The first configure/build downloads SDL3, raylib, Dear ImGui and an FFmpeg prebuilt.

## Docs

- [Architecture & dependency rule](docs/ARCHITECTURE.md)
- [Dependencies & pins](docs/DEPENDENCIES.md)
- [Legal](docs/LEGAL.md)
- [Decisions (ADRs)](docs/adr/)
- [Module index](docs/modules/README.md)

## Licence

GPL-3.0-or-later — see [LICENSE](LICENSE). Kessen is a trademark of Koei Tecmo; this project is not affiliated with Koei Tecmo or Sony.
