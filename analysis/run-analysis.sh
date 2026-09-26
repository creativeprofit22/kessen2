#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Reproducible static analysis of the Kessen II boot ELF (no GUI steps).
#
#   sh analysis/run-analysis.sh
#
# Stages:
#   1. ps2_analyzer (PS2Recomp, built on demand) -> SCE SDK signature matches
#      -> analysis/out/sdk-names.csv
#   2. analyzeHeadless: import as r5900:LE:32:default, pin analyzer options,
#      auto-analyze, apply SDK names, write reports, run PS2Recomp's
#      ExportPS2Functions.java -> analysis/kessen2.toml + analysis/out/kessen2_functions.csv
#   3. Point the TOML's [general] input/output at the ELF and K2_GENERATED_DIR.
#
# Environment (defaults in brackets):
#   GHIDRA_INSTALL_DIR  Ghidra 12.1.3 with ghidra-emotionengine-reloaded v2.1.37 in
#                       Ghidra/Extensions  [E:/Tools/ghidra_12.1.3_PUBLIC]
#   K2_JAVA_HOME        JDK 21             [E:/Tools/jdk-21.0.12.1+1]
#   K2_ELF              boot ELF, relative to the repo root or absolute
#                       [work/disc/SLUS-20275/iso/SLUS_202.75]
#   K2_PS2_ANALYZER     ps2_analyzer executable  [first existing of
#                       out/build/ps2recomp-tools/ps2xAnalyzer/Release/ps2_analyzer(.exe)
#                       (multi-config generators) and
#                       out/build/ps2recomp-tools/ps2xAnalyzer/ps2_analyzer(.exe)
#                       (single-config generators)]
#   K2_GENERATED_DIR    ps2_recomp output written into the TOML; must match CMake's
#                       KESSEN2_GENERATED_DIR. Either <repo>/generated or a path
#                       outside the repo (other in-tree paths are rejected)  [generated]
#
# Everything written here is git-ignored (see docs/modules/analysis.md).
set -eu

top=$(git rev-parse --show-toplevel)
cd "$top"

case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) windows=1 ;;
    *) windows=0 ;;
esac

# Native absolute path (forward slashes on Windows) for tools that are not MSYS-aware.
native() {
    if [ "$windows" = 1 ]; then cygpath -m "$1"; else printf '%s\n' "$1"; fi
}

die() { echo "run-analysis: error: $*" >&2; exit 1; }

