# LINKDATA.BNS — main game-data container

**Status:** outer layout and link-archive format **identified** (parser + extractor in `k2disc archive`); 7 unindexed regions **partially understood** (their index lives in the boot ELF).
**Evidence:** Kessen II NTSC-U `SLUS-20275`, `LINKDATA.BNS` = 537 673 728 bytes = 262 536 sectors. Structural facts only; no contents reproduced.

The disc has three `LINKDATA.*` files. `LINKDATA.BNS` holds the game data (models, animations, textures, scripts, tables) and is the "main container" of this document. `LINKDATA.ANS` / `LINKDATA.CNS` are streaming files — see [INDEX.md](INDEX.md).

## 1. Outer layer: a sector-addressed stream

`LINKDATA.BNS` has **no header and no in-file directory**. It is a sequence of members, each starting on a 2048-byte sector boundary and zero-padded to the next one. The game addresses members by `(start sector, sector count)` pairs that are compiled into the boot ELF (see §4).

On the retail disc the file splits into:

| What | Count | Sectors | Share |
|---|---:|---:|---:|
| Link archives (§2), back-to-back | 467 | 195 765 | 74.6 % |
| Unindexed regions (§3) | 7 | 66 771 | 25.4 % |

Every link archive's padding (from `offsets[count]` to the sector boundary) is all zeros (467/467).

### Segmentation rule used by `k2disc archive`

Starting at sector 0: if a valid link archive (§2 validation, with `limit` = bytes to end of file) starts at the current sector, it is a member of `ceil(offsets[count] / 2048)` sectors. Otherwise the sectors up to the next sector where a valid link archive starts (or end of file) form one **region** member. This rule reproduces exactly the 467 + 7 split above.

## 2. Link archive (Koei "link" pack)

All integers little-endian `u32`.

| Offset | Size | Field | Notes |
|---:|---:|---|---|
| 0x00 | 4 | `count` | number of entries, ≥ 1 (observed 1…602) |
| 0x04 | 4 × (`count` + 1) | `offsets[]` | byte offsets from the start of the archive; entry *i* = `[offsets[i], offsets[i+1])` |
| … | 0–12 | padding | zeros up to a 16-byte boundary |
| `offsets[0]` | | entry data | entries are contiguous; no per-entry header, no names |

Invariants (hold for all 467 outer and 652 nested archives on the disc):

- `offsets[0] == align16(4 × (count + 2))` — the header size rounded up to 16.
- `offsets` is non-decreasing; `offsets[count]` is the archive's total size.
- **Nested** archives (an entry that is itself a link archive) end exactly at their entry boundary: `offsets[count] == entry size`.
- Entry starts are not necessarily aligned (3 776 of 4 431 top-level entries are 16-aligned).
- There are no names, types or compression flags in the header. Entry type comes only from the entry's own magic (see INDEX.md). No compressed entries have been identified so far; if some exist, the codec is part of the entry payload, not the container.

Validation used by the parser (fails closed): `1 ≤ count ≤ 65 535`; the header fits in `limit`; `offsets[0]` equals the aligned header size; offsets are monotonic; `offsets[count] ≤ limit`. When checking a nested entry, it also requires `offsets[count] == entry size`. This strict rule rejects nearly all non-archive data, because it needs an exact header-size match plus a consistent offset chain.

## 3. Unindexed regions

These are sector ranges that are not link archives. The game reaches their members through tables in the boot ELF, not through anything in the file. Offsets below are **ELF file offsets** in `SLUS_202.75` (text/data segment: file offset `0x1000` = EE address `0x100000`).

| Start sector | Sectors | First bytes | Index found in ELF |
|---:|---:|---|---|
| `0x00E0C` | `0x420F` | `TMD20060` (model) | `(sector, sectors)` pair tables at `0x148180`: 3 zero-terminated blocks (47 / 359 / 25 entries) covering `0xE0C`…`0x49DE` |
| `0x0F442` | `0x73FD` | `FF FF 04 FF …` | hard-coded `load(sector, sectors)` calls (loader at EE `0x128B40`, e.g. `(0xF442, 0x71)`, `(0xF4B5, 0x21)`); `(sector, sectors, bytes)` table at `0x147C10` (116 records, `0xFB8B`…`0x1683F`, tiles with no gaps or overlaps) |
| `0x2EEC2` | `0x0066` | `0B 00 00 00 …` | not found yet |
| `0x31E0B` | `0x141B` | `FF FF 04 00 …` | `(sector, sectors, bytes)` runs at `0xCF1D0`…`0xCF6xx` covering `0x31E29`…`0x31EB2` (partial) |
| `0x3B4F0` | `0x2F18` | `FF FF 03 FF …` | not found yet |
| `0x3F2AF` | `0x0825` | `FCVQ0100` | not found yet |
| `0x3FE7F` | `0x0309` | `01 00 00 00 08 00 …` | not found yet |

The outer link archives are loaded the same way: for example, the loader is called with `(0x0, 0x78C)`, `(0x78C, 0x3C8)` and `(0xB54, 0x2B8)` — exactly the first three archives' sector extents.

Record layouts seen in the ELF tables:

- `(u32 sector, u32 sectors)` — 8 bytes; blocks end with `(0, 0)`.
- `(u32 sector, u32 sectors, u32 bytes)` — 12 bytes; `bytes ≤ sectors × 2048 < bytes + 2048`.

## 4. How to use it

```
k2disc archive list    work/disc/SLUS-20275/iso/LINKDATA.BNS
k2disc archive extract work/disc/SLUS-20275/iso/LINKDATA.BNS            # -> work/disc/SLUS-20275/archives/LINKDATA.BNS/
k2disc archive list    <standalone link-archive file>                  # inner entries
```

`extract` never writes a link archive as a file: every link archive it meets (up to the nesting limit) becomes a directory, and its entries are listed in `index.tsv` at the output root (e.g. `work/disc/SLUS-20275/archives/LINKDATA.BNS/index.tsv`). `archive list <file>` accepts any standalone link-archive file whose size exactly matches its offset table, for example one cut from `LINKDATA.BNS` by the sector range that `archive list` prints for the outer member.

`extract` writes each outer member as `NNNNN_sSSSSS.<ext>` (index, start sector in hex). A link archive is expanded into a directory of the same stem, recursively (nested archives become subdirectories, up to 8 levels). Leaves are named `NNNN.<ext>`, with the extension chosen from the magic. Regions are written as `NNNNN_sSSSSS.region.bin`.

## Open questions

- Complete the ELF-side index for the regions (a loader call graph from the recompiler would give authoritative names/uses).
- Entry types without magic (`0x01`, `0x04`, `0x0B`, … leading `u32`) — probably counted tables; unknown.
- Whether any entry payloads are compressed (none identified yet).
