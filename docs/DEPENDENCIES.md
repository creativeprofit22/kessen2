# Dependencies

Checked **2026-09-26**. Update the date and pins together.

| Name | Version / commit | Licence | How consumed | Notes |
|---|---|---|---|---|
| [PS2Recomp](https://github.com/ran-j/PS2Recomp) | `75d729ce40d7eed9649fd4bb05628dee520f3d0c` (main, 2026-09-19, "Feature/iop emulator (#244)") | GPL-3.0 | git submodule `external/PS2Recomp`, `add_subdirectory(... EXCLUDE_FROM_ALL)` | Only `ps2xIOP` + `ps2xRuntime` are configured (see below) |
| [SDL](https://github.com/libsdl-org/SDL) | 3.4.16 — tag `release-3.4.16`, commit `fa2c02bb6e21974a89ea9824bc53c9932abe5f9c` | Zlib | `FetchContent` by SHA, shared (`SDL3.dll` copied beside `kessen2.exe`) | Used only by `k2_platform` |
| [raylib](https://github.com/raysan5/raylib) | 5.5 (`c1ab645c…`) | Zlib | Transitive, fetched by PS2Recomp | Upstream host backend |
| [Dear ImGui](https://github.com/ocornut/imgui) | v1.92.7-docking (`b1bcb12a…`) | MIT | Transitive, fetched by PS2Recomp | Upstream debug UI |
| [rlImGui](https://github.com/raylib-extras/rlImGui) | tag `Raylib_5_5` (`118221c8…`) | Zlib | Transitive, fetched by PS2Recomp | Upstream debug UI |
| FFmpeg ([msvc prebuilt](https://github.com/System233/ffmpeg-msvc-prebuilt)) | n7.1-241205, LGPL shared build | LGPL-2.1+ | Transitive, downloaded by PS2Recomp at build time on Windows | Its DLLs (plus bundled SDL2/OpenEXR/etc.) are staged beside `kessen2.exe` |
| [Ghidra](https://github.com/NationalSecurityAgency/ghidra) | **12.1.3** — `ghidra_12.1.3_PUBLIC_20260817.zip`, sha256 `93a5d11a9ad510622acaaf908c556a7b9b764d338e78a7567f3689bf5081fd54` | Apache-2.0 | Portable unzip outside the repo (analysis only, not built) | 12.1.4 (2026-09-21) exists but the EE plugin has no build for it yet — stay on 12.1.3 |
| [ghidra-emotionengine-reloaded](https://github.com/chaoticgd/ghidra-emotionengine-reloaded) | v2.1.37 (2026-08-25) — `ghidra_12.1.3_PUBLIC_20260825_ghidra-emotionengine-reloaded.zip`, sha256 `3af7641174b470bf19ec32b72b3929dbd21ec3f11d66894d2316be044fac1258` | Apache-2.0 | Unzipped into `<ghidra>/Ghidra/Extensions/` | Still the latest release; newest Ghidra it supports is 12.1.3 |
| [Eclipse Temurin JDK](https://github.com/adoptium/temurin21-binaries) | 21.0.12.1+1 — `OpenJDK21U-jdk_x64_windows_hotspot_21.0.12.1_1.zip`, sha256 `f9d6e191ab098c0d416e7d588a24420a8621cd2f4720dab2459b8b7b2d2d8b4e` | GPL-2.0 + CE | Portable unzip, passed to the analysis driver via `K2_JAVA_HOME` | Ghidra 12 needs JDK 21; no system install |
| `ps2_analyzer` (from PS2Recomp, same pin) | `75d729ce…` | GPL-3.0 | Standalone build into `out/build/ps2recomp-tools` by `analysis/run-analysis.sh` | Supplies the SCE SDK signature matches used to name library functions |
| CMake | ≥ 3.21 (tested 4.4.2) | BSD-3-Clause | Build tool | |
| MSVC | 14.44.35207 (VS 2022 Build Tools 17.14), generator `Visual Studio 17 2022` | Proprietary | Toolchain | Ninja not assumed |

## Known upstream issues at this pin

- **`ps2_recomp` cannot be built as a subproject.** `ps2xRecomp/CMakeLists.txt` does `include("${CMAKE_SOURCE_DIR}/ps2xRuntime/cmake/ReleaseMode.cmake")`, which only resolves when PS2Recomp is the top-level project. The superbuild therefore sets `PS2X_BUILD_RECOMP=OFF`. Build the tool standalone when needed (Phase 4):

  ```sh
  cmake -S external/PS2Recomp -B out/build/ps2recomp -G "Visual Studio 17 2022" -A x64 \
        -DPS2X_BUILD_RUNTIME=OFF -DPS2X_BUILD_ANALYZER=OFF -DPS2X_BUILD_TEST=OFF -DPS2X_BUILD_STUDIO=OFF
  cmake --build out/build/ps2recomp --config Release --target ps2_recomp
  ```
  _(Not yet verified; do so in Phase 4.)_
- `FetchContent_Populate` deprecation warnings (CMP0169) from `ps2xRuntime/CMakeLists.txt` — harmless with CMake 4.4.
- Upstream `ps2EntryRunner` links with `/FORCE:MULTIPLE` for a WinAPI/raylib `CloseWindow` clash. `kessen2` does not need it yet (it references no raylib symbols); revisit when the runtime is actually driven.

- **`ps2_analyzer` standalone build** (used by `analysis/run-analysis.sh`, which runs it automatically if missing):

  ```sh
  cmake -S external/PS2Recomp -B out/build/ps2recomp-tools -G "Visual Studio 17 2022" -A x64 \
        -DPS2X_BUILD_RECOMP=ON -DPS2X_BUILD_ANALYZER=ON -DPS2X_BUILD_RUNTIME=OFF -DPS2X_BUILD_TEST=OFF -DPS2X_BUILD_STUDIO=OFF
  cmake --build out/build/ps2recomp-tools --config Release --target ps2_analyzer
  ```

  The script's on-demand build uses the platform-default generator (no `-G`/`-A`) with `-DCMAKE_BUILD_TYPE=Release`, so on Linux/macOS the binary lands in `ps2xAnalyzer/` rather than `ps2xAnalyzer/Release/`; the script checks both.

## Updating a pin

1. `git -C external/PS2Recomp fetch && git -C external/PS2Recomp checkout <sha>` (or change the SDL `GIT_TAG` SHA in the root `CMakeLists.txt`).
2. Reconfigure, build, `ctest --preset msvc-x64`.
3. Update this table and the check date.
