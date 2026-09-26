#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Builds the ELF that analysis and ps2_recomp see: the retail boot ELF with the fixed
# "main" overlay (loaded by the game from LINKDATA.BNS into 0x5A4800 at boot) written into
# its empty program segment, so the overlay is analysed and recompiled with the rest.
#
#   sh tools/recomp/make-combined-elf.sh
#   sh analysis/run-analysis.sh    # uses the combined ELF by default
#
# The retail ELF lists its overlays as NOLOAD segments (1 at 0x5A4800, 2 at 0x7F1800,
# 111 at 0x860000). Only the 0x5A4800 one is loaded once and never swapped; the others
# share addresses and need per-overlay dispatch in PS2Recomp (docs/BRINGUP.md #3).
#
# The runtime still loads the unmodified retail ELF; the game copies the overlay bytes in
# itself. Output is game data: it goes to work/ (git-ignored) and must never be committed.
#
# Environment (defaults in brackets):
#   K2_ISO_DIR   extracted disc  [work/disc/SLUS-20275/iso]
#   K2_OUT       output ELF      [work/recomp/SLUS_202.75.combined.elf]
set -eu

top=$(git rev-parse --show-toplevel)
cd "$top"

die() { echo "make-combined-elf: error: $*" >&2; exit 1; }
log() { echo "make-combined-elf: $*"; }

K2_ISO_DIR=${K2_ISO_DIR:-work/disc/SLUS-20275/iso}
K2_OUT=${K2_OUT:-work/recomp/SLUS_202.75.combined.elf}
elf=$K2_ISO_DIR/SLUS_202.75
bns=$K2_ISO_DIR/LINKDATA.BNS

# Observed at boot (K2_TRACE_CD=1): sceCdRead lsn=LINKDATA+0xFB8B sectors=0x1A3 -> 0x5A4800.
ov_vaddr=$((0x5A4800))
ov_offset=$((0xFB8B * 0x800))
ov_size=$((0x1A3 * 0x800))

[ -f "$elf" ] || die "boot ELF not found: $elf (extract the disc first, see docs/modules/disc.md)"
[ -f "$bns" ] || die "LINKDATA.BNS not found: $bns"

# Little-endian u32/u16 readers and a u32 writer (host is little-endian x86/x64).
u32() { od -A n -t u4 -j "$2" -N 4 "$1" | tr -d ' '; }
u16() { od -A n -t u2 -j "$2" -N 2 "$1" | tr -d ' '; }
put_u32() { # file offset value
    printf "$(printf '\\%03o\\%03o\\%03o\\%03o' $(($3 & 255)) $(($3 >> 8 & 255)) \
        $(($3 >> 16 & 255)) $(($3 >> 24 & 255)))" \
        | dd of="$1" bs=1 seek="$2" conv=notrunc 2>/dev/null
}

[ "$(od -A n -t x1 -N 4 "$elf" | tr -d ' ')" = "7f454c46" ] || die "$elf is not an ELF"

# Overlay header check (fail closed if the disc image differs from the one analysed).
hdr_magic=$(u32 "$bns" "$ov_offset")
hdr_vaddr=$(u32 "$bns" $((ov_offset + 8)))
[ "$hdr_magic" = $((0x336F574D)) ] || die "no MWo3 overlay header at LINKDATA.BNS+$ov_offset"
[ "$hdr_vaddr" = "$ov_vaddr" ] || die "overlay load address $hdr_vaddr != $ov_vaddr"

mkdir -p "$(dirname "$K2_OUT")"
cp "$elf" "$K2_OUT"

# Append overlay bytes at the next 0x800 boundary.
size=$(wc -c < "$K2_OUT" | tr -d ' ')
data_off=$(( (size + 0x7FF) / 0x800 * 0x800 ))
dd if=/dev/zero bs=1 count=$((data_off - size)) 2>/dev/null >> "$K2_OUT"
dd if="$bns" bs=2048 skip=$((ov_offset / 2048)) count=$((ov_size / 2048)) 2>/dev/null >> "$K2_OUT"
[ "$(wc -c < "$K2_OUT" | tr -d ' ')" = $((data_off + ov_size)) ] || die "short overlay read"

# Point the overlay's program header and section header at the appended bytes.
phoff=$(u32 "$elf" 28); shoff=$(u32 "$elf" 32)
phent=$(u16 "$elf" 42); phnum=$(u16 "$elf" 44)
shent=$(u16 "$elf" 46); shnum=$(u16 "$elf" 48)
ph_done=0; sh_done=0
i=0
while [ "$i" -lt "$phnum" ]; do
    ph=$((phoff + i * phent))
    if [ "$(u32 "$elf" $((ph + 8)))" = "$ov_vaddr" ]; then
        memsz=$(u32 "$elf" $((ph + 20)))
        [ "$memsz" -ge "$ov_size" ] || die "overlay segment too small ($memsz < $ov_size)"
        put_u32 "$K2_OUT" $((ph + 4)) "$data_off"
        put_u32 "$K2_OUT" $((ph + 16)) "$ov_size"
        ph_done=$((ph_done + 1))
    fi
    i=$((i + 1))
done
i=0
while [ "$i" -lt "$shnum" ]; do
    sh=$((shoff + i * shent))
    if [ "$(u32 "$elf" $((sh + 12)))" = "$ov_vaddr" ]; then
        put_u32 "$K2_OUT" $((sh + 16)) "$data_off"
        put_u32 "$K2_OUT" $((sh + 20)) "$ov_size"
        sh_done=$((sh_done + 1))
    fi
    i=$((i + 1))
done
[ "$ph_done" = 1 ] || die "expected 1 program header at $ov_vaddr, found $ph_done"
[ "$sh_done" = 1 ] || die "expected 1 section header at $ov_vaddr, found $sh_done"

log "wrote $K2_OUT (overlay 0x$(printf %x "$ov_vaddr") +0x$(printf %x "$ov_size") bytes at file offset 0x$(printf %x "$data_off"))"
