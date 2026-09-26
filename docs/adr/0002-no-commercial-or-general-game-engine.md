# ADR-0002: No commercial or general-purpose game engine

- **Status:** Accepted
- **Date:** 2026-09-26

## Context

It is tempting to host the port inside Godot, Unreal or Unity for rendering, input and packaging.

## Decision

Do not. The host is a thin C++ layer: SDL3 for platform services (`platform/`) and a custom renderer for GS/VU output (`render/`), composed in `app/`.

## Consequences

- The recompiled game **is** the engine: it owns the main loop, timing and frame submission. Engines that want to own the loop would have to be bent around it.
- Matches how existing static-recompilation ports are built (SDL plus a custom renderer).
- No licence conflicts: Unreal's and Unity's proprietary terms are incompatible with GPL-3.0 code such as PS2Recomp; Godot (MIT) is compatible but adds a large runtime for no gain.
- We write our own renderer; that cost is accepted.

## Alternatives considered

- **Unreal / Unity** — proprietary licences incompatible with GPL distribution; engine loop ownership conflict.
- **Godot** — licence-compatible, but loop ownership and GDExtension plumbing outweigh the benefits.
- **Keep upstream raylib host** — fine for bring-up (it is linked today through `ps2_runtime`), but we want control over the renderer; `render/` will replace it gradually.
