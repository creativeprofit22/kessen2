# Disc tooling — `k2disc`

`k2disc` reads **your own** Kessen II disc image. It extracts the files, identifies the boot ELF and IOP modules, catalogues file types, and unpacks the main data container. Design decisions: [ADR-0005](../adr/0005-disc-tooling-in-cpp.md). Format findings: [docs/formats/INDEX.md](../formats/INDEX.md).

## Build

`k2disc` is built with the rest of the repo:

```
cmake --preset msvc-x64
cmake --build --preset msvc-x64-release --parallel --target k2disc
# binary: out/build/msvc-x64/tools/disc/Release/k2disc.exe
```

## Commands

| Command | What it does |
|---|---|
| `k2disc info <iso>` | Volume ID, UDF presence, file count, `SYSTEM.CNF` boot ELF path, serial, `VER`/`VMODE`, boot ELF LBA/size and **CRC32** (whole file — the value PS2Recomp's `PS2_REGISTER_GAME_OVERRIDE` matches on). |
| `k2disc extract <iso> [--out DIR]` | Extracts the full ISO9660 tree and writes `manifest.tsv`. |
| `k2disc irx <iso\|dir>` | Lists every IOP module: loose `*.IRX` files and members of IOPRP/ROMDIR images (`RESET` magic), with `.iopmod` name and version. |
| `k2disc scan <iso\|dir> [--markdown]` | Catalogue by extension + magic: count, total bytes, three sample paths. `--markdown` prints a table. For a directory, k2disc's own `index.tsv`/`manifest.tsv` at its root are skipped (`irx` does the same). |
| `k2disc archive list <container>` | For `LINKDATA.BNS`: every outer member (sector, sectors, bytes, kind, entry count) and a total that must equal the file size. For a standalone link-archive file (not produced by `extract`, which expands archives into directories): its entries. |
| `k2disc archive extract <container> [--out DIR]` | Unpacks link archives recursively, writes regions raw, and writes a sorted `index.tsv`. |

Exit codes: `0` ok · `1` input/format error (the message names the offset and reason) · `2` usage (including a flag the command does not accept, e.g. `--out` on `irx`).

Typical session:

```
k2disc info    "E:/Descargas/Kessen II/Kessen II.iso"
k2disc extract "E:/Descargas/Kessen II/Kessen II.iso"
k2disc irx     work/disc/SLUS-20275/iso
k2disc scan    work/disc/SLUS-20275/iso --markdown
k2disc archive list    work/disc/SLUS-20275/iso/LINKDATA.BNS
k2disc archive extract work/disc/SLUS-20275/iso/LINKDATA.BNS
k2disc scan    work/disc/SLUS-20275/archives/LINKDATA.BNS
```

## Output layout

Everything goes under the git-ignored `work/` tree. The default root is `<repo>/work/disc`, compiled in as an absolute path, so it does not depend on the current directory.

Path arguments may contain any Unicode characters. On Windows, k2disc reads the UTF-16 command line rather than the ANSI-code-page `argv`. Paths in its output and error messages are printed as UTF-8.

```
work/disc/<SERIAL>/                       SERIAL from SYSTEM.CNF, e.g. SLUS-20275 (else the volume ID)
  iso/…                                   raw ISO9660 tree (';1' version suffixes stripped)
  manifest.tsv                            path  lba  size  crc32   (sorted by path, no timestamps)
  archives/<CONTAINER>/                   default for `archive extract` when the input is under <SERIAL>/iso/
    NNNNN_sSSSSS/                         outer link archive: member index, start sector (hex)
      NNNN.<ext>                          entry; extension from magic (tmd, fcvq, tm2, tod, txt, bin, …)
      NNNN/                               nested link archive (recursion limit 8)
    NNNNN_sSSSSS.region.bin               ELF-indexed region, written raw
    index.tsv                             path  size  kind   (sorted)
```

With `--out DIR`, output goes to `DIR/<SERIAL>/…` (`extract`) or directly to `DIR` (`archive extract`).

## Safety rules

- The disc image and containers are **untrusted input**. Every offset, size and table is bounds-checked against the real file size before it is read. Malformed input fails with exit code 1 and the offending offset; nothing crashes or reads out of range.
- Names from the image are sanitised before touching the filesystem. The tool rejects `.`/`..`, separators, drive colons, control characters, trailing dots/spaces and Windows device names (`CON`, `COM1`, …). The final path must stay lexically inside the output root. Directory loops, duplicate entries and over-deep trees are rejected.
- Limits: 32 directory levels, 16 MiB per directory, 64 MiB per IOP image, 512 MiB per container member, 65 535 entries per link archive, 8 nesting levels.
- Output is deterministic: sorted manifests and indexes, no timestamps.
- **Never commit anything under `work/`.** It is git-ignored, and `tools/guard/check-forbidden.sh` rejects ELF/ISO content and `work/` paths in commits.

## Tests

`ctest --preset msvc-x64 -R k2disc`. That covers CRC32, ISO9660, `SYSTEM.CNF`, disc inspect/extract, IRX/ROMDIR, the catalogue and the LINKDATA container. Every fixture (ISO images, IRX ELFs, link archives, sector streams) is built in memory by the test code. No game data is used or committed.