# Absolute native path with '.'/'..' segments and trailing slashes removed
# (mirrors cmake_path(ABSOLUTE_PATH ... NORMALIZE) in the root CMakeLists.txt).
normalize_path() {
    if [ "$windows" = 1 ]; then p=$(cygpath -m -a "$1") # cwd is $top
    else case "$1" in /*) p=$1 ;; *) p="$top/$1" ;; esac; fi
    printf '%s\n' "$p" | awk '{
        n = split($0, seg, "/"); out = ""; depth = 0
        lead = (substr($0, 1, 1) == "/") ? "/" : ""
        for (i = 1; i <= n; i++) {
            s = seg[i]
            if (s == "" || s == ".") continue
            if (s == "..") { if (depth > 1 || (depth == 1 && lead == "/")) depth--; continue }
            stack[++depth] = s
        }
        for (i = 1; i <= depth; i++) out = out (i > 1 ? "/" : "") stack[i]
        print lead out
    }'
}
log() { echo "run-analysis: $*"; }

start_all=$(date +%s)
stage_start=$start_all
stage_done() {
    now=$(date +%s)
    log "$1 done in $((now - stage_start)) s"
    stage_start=$now
}

GHIDRA_INSTALL_DIR=${GHIDRA_INSTALL_DIR:-E:/Tools/ghidra_12.1.3_PUBLIC}
K2_JAVA_HOME=${K2_JAVA_HOME:-E:/Tools/jdk-21.0.12.1+1}
K2_ELF=${K2_ELF:-work/disc/SLUS-20275/iso/SLUS_202.75}
exe=""
[ "$windows" = 1 ] && exe=.exe
K2_PS2_ANALYZER=${K2_PS2_ANALYZER:-}
# Default ps2_analyzer locations: Release/ subdir on multi-config generators
# (Visual Studio), directly in ps2xAnalyzer/ on single-config ones (Makefiles/Ninja).
analyzer_dir=out/build/ps2recomp-tools/ps2xAnalyzer
analyzer_candidates="$analyzer_dir/Release/ps2_analyzer$exe $analyzer_dir/ps2_analyzer$exe"
K2_GENERATED_DIR=${K2_GENERATED_DIR:-$top/generated}

# Same rule as the root CMakeLists.txt: only <repo>/generated is git-ignored and
# guarded, so refuse any other in-tree location.
n_gen=$(normalize_path "$K2_GENERATED_DIR")
# The path is written into a TOML string and passed through awk -v.
case "$n_gen" in
    *'"'*|*'\'*) die "K2_GENERATED_DIR must not contain '\"' or '\\': $K2_GENERATED_DIR" ;;
esac
n_top=$(normalize_path "$top")
cmp_gen=$n_gen
cmp_top=$n_top
if [ "$windows" = 1 ]; then # case-insensitive filesystem
    cmp_gen=$(printf '%s\n' "$cmp_gen" | tr '[:upper:]' '[:lower:]')
    cmp_top=$(printf '%s\n' "$cmp_top" | tr '[:upper:]' '[:lower:]')
fi
case "$cmp_gen" in
    "$cmp_top"|"$cmp_top"/*)
        [ "$cmp_gen" = "$cmp_top/generated" ] \
            || die "K2_GENERATED_DIR must be $n_top/generated or outside the repository (other in-tree paths are not git-ignored). Got: $K2_GENERATED_DIR" ;;
esac

# --- Preconditions (fail closed) ---
[ -f "$K2_ELF" ] || die "boot ELF not found: $K2_ELF (extract the disc first, see docs/modules/disc.md)"
# Resolve once (relative values from the repo root; cwd is $top). Native form,
# used for Ghidra's -import and the TOML's [general] input.
n_elf=$(normalize_path "$K2_ELF")
case "$n_elf" in
    *'"'*|*'\'*) die "K2_ELF must not contain '\"' or '\\': $K2_ELF" ;;
esac
[ -d "$GHIDRA_INSTALL_DIR/Ghidra" ] || die "Ghidra not found at GHIDRA_INSTALL_DIR=$GHIDRA_INSTALL_DIR"
grep -q '^application.version=12.1.3$' "$GHIDRA_INSTALL_DIR/Ghidra/application.properties" \
    || die "Ghidra at $GHIDRA_INSTALL_DIR is not 12.1.3"
ee_props="$GHIDRA_INSTALL_DIR/Ghidra/Extensions/ghidra-emotionengine-reloaded/extension.properties"
[ -f "$ee_props" ] || die "ghidra-emotionengine-reloaded not installed in $GHIDRA_INSTALL_DIR/Ghidra/Extensions"
[ -x "$K2_JAVA_HOME/bin/java$exe" ] || die "JDK not found at K2_JAVA_HOME=$K2_JAVA_HOME"
"$K2_JAVA_HOME/bin/java$exe" -version 2>&1 | head -n 1 | grep -q '"21\.' \
    || die "K2_JAVA_HOME must be a JDK 21 (Ghidra 12.1.3 requires 21)"
if [ "$windows" = 1 ]; then headless="$GHIDRA_INSTALL_DIR/support/analyzeHeadless.bat"
else headless="$GHIDRA_INSTALL_DIR/support/analyzeHeadless"; fi
[ -f "$headless" ] || die "analyzeHeadless not found: $headless"
export_script=external/PS2Recomp/ps2xRecomp/tools/ghidra/ExportPS2Functions.java
[ -f "$export_script" ] || die "PS2Recomp submodule missing ($export_script); run git submodule update --init"

out=analysis/out
reports=$out/reports
toml=analysis/kessen2.toml
csv=$out/kessen2_functions.csv
rm -rf "$out" "$toml"
mkdir -p "$reports" "$out/ghidra" "$out/scripts"

log "ELF: $K2_ELF ($(wc -c < "$K2_ELF" | tr -d ' ') bytes)"

# --- Stage 1: SCE SDK signature scan ---
# Prints the ps2_analyzer to run, or nothing: K2_PS2_ANALYZER if set, else the
# first existing default candidate.
find_analyzer() {
    if [ -n "$K2_PS2_ANALYZER" ]; then
        [ -x "$K2_PS2_ANALYZER" ] && printf '%s\n' "$K2_PS2_ANALYZER"
        return 0
    fi
    for c in $analyzer_candidates; do
        [ -x "$c" ] && { printf '%s\n' "$c"; return 0; }
    done
    return 0
}
analyzer=$(find_analyzer)
if [ -z "$analyzer" ]; then
    log "building ps2_analyzer (standalone PS2Recomp configure; first run only)"
    cmake -S external/PS2Recomp -B out/build/ps2recomp-tools \
        -DPS2X_BUILD_RECOMP=ON -DPS2X_BUILD_ANALYZER=ON -DPS2X_BUILD_RUNTIME=OFF \
        -DPS2X_BUILD_TEST=OFF -DPS2X_BUILD_STUDIO=OFF -DCMAKE_BUILD_TYPE=Release > "$reports/ps2_analyzer-build.log" 2>&1 \
        || die "ps2_analyzer configure failed, see $reports/ps2_analyzer-build.log"
    cmake --build out/build/ps2recomp-tools --config Release --target ps2_analyzer --parallel \
        >> "$reports/ps2_analyzer-build.log" 2>&1 \
        || die "ps2_analyzer build failed, see $reports/ps2_analyzer-build.log"
    analyzer=$(find_analyzer)
    if [ -z "$analyzer" ]; then
        if [ -n "$K2_PS2_ANALYZER" ]; then
            die "ps2_analyzer not produced at $K2_PS2_ANALYZER (set K2_PS2_ANALYZER)"
        fi
        die "ps2_analyzer not produced at any of: $analyzer_candidates (set K2_PS2_ANALYZER)"
    fi
fi
log "ps2_analyzer: $analyzer"
"$analyzer" "$K2_ELF" "$out/ps2_analyzer.toml" > "$reports/ps2_analyzer.log" 2>&1 \
    || die "ps2_analyzer failed, see $reports/ps2_analyzer.log"
grep 'SCE SDK symbol match' "$reports/ps2_analyzer.log" | sed 's/^/run-analysis:   /' || true
# Every "name@0xADDR" selector the analyzer emitted is an SCE database match
# (stubs + entry_points); unnamed functions never get a selector.
{
    echo "name,address"
    grep -o '"[A-Za-z_][A-Za-z0-9_]*@0x[0-9A-Fa-f]*"' "$out/ps2_analyzer.toml" \
        | tr -d '"' | awk -F'@' '$1 !~ /^(sub|FUN)_/ { print $1 "," $2 }' | sort -t, -k2,2 -u
} > "$out/sdk-names.csv"
sdk_count=$(($(wc -l < "$out/sdk-names.csv") - 1))
[ "$sdk_count" -gt 0 ] || die "no SDK names extracted from $out/ps2_analyzer.toml"
log "SDK names: $sdk_count"
stage_done "stage 1 (ps2_analyzer)"

# --- Stage 2: Ghidra headless ---
# One script directory: a ';'-separated -scriptPath is split by cmd.exe on Windows.
cp analysis/ghidra/*.java "$export_script" "$out/scripts/"

# Process-local only: Ghidra's launcher prefers `java` on PATH.
JAVA_HOME=$(native "$K2_JAVA_HOME")
PATH="$K2_JAVA_HOME/bin:$PATH"
export JAVA_HOME PATH
# Paths below are already native; stop MSYS from rewriting arguments.
MSYS_NO_PATHCONV=1
MSYS2_ARG_CONV_EXCL='*'
export MSYS_NO_PATHCONV MSYS2_ARG_CONV_EXCL

n_out=$(native "$top/$out")
n_reports=$(native "$top/$reports")
"$headless" "$n_out/ghidra" kessen2 \
    -import "$n_elf" -overwrite \
    -processor r5900:LE:32:default -cspec default \
    -scriptPath "$n_out/scripts" \
    -preScript K2SetAnalysisOptions.java "$n_reports/analyzer-options.txt" \
    -postScript K2ApplySdkNames.java "$n_out/sdk-names.csv" "$n_reports/sdk-naming.txt" \
    -postScript K2Report.java "$n_reports" "$n_out/sdk-names.csv" \
    -postScript ExportPS2Functions.java "$(native "$top/$toml")" "$n_out/kessen2_functions.csv" \
    -log "$n_reports/ghidra.log" -scriptlog "$n_reports/scripts.log" \
    > "$reports/analyzeHeadless.out" 2>&1 \
    || die "analyzeHeadless failed, see $reports/analyzeHeadless.out"
# analyzeHeadless can exit 0 after a script error; check the script log too.
if grep -Eq 'ERROR|Exception' "$reports/scripts.log"; then
    grep -E 'ERROR|Exception' "$reports/scripts.log" | head -n 20 >&2
    die "a Ghidra script reported an error, see $reports/scripts.log"
fi
grep -E '^INFO +(K2|ExportPS2)|K2SetAnalysisOptions:|K2ApplySdkNames:|K2Report:' "$reports/scripts.log" \
    | sed 's/^/run-analysis:   /' || true
stage_done "stage 2 (analyzeHeadless)"

# --- Stage 3: point the config at this checkout ---
for f in "$toml" "$csv" "$reports/analyzer-options.txt" "$reports/sdk-naming.txt" \
         "$reports/summary.txt" "$reports/sdk-functions.csv" "$reports/vu0-sites.csv" \
         "$reports/vu0-functions.csv"; do
    [ -s "$f" ] || die "expected output missing or empty: $f"
done
tmp_toml="$toml.tmp"
awk -v elf="$n_elf" -v gen="$n_gen" '
    /^\[/ { section = $0 }
    section == "[general]" && /^input = / { print "input = \"" elf "\""; next }
    section == "[general]" && /^output = / { print "output = \"" gen "\""; next }
    { print }
' "$toml" > "$tmp_toml"
mv "$tmp_toml" "$toml"
grep -q "^output = \"$n_gen\"" "$toml" || die "failed to rewrite output path in $toml"
stage_done "stage 3 (config fix-up)"

log "outputs: $toml, $csv, $reports/"
grep -E '^(functions_|sdk_named|mdebug|symtab|vu0_)' "$reports/summary.txt" | sed 's/^/run-analysis:   /'
log "total $(( $(date +%s) - start_all )) s"
