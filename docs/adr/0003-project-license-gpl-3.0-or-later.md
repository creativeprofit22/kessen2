# ADR-0003: Project licence GPL-3.0-or-later

- **Status:** Accepted (project owner decision, 2026-09-26)
- **Date:** 2026-09-26

## Context

`kessen2.exe` statically links PS2Recomp's `ps2_runtime`, which is GPL-3.0. Any distributed binary is therefore subject to GPL-3.0 terms regardless of how our own sources are licensed.

## Decision

License this project's own code as **GPL-3.0-or-later**. Full text in [LICENSE](../../LICENSE); own source files carry `SPDX-License-Identifier: GPL-3.0-or-later`.

## Consequences

- Consistent with the binaries we can actually ship; no dual-licence confusion.
- "Or later" allows moving to a future GPL version if PS2Recomp does.
- Contributors' code is GPL too; permissive reuse of our modules outside GPL projects is not possible.
- Game content is not covered by any of this — it is never in the repo ([LEGAL.md](../LEGAL.md)).

## Alternatives considered

- **MIT / Apache-2.0 for our sources** — legally possible for source files, but every binary is GPL anyway; the mismatch invites confusion.
- **GPL-3.0-only** — needlessly blocks future GPL versions.
