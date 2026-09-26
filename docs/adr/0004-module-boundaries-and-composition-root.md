# ADR-0004: Module boundaries and a single composition root

- **Status:** Accepted
- **Date:** 2026-09-26

## Context

PS2Recomp's `ps2EntryRunner` target globs generated code from `ps2xRuntime/src/runner/*.cpp` — i.e. it expects recompiler output to be written **inside the submodule**. That would dirty the submodule, mix game-derived code with upstream code, and make it easy to commit forbidden files. We also want the platform and renderer to stay game-agnostic.

## Decision

- PS2Recomp is consumed as an untouched submodule via `add_subdirectory(... EXCLUDE_FROM_ALL)`; `ps2EntryRunner` is never built.
- Generated code lives in top-level `generated/` (git-ignored, overridable with `KESSEN2_GENERATED_DIR`) and becomes `k2_generated` only when sources exist.
- `app/` (`kessen2`) is the **only** target allowed to link everything; each other module has an allowlist (see [ARCHITECTURE.md](../ARCHITECTURE.md#dependency-rule)).
- Rules are enforced mechanically by `cmake/K2Modules.cmake` at configure time and by `tools/guard/check-forbidden.sh` for source includes, both covered by `ctest`.

## Consequences

- A forbidden `target_link_libraries` fails configuration immediately with the offending edge named.
- `app/` must reimplement what `ps2EntryRunner`'s `main` does (boot ELF path, runtime init, debug UI wiring) — accepted, it is small.
- Game-specific logic has one home: `runtime-ext/`.
- Adding a module means one `k2_add_module()` call and a README with the standard sections.

## Alternatives considered

- **Write generated code into the submodule and use `ps2EntryRunner`** — dirties the submodule and blurs the legal boundary.
- **Fork PS2Recomp** — ongoing merge cost; revisit only if upstream blocks us.
- **Convention-only boundaries** — erode silently; enforcement is cheap.
