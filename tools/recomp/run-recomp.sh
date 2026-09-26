#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Recompile the Kessen II boot ELF into C++ with PS2Recomp's ps2_recomp.
#
#   sh tools/recomp/run-recomp.sh [config.toml]
#
# Steps:
#   1. Build ps2_recomp in out/build/ps2recomp-tools if it is missing.
#   2. Check the config's [general] output is <repo>/generated or outside the repo
#      (same rule as analysis/run-analysis.sh and the root CMakeLists.txt).
#   3. Check the config's [general] input is the combined ELF
#      (SLUS_202.75.combined.elf from tools/recomp/make-combined-elf.sh).
#   4. Delete stale generated *.cpp/*.h (README.md is kept), run ps2_recomp.
#
# Environment (defaults in brackets):
#   K2_ALLOW_RETAIL_ELF  set to 1 to accept an input other than the combined ELF
#                   (e.g. the retail boot ELF, which lacks the main overlay and
#                   crashes around frame 509, see docs/BRINGUP.md #3)  [unset]
#   K2_PS2_RECOMP   ps2_recomp executable  [first existing of
#                   out/build/ps2recomp-tools/ps2xRecomp/Release/ps2_recomp(.exe) and
#                   out/build/ps2recomp-tools/ps2xRecomp/ps2_recomp(.exe)];
#                   if set, it must be executable (no build fallback)
#
# Output is git-ignored; the log goes to analysis/out/reports/ps2_recomp.log.
set -eu

top=$(git rev-parse --show-toplevel)
cd "$top"

case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) windows=1 ;;
    *) windows=0 ;;
esac

die() { echo "run-recomp: error: $*" >&2; exit 1; }
log() { echo "run-recomp: $*"; }

