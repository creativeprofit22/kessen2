# diag/ — probe diagnostics (`k2_diag`)

Design and limits: [ADR-0006](../docs/adr/0006-probe-diagnostics.md). Probe files for this game: [probes/](../probes/README.md).

## Purpose

Declarative, versioned, fail-closed bring-up diagnostics for a PS2Recomp game: function entry/return logging, periodic RAM watches, write attribution ("which recompiled function changed this range") and periodic screenshots, all driven by one probe spec file (`K2_PROBE=path`). Replaces ad-hoc environment variables and temporary edits in the engine build copy.

## Inputs

- A probe spec (plain text, `k2probe 1`, ≤ 64 KiB), parsed by `parse_probe_spec` / `load_probe_file` (`include/k2/diag/probe_spec.h`). Any malformed line is an error naming the line.
- With the runtime binding: a loaded `PS2Runtime` (function table, RDRAM) and dispatch edges from `PS2Runtime::setDispatchObserver` (engine patch `0011-dispatch-observer.patch`).

## Outputs

- Line-oriented log (`K2_PROBE_LOG=path`, default stderr with prefix `[k2probe] `): `frame=F seq=N kind=K key=value…`. Watch rows and the final attribution summary are sorted by address.
- "Take a screenshot now" decisions (`FrameActions`); the app takes the screenshot.

## Layout

| Part | Files | Built |
|---|---|---|
| Pure (spec, attributor, log) | `probe_spec`, `write_attribution`, `probe_log` | always; unit tests `k2diag_*` |
| Runtime binding | `probe_session`, `func_probes` | only when `ps2_runtime` and `k2_generated` exist (`K2_DIAG_HAS_RUNTIME=1`) |

## Allowed dependencies

`ps2_runtime` only (`k2_add_module(k2_diag ALLOWS ps2_runtime)`).

## Forbidden

- Game-specific addresses, names or behaviour (those go in `probes/*.probe` or `runtime-ext/`).
- Generated code headers, raylib, SDL, `k2_render`, `k2_platform`.
- Being linked by `k2_platform`, `k2_render` or `k2_generated` (only `k2_runtime_ext` and the app may link it).
- Changing guest behaviour: probes only observe (function wrappers call the original and log).
