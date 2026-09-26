# Disc format index — Kessen II (SLUS-20275)

What is on the retail disc and how well we understand each file type. Produced with `k2disc` (see [docs/modules/disc.md](../modules/disc.md)); re-run it on your own disc to reproduce. Only structural facts are recorded here — never file contents.

Status legend: **identified** (format known, tooling can read it) · **partially understood** (structure known, payload not fully decoded) · **unknown**.

## Disc identity

| Field | Value |
|---|---|
| Serial | `SLUS-20275` (NTSC-U) |
| Boot ELF (`SYSTEM.CNF` `BOOT2`) | `cdrom0:\SLUS_202.75;1` |
| Boot ELF size | 1 371 008 bytes |
| Boot ELF CRC32 (whole file, PS2Recomp `computeFileCrc32`) | `0xBAFBDE3F` — cross-checked with Python `zlib.crc32` |
| `SYSTEM.CNF` | `VER = 1.00`, `VMODE = NTSC` |
| Filesystem | ISO9660 + UDF bridge (`NSR02`), 2 135 264 × 2048-byte sectors; ISO9660 volume ID empty |
| Files on disc | 14 files in 2 directories (root, `MODULES/`), 4 311 559 318 bytes total |

## Files on disc

`k2disc scan <iso> --markdown` (magic = first bytes; see `tools/disc/src/catalogue.cpp`).

| Extension | Kind (magic) | Count | Total bytes | Status | Notes |
|---|---|---:|---:|---|---|
| `75` (`SLUS_202.75`) | `elf` | 1 | 1 371 008 | identified | EE boot ELF (MIPS R5900) — input to PS2Recomp |
| `CNF` | `system-cnf` | 1 | 57 | identified | `SYSTEM.CNF` boot descriptor |
| `IRX` | `elf-irx` | 8 | 275 748 | identified | IOP modules, see below |
| `IMG` | `iop-romdir` | 1 | 230 329 | identified | `IOPRP23.IMG`: ROMDIR image of 15 IOP replacement modules |
| `ANS` | `sony-sshd` | 1 | 1 254 176 768 | partially understood | `LINKDATA.ANS`: sound bank + MPEG-2 PS streams (below) |
| `BNS` | `unknown:42000000` | 1 | 537 673 728 | identified (container) | `LINKDATA.BNS`: **main container** — [linkdata.md](linkdata.md) |
| `CNS` | `unknown:00000000` | 1 | 2 517 831 680 | partially understood | `LINKDATA.CNS`: headerless data + MPEG-2 PS streams (below) |
| **total** | | 14 | 4 311 559 318 | | |

The three `LINKDATA.*` files hold 99.99 % of the disc. The `kind` column is what `scan` reports from the first bytes; the container files have no magic of their own.

## Containers

| File | Structure | Status |
|---|---|---|
| `LINKDATA.BNS` | Headerless 2048-byte-sector stream: 467 Koei **link archives** (count + offset table, nestable) + 7 regions indexed only from the boot ELF. Full spec: [linkdata.md](linkdata.md). `k2disc archive list/extract` handles it. | **identified** (link archives) / regions **partially understood** |
| `LINKDATA.ANS` | Sectors `0x0`–`0x1F`: Sony SShd sound-bank header (`IECSsreV` / `IECSdaeH`, header 0x870 bytes, body 0x13780 bytes) followed by its sample body. From sector `0x20`: MPEG-2 program stream packs (`00 00 01 BA`) every 8 sectors (16 KiB packs), video `0xE0` + private-1 `0xBD` (audio) + system `0xBB` / padding `0xBE`. The non-pack sectors in between are also stream payload. There is no in-file index; the boot ELF has only the file name `LINKDATA.ANS;1`. | **partially understood** — stream boundaries/index not located |
| `LINKDATA.CNS` | Sectors `0x0`–`0x619C`: headerless data (no magic; leading u32 tables). From sector `0x619D`: MPEG-2 program stream packs, same 16 KiB layout as ANS. No in-file index; the boot ELF references `LINKDATA.CNS;1`. | **partially understood** — stream boundaries/index not located |

## Types inside `LINKDATA.BNS`

`k2disc archive extract` + `k2disc scan` over the extracted tree: 15 599 leaf files, 537 069 313 bytes. That is everything except link-archive headers and sector padding.