lower() { if [ "$windows" = 1 ]; then tr '[:upper:]' '[:lower:]'; else cat; fi; }
native_abs() {
    if [ "$windows" = 1 ]; then cygpath -m -a "$1"
    else case "$1" in /*) printf '%s\n' "$1" ;; *) printf '%s\n' "$top/$1" ;; esac; fi
}

toml=${1:-analysis/kessen2.toml}
[ -f "$toml" ] || die "config not found: $toml (run sh analysis/run-analysis.sh first)"

exe=""
[ "$windows" = 1 ] && exe=.exe
recomp_dir=out/build/ps2recomp-tools/ps2xRecomp
candidates="$recomp_dir/Release/ps2_recomp$exe $recomp_dir/ps2_recomp$exe"
K2_PS2_RECOMP=${K2_PS2_RECOMP:-}
# An override is used as-is (fail closed): never fall back to building ps2_recomp.
if [ -n "$K2_PS2_RECOMP" ] && [ ! -x "$K2_PS2_RECOMP" ]; then
    die "K2_PS2_RECOMP is not an executable: $K2_PS2_RECOMP"
fi

find_recomp() {
    if [ -n "$K2_PS2_RECOMP" ]; then
        printf '%s\n' "$K2_PS2_RECOMP"
        return 0
    fi
    for c in $candidates; do
        [ -x "$c" ] && { printf '%s\n' "$c"; return 0; }
    done
    return 0
}

reports=analysis/out/reports
mkdir -p "$reports"
recomp=$(find_recomp)
if [ -z "$recomp" ]; then
    log "building ps2_recomp (standalone PS2Recomp configure; first run only)"
    [ -f external/PS2Recomp/CMakeLists.txt ] || die "PS2Recomp submodule missing; run git submodule update --init --recursive"
    cmake -S external/PS2Recomp -B out/build/ps2recomp-tools \
        -DPS2X_BUILD_RECOMP=ON -DPS2X_BUILD_ANALYZER=ON -DPS2X_BUILD_RUNTIME=OFF \
        -DPS2X_BUILD_TEST=OFF -DPS2X_BUILD_STUDIO=OFF -DCMAKE_BUILD_TYPE=Release > "$reports/ps2_recomp-build.log" 2>&1 \
        || die "ps2_recomp configure failed, see $reports/ps2_recomp-build.log"
    cmake --build out/build/ps2recomp-tools --config Release --target ps2_recomp --parallel \
        >> "$reports/ps2_recomp-build.log" 2>&1 \
        || die "ps2_recomp build failed, see $reports/ps2_recomp-build.log"
    recomp=$(find_recomp)
    [ -n "$recomp" ] || die "ps2_recomp not produced at any of: $candidates (set K2_PS2_RECOMP)"
fi
log "ps2_recomp: $recomp"

# --- Output path check (fail closed) ---
out_line=$(awk '/^\[/ { s = $0 } s == "[general]" && /^output = "/ { print; exit }' "$toml")
[ -n "$out_line" ] || die "no [general] output in $toml"
gen=$(printf '%s\n' "$out_line" | sed 's/^output = "\(.*\)"[[:space:]]*$/\1/')
case "$gen" in
    *'"'*|*'\'*|'') die "unsupported [general] output in $toml: $out_line" ;;
esac
cmp_gen=$(native_abs "$gen" | sed 's:/*$::' | lower)
cmp_top=$(native_abs "$top" | sed 's:/*$::' | lower)
case "$cmp_gen" in
    "$cmp_top"|"$cmp_top"/*)
        [ "$cmp_gen" = "$cmp_top/generated" ] \
            || die "[general] output must be $top/generated or outside the repository. Got: $gen" ;;
esac
mkdir -p "$gen"

# --- Input ELF check (fail closed) ---
in_line=$(awk '/^\[/ { s = $0 } s == "[general]" && /^input = "/ { print; exit }' "$toml")
[ -n "$in_line" ] || die "no [general] input in $toml"
elf_in=$(printf '%s\n' "$in_line" | sed 's/^input = "\(.*\)"[[:space:]]*$/\1/')
case "$elf_in" in
    *'"'*|*'\'*|'') die "unsupported [general] input in $toml: $in_line" ;;
esac
if [ "${elf_in##*/}" != SLUS_202.75.combined.elf ]; then
    [ "${K2_ALLOW_RETAIL_ELF:-}" = 1 ] \
        || die "[general] input in $toml is $elf_in, not SLUS_202.75.combined.elf: the main code overlay would be missing. Run sh tools/recomp/make-combined-elf.sh, then sh analysis/run-analysis.sh (or set K2_ALLOW_RETAIL_ELF=1 to override)"
    log "warning: K2_ALLOW_RETAIL_ELF=1, recompiling $elf_in without the main overlay"
fi
log "input: $elf_in"

# --- Stale output cleanup (only files ps2_recomp writes; README.md stays) ---
stale=$(find "$gen" -maxdepth 1 -type f \( -name '*.cpp' -o -name '*.h' \) | wc -l | tr -d ' ')
find "$gen" -maxdepth 1 -type f \( -name '*.cpp' -o -name '*.h' \) -exec rm -f {} +
log "removed $stale stale file(s) from $gen"

start=$(date +%s)
log "running ps2_recomp $toml (log: $reports/ps2_recomp.log)"
if ! "$recomp" "$toml" > "$reports/ps2_recomp.log" 2>&1; then
    tail -n 30 "$reports/ps2_recomp.log" >&2
    die "ps2_recomp failed after $(( $(date +%s) - start )) s, see $reports/ps2_recomp.log"
fi
cpp=$(find "$gen" -maxdepth 1 -type f -name '*.cpp' | wc -l | tr -d ' ')
hdr=$(find "$gen" -maxdepth 1 -type f -name '*.h' | wc -l | tr -d ' ')
[ -f "$gen/register_functions.cpp" ] || die "register_functions.cpp missing in $gen"
log "wrote $cpp .cpp + $hdr .h into $gen in $(( $(date +%s) - start )) s"
