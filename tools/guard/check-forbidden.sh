#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Refuse game data, generated code, derived analysis output and oversized files in git.
#
#   check-forbidden.sh [--staged]   check files added/modified in the index (pre-commit)
#   check-forbidden.sh --all        check every file in the index (CI)
#
# Checks the *index* contents (what would be committed), not the working tree:
#   (a) forbidden paths / extensions, incl. generated/ and derived analysis output
#       (analysis/out/, analysis/*.toml, analysis/*.csv; commit policy pending)
#   (b) content magic: ELF, ISO9660, PS2 memory card
#   (c) files larger than 20 MB
#   (d) generated-code includes in platform/, render/, runtime-ext/
#   (e) the temporary-diagnostic marker (see AGENTS.md) in tracked code, patches, CMake and
#       scripts, and anywhere in the engine build copy out/build/*/_deps/ps2recomp-patched/src
#       when it exists: use a probe file (probes/) or a numbered patch instead
# POSIX sh + git + coreutils only (works in Git for Windows' hook shell and Linux CI).
set -eu

mode=${1:---staged}
case "$mode" in
    --staged|--all) ;;
    -h|--help) sed -n '2,18p' "$0"; exit 0 ;;
    *) echo "usage: $0 [--staged|--all]" >&2; exit 2 ;;
esac

top=$(git rev-parse --show-toplevel)
cd "$top"

MAX_BYTES=20971520   # 20 MB
# Built from pieces so this file does not match itself. Whole word, any case: "[MARKER]" and
# "// MARKER" match, identifiers such as "marker_test" do not.
MARKER="K2""DIAG"
tmp=$(mktemp "${TMPDIR:-/tmp}/k2guard.XXXXXX")
trap 'rm -f "$tmp"' EXIT HUP INT TERM

if [ "$mode" = "--all" ]; then
    git -c core.quotepath=off ls-files > "$tmp"
else
    git -c core.quotepath=off diff --cached --name-only --diff-filter=ACMR > "$tmp"
fi

fail=0
report() {
    echo "check-forbidden: $1: $2" >&2
    fail=1
}

while IFS= read -r path; do
    [ -n "$path" ] || continue

    # Skip submodule gitlinks (mode 160000): their content is not in this repo.
    entry_mode=$(git ls-files -s -- "$path" | head -n 1 | cut -d' ' -f1)
    [ "$entry_mode" = "160000" ] && continue

    lower=$(printf '%s' "$path" | tr 'A-Z' 'a-z')
    base=${lower##*/}

    # (a) paths and extensions
    case "$base" in
        *.iso|*.bin|*.img|*.cue|*.mdf|*.mds|*.nrg|*.cso|*.chd) report "$path" "disc image extension" ;;
        *.elf|*.irx) report "$path" "PS2 executable/module extension" ;;
        *.mc2|*.ps2) report "$path" "memory card / save data" ;;
        *.rom0|*.rom1|*.rom2|*.erom|*.nvm|*.mec) report "$path" "PS2 BIOS/firmware file" ;;
        *.gzf|*.gpr) report "$path" "Ghidra project file" ;;
        slus_*|sles_*|slpm_*|slps_*|scus_*|sces_*|scps_*) report "$path" "PS2 boot executable name" ;;
    esac
    case "/$lower" in
        */work/*|*/extracted/*|*/dump/*|*/dumps/*|*/assets_dumped/*) report "$path" "extracted-data directory" ;;
        *.rep/*) report "$path" "Ghidra project directory" ;;
    esac
    case "$lower" in
        generated/readme.md) ;;
        generated/*) report "$path" "generated code must never be committed" ;;
        analysis/out/*|analysis/*.toml|analysis/*.csv)
            report "$path" "derived analysis output (policy pending, see analysis/README.md)" ;;
    esac

    # (c) size
    size=$(git cat-file -s ":$path" 2>/dev/null || echo 0)
    if [ "$size" -gt "$MAX_BYTES" ]; then
        report "$path" "larger than 20 MB ($size bytes)"
    fi

    # (b) content magic
    if [ "$size" -ge 4 ]; then
        magic=$(git cat-file blob ":$path" | head -c 4 | od -An -tx1 | tr -d ' \n')
        [ "$magic" = "7f454c46" ] && report "$path" "ELF binary content"
    fi
    if [ "$size" -ge 28 ]; then
        hdr=$(git cat-file blob ":$path" | head -c 28 | tr -d '\000')
        [ "$hdr" = "Sony PS2 Memory Card Format " ] && report "$path" "PS2 memory card image"
    fi
    if [ "$size" -ge 32774 ]; then
        iso=$(git cat-file blob ":$path" | head -c 32774 | tail -c 5 | tr -d '\000')
        [ "$iso" = "CD001" ] && report "$path" "ISO9660 disc image content"
    fi

    # (d) no generated-code includes in modules that must stay game-agnostic
    case "$lower" in
        platform/*|render/*|runtime-ext/*)
            case "$base" in
                *.c|*.cc|*.cpp|*.cxx|*.h|*.hh|*.hpp|*.hxx|*.inl|*.ipp)
                    if git cat-file blob ":$path" | grep -Eiq \
                        '^[[:space:]]*#[[:space:]]*include[[:space:]]*[<"]([^>"]*/)?(ps2_recompiled_|generated/)'; then
                        report "$path" "includes generated code (only app/ may use it)"
                    fi ;;
            esac ;;
    esac

    # (e) temporary-diagnostic marker in code, patches, CMake and scripts
    case "$lower" in
        *.md|tools/guard/*) ;;
        *.c|*.cc|*.cpp|*.cxx|*.h|*.hh|*.hpp|*.hxx|*.inl|*.ipp|*.patch|*.diff|*.cmake|*/cmakelists.txt|cmakelists.txt|*.sh|*.py|*.ps1|*.yml|*.yaml|*.json|*.probe|series)
            if git cat-file blob ":$path" | grep -Eiqw "$MARKER"; then
                report "$path" "temporary diagnostic marker $MARKER (use a probe or a numbered patch)"
            fi ;;
    esac
done < "$tmp"

# (e) the engine build copy must not carry hand-edited diagnostics either
for src in out/build/*/_deps/ps2recomp-patched/src; do
    [ -d "$src" ] || continue
    hits=$(grep -rEilw --include='*.c' --include='*.cc' --include='*.cpp' --include='*.h' \
        --include='*.hpp' --include='*.inl' "$MARKER" "$src" 2>/dev/null | head -n 5 || true)
    if [ -n "$hits" ]; then
        for hit in $hits; do
            report "$hit" "temporary diagnostic marker $MARKER in the engine build copy (use a probe or a numbered patch)"
        done
    fi
done

if [ "$fail" -ne 0 ]; then
    echo "check-forbidden: blocked. Game data and generated code must never enter this public repo (docs/LEGAL.md)." >&2
    exit 1
fi
exit 0