| Kind (magic) | Count | Total bytes | Status | Notes |
|---|---:|---:|---|---|
| `koei-tmd2` — `TMD20060` | 3 955 | 132 142 144 | partially understood | Koei TMD2 3-D model; header has counts + 0x40-stride tables. Layout not decoded |
| `koei-fcvq` — `FCVQ0100` | 9 268 | 125 076 944 | partially understood | Koei vector-quantised image/texture/video frames. Codec not decoded |
| `tim2` — `TIM2` | 162 | 17 933 184 | identified | Standard PS2 TIM2 textures |
| `koei-tod2` — `TOD2` | 518 | 7 651 200 | partially understood | Koei TOD2 animation (by name/magic; layout not decoded) |
| `text` | 111 | 1 474 348 | identified | Plain ASCII text files (purpose per file not yet mapped) |
| `koei-mop0` — `MOP0` | 12 | 87 232 | unknown | Magic only |
| unknown, `FF FF 0x FF`-headed | — | ≈ 164 500 000 | unknown | Mostly the ELF-indexed regions `0xF442`, `0x31E0B`, `0x3B4F0` and archive entries starting `FF FF 00 FF`. Consistent 8-byte header, not decoded |
| unknown, small-integer-headed (`01`/`05`/`08`/`0B`/… `00 00 00`) | — | ≈ 88 000 000 | unknown | Count-prefixed tables (327 distinct leading words in total across unknowns) |
| **all unknown** | 1 573 | 252 704 261 | unknown | 47 % of container bytes |

According to `scan`, no member starts with a known compression header. Whether TMD2/FCVQ payloads are internally compressed is an open question.

## IOP modules

`k2disc irx` output. Loose `.IRX` files are loaded by the game; the IOPRP23 members replace the console's ROM modules after an IOP reset. The **PS2Recomp** column is the pinned submodule `75d729c` (`ps2xIOP/src/iop_module_manager.cpp` built-in list; dedicated HLE services exist only for `libsd`, `mcserv`, `dbcman`).

| Disc path | Size | `.iopmod` name | Version | PS2Recomp |
|---|---:|---|---|---|
| `MODULES/KOEISND.IRX` | 41 013 | `KOEI_Sound_Driver` | 1.02 | **not covered** — Koei-specific, needs HLE or IOP emulation |
| `MODULES/LIBSD.IRX` | 26 301 | `Sound_Device_Library` | 1.04 | HLE service (`libsd`) |
| `MODULES/MCMAN.IRX` | 90 533 | `mcman` | 2.1A | built-in stub |
| `MODULES/MCSERV.IRX` | 7 353 | `mcserv` | 2.0E | HLE service (`mcserv`) |
| `MODULES/MODHSYN.IRX` | 58 597 | `SPU2_Synthesizer_Module` | 1.05 | **not covered** (Sony hardware synthesizer, `modhsyn`) |
| `MODULES/MODMSIN.IRX` | 1 929 | `Midi_Message_Input_Module` | 1.01 | **not covered** (Sony MIDI input, `modmsin`) |
| `MODULES/PADMAN.IRX` | 43 861 | `padman` | 4.02 | built-in stub |
| `MODULES/SIO2MAN.IRX` | 6 161 | `sio2man` | 2.04 | built-in stub |
| `MODULES/IOPRP23.IMG:LOADCORE` | 9 461 | `Module_Manager` | 2.01 | built-in stub (`loadcore`) |
| `MODULES/IOPRP23.IMG:SIFCMD` | 10 105 | `IOP_SIF_rpc_interface` | 2.07 | built-in stub |
| `MODULES/IOPRP23.IMG:SIFMAN` | 6 009 | `IOP_SIF_manager` | 2.01 | built-in stub |
| `MODULES/IOPRP23.IMG:THREADMAN` | 36 929 | `Multi_Thread_Manager` | 2.02 | built-in stubs (`thbase`/`thevent`/…) |
| `MODULES/IOPRP23.IMG:IOMAN` | 11 177 | `IO/File_Manager` | 2.01 | built-in stub |
| `MODULES/IOPRP23.IMG:MODLOAD` | 13 285 | `Moldule_File_loader` (sic) | 2.03 | built-in stub |
| `MODULES/IOPRP23.IMG:FILEIO` | 17 829 | `FILEIO_service` | 2.0A | built-in stub |
| `MODULES/IOPRP23.IMG:CDVDMAN` | 53 837 | `cdvd_driver` | 2.15 | built-in stub |
| `MODULES/IOPRP23.IMG:CDVDFSV` | 34 261 | `cdvd_ee_driver` | 2.15 | built-in stub |
| `MODULES/IOPRP23.IMG:LOADFILE` | 11 617 | `LoadModuleByEE` | 2.01 | **not in list** (EE-side loader RPC; handled by runtime SIF stubs) |
| `MODULES/IOPRP23.IMG:TIMEMANI` | 5 813 | `Timer_Manager` | 2.01 | built-in stub (`timrman`) |
| `MODULES/IOPRP23.IMG:ROMDRV` | 3 881 | `ROM_file_driver` | 2.01 | **not in list** |
| `MODULES/IOPRP23.IMG:EESYNC` | 1 545 | `SyncEE` | 2.01 | **not in list** |
| `MODULES/IOPRP23.IMG:SYSCLIB` | 10 213 | `System_C_lib` | 2.01 | built-in stub |
| `MODULES/IOPRP23.IMG:STDIO` | 3 337 | `Stdio` | 2.01 | built-in stub |

Coverage "built-in stub" means PS2Recomp reports a successful load without running module code. Matching is by file-name key and must be re-checked in Phase 4 against the paths the ELF actually passes to `SifLoadModule`.
