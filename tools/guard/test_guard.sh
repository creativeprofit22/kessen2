#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Self-test for check-forbidden.sh: in a throwaway git repo, stage synthetic forbidden
# files one at a time and assert the check rejects each; then assert clean files pass.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
check="$here/check-forbidden.sh"
repo=$(mktemp -d "${TMPDIR:-/tmp}/k2guardtest.XXXXXX")
trap 'rm -rf "$repo"' EXIT HUP INT TERM

cd "$repo"
git init -q .
git config core.autocrlf false   # throwaway repo only: keep test output quiet
failures=0

reset_repo() {
    git read-tree --empty
    find . -mindepth 1 -maxdepth 1 ! -name .git -exec rm -rf {} +
}

# expect <pass|fail> <description> ; files must already be created and staged
expect() {
    if sh "$check" --staged >/dev/null 2>"$repo/.git/k2err"; then got=pass; else got=fail; fi
    if [ "$got" = "$1" ]; then
        echo "ok   - $2"
    else
        echo "FAIL - $2 (expected $1, got $got)"; cat "$repo/.git/k2err"
        failures=$((failures + 1))
    fi
    reset_repo
}

stage() { git add -f -- "$@"; }

printf 'not really a disc\n' > game.iso;                  stage game.iso;    expect fail "ISO extension"
printf 'x\n' > Kessen.BIN;                                stage Kessen.BIN;  expect fail "BIN extension (upper case)"
printf 'x\n' > SLPM_650.42;                               stage SLPM_650.42; expect fail "PS2 boot executable name"
printf '\177ELF\001\001\001\000' > boot.dat;              stage boot.dat;    expect fail "ELF magic in innocuous name"
printf 'x\n' > module.irx;                                stage module.irx;  expect fail "IRX extension"
head -c 32769 /dev/zero > disc.dat; printf 'CD001' >> disc.dat; head -c 64 /dev/zero >> disc.dat
                                                          stage disc.dat;    expect fail "ISO9660 magic at 0x8001"
printf 'Sony PS2 Memory Card Format 1.2.0.0' > card.dat;  stage card.dat;    expect fail "PS2 memory card header"
mkdir -p generated; printf 'int f();\n' > generated/x.cpp; stage generated/x.cpp; expect fail "generated/ source"
mkdir -p work; printf 'x\n' > work/notes.txt;             stage work/notes.txt; expect fail "work/ directory"
head -c 20971521 /dev/zero > big.dat;                     stage big.dat;     expect fail "file larger than 20 MB"
mkdir -p platform/src
printf '#include "ps2_recompiled_functions.h"\n' > platform/src/a.cpp
                                                          stage platform/src/a.cpp; expect fail "generated include in platform/"
mkdir -p render/src
printf '#  include <../generated/foo.h>\n' > render/src/b.cpp
                                                          stage render/src/b.cpp; expect fail "generated include in render/"
mkdir -p runtime-ext/src
printf '#include "ps2_recompiled_functions.h"\n' > runtime-ext/src/c.cpp
                                                          stage runtime-ext/src/c.cpp; expect fail "generated include in runtime-ext/"
printf 'x\n' > bios.rom1;                                 stage bios.rom1;   expect fail "BIOS side file"
mkdir -p analysis; printf 'x = 1\n' > analysis/kessen2.toml; stage analysis/kessen2.toml; expect fail "analysis/*.toml derived output"
mkdir -p analysis/out/reports; printf 'a,b\n' > analysis/out/reports/x.csv
                                                          stage analysis/out/reports/x.csv; expect fail "analysis/out/ derived output"

mkdir -p generated platform/src
printf '# generated\n' > generated/README.md
printf '# hello\n' > README.md
printf '#include "k2/platform.h"\n' > platform/src/ok.cpp
stage generated/README.md README.md platform/src/ok.cpp;                     expect pass "clean files"

mkdir -p analysis/ghidra
printf '# analysis\n' > analysis/README.md
printf '#!/bin/sh\n' > analysis/run-analysis.sh
printf 'class K2Report {}\n' > analysis/ghidra/K2Report.java
stage analysis/README.md analysis/run-analysis.sh analysis/ghidra/K2Report.java; expect pass "analysis scripts and docs"

if [ "$failures" -ne 0 ]; then
    echo "test_guard: $failures case(s) failed" >&2
    exit 1
fi
echo "test_guard: all cases passed"
