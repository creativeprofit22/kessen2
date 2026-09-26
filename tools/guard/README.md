# tools/guard — keep game data out of git

**Purpose.** Block commits of disc images, ELFs, IRX modules, memory cards, extracted assets, generated code and oversized files; enforce "no generated includes" in `platform/`, `render/` and `runtime-ext/`.

**Inputs.** The git index (`--staged`) or all tracked files (`--all`).

**Outputs.** Exit status 0 (clean) / 1 (violations listed on stderr).

**Allowed dependencies.** POSIX `sh`, `git`, `grep`, `od`/`dd` (Git for Windows ships all of them).

**Forbidden.** Modifying the repo or git config. `install-hooks.sh` only copies `pre-commit` into `.git/hooks/`.

Usage: `sh tools/guard/install-hooks.sh` once per clone; CI runs `check-forbidden.sh --all`; `ctest` runs `test_guard.sh`.
