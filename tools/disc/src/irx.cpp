// SPDX-License-Identifier: GPL-3.0-or-later
#include "irx.hpp"

#include <cctype>

namespace k2disc {
namespace {

constexpr std::size_t kIopModNameOff = 26;
constexpr std::size_t kRomdirEntrySize = 16;
constexpr std::size_t kMaxRomdirEntries = 4096;

Result<IrxInfo> read_iopmod(Bytes blk, IrxInfo info) {
    if (blk.size() < kIopModNameOff + 1) {
        return fail(".iopmod block too small (" + std::to_string(blk.size()) + " bytes)");
    }
    info.has_iopmod = true;
    info.entry = *u32le(blk, 4);
    info.text_size = *u32le(blk, 12);
    info.data_size = *u32le(blk, 16);
    info.bss_size = *u32le(blk, 20);
    info.version = *u16le(blk, 24);
    for (std::size_t i = kIopModNameOff; i < blk.size() && blk[i] != 0; ++i) {
        const char c = static_cast<char>(blk[i]);
        info.name += std::isprint(static_cast<unsigned char>(c)) ? c : '?';
    }
    return info;
}

} // namespace

std::string IrxInfo::version_string() const {
    const auto hex2 = [](unsigned v) {
        constexpr char d[] = "0123456789ABCDEF";
        return std::string{d[(v >> 4) & 0xF], d[v & 0xF]};
    };
    return std::to_string(version >> 8) + "." + hex2(version & 0xFFu);
}

Result<IrxInfo> parse_irx(Bytes elf) {
    if (!has_tag(elf, 0, "\x7F" "ELF")) {
        return fail("not an ELF file (bad magic at offset 0)");
    }
    if (elf.size() < 52 || elf[4] != 1 || elf[5] != 1) {
        return fail("not a 32-bit little-endian ELF");
    }
    IrxInfo info;
    info.elf_type = *u16le(elf, 16);
    const auto machine = *u16le(elf, 18);
    if (machine != 8) {
        return fail("ELF machine " + std::to_string(machine) + " is not MIPS");
    }

    // Section headers first.
    const auto shoff = *u32le(elf, 32);
    const auto shentsize = *u16le(elf, 46);
    const auto shnum = *u16le(elf, 48);
    if (shoff != 0 && shnum != 0) {
        if (shentsize < 40 || !sub(elf, shoff, static_cast<std::size_t>(shentsize) * shnum)) {
            return fail("section header table out of range at offset " + std::to_string(shoff));
        }
        for (std::size_t i = 0; i < shnum; ++i) {
            const std::size_t sh = shoff + i * shentsize;
            if (*u32le(elf, sh + 4) != kIopModType) {
                continue;
            }
            const auto off = *u32le(elf, sh + 16);
            const auto size = *u32le(elf, sh + 20);
            const auto blk = sub(elf, off, size);
            if (!blk) {
                return fail(".iopmod section out of range at offset " + std::to_string(off));
            }
            return read_iopmod(*blk, info);
        }
    }
    // Fall back to program headers.
    const auto phoff = *u32le(elf, 28);
    const auto phentsize = *u16le(elf, 42);
    const auto phnum = *u16le(elf, 44);
    if (phoff != 0 && phnum != 0) {
        if (phentsize < 32 || !sub(elf, phoff, static_cast<std::size_t>(phentsize) * phnum)) {
            return fail("program header table out of range at offset " + std::to_string(phoff));
        }
        for (std::size_t i = 0; i < phnum; ++i) {
            const std::size_t ph = phoff + i * phentsize;
            if (*u32le(elf, ph) != kIopModType) {
                continue;
            }
            const auto off = *u32le(elf, ph + 4);
            const auto size = *u32le(elf, ph + 16);
            const auto blk = sub(elf, off, size);
            if (!blk) {
                return fail(".iopmod segment out of range at offset " + std::to_string(off));
            }
            return read_iopmod(*blk, info);
        }
    }
    return info; // valid ELF without .iopmod (has_iopmod = false)
}

Result<std::vector<RomdirEntry>> parse_romdir(Bytes image) {
    if (!has_tag(image, 0, "RESET")) {
        return fail("not a ROMDIR image (no RESET entry at offset 0)");
    }
    std::vector<RomdirEntry> out;
    std::uint64_t data_off = 0;
    for (std::size_t i = 0;; ++i) {
        if (i >= kMaxRomdirEntries) {
            return fail("ROMDIR has more than " + std::to_string(kMaxRomdirEntries) + " entries");
        }
        const auto e = sub(image, i * kRomdirEntrySize, kRomdirEntrySize);
        if (!e) {
            return fail("ROMDIR not terminated before end of image (entry " + std::to_string(i) + ")");
        }
        if ((*e)[0] == 0) {
            break; // terminator
        }
        RomdirEntry r;
        for (std::size_t k = 0; k < 10 && (*e)[k] != 0; ++k) {
            const char c = static_cast<char>((*e)[k]);
            r.name += std::isprint(static_cast<unsigned char>(c)) ? c : '?';
        }
        r.ext_info_size = *u16le(*e, 10);
        r.size = *u32le(*e, 12);
        if (data_off + r.size > image.size()) {
            return fail("ROMDIR entry '" + r.name + "' (" + std::to_string(r.size) + " bytes at offset " +
                        std::to_string(data_off) + ") runs past end of image");
        }
        r.offset = static_cast<std::uint32_t>(data_off);
        data_off = (data_off + r.size + 15) & ~std::uint64_t{15};
        out.push_back(std::move(r));
    }
    return out;
}

std::vector<IopModuleRow> describe_iop_file(std::string_view path, Bytes data) {
    std::vector<IopModuleRow> rows;
    const auto describe = [](std::string p, Bytes b) {
        IopModuleRow row{std::move(p), b.size(), {}, {}, {}};
        if (!has_tag(b, 0, "\x7F" "ELF")) {
            row.note = "not ELF";
            return row;
        }
        const auto irx = parse_irx(b);
        if (!irx) {
            row.note = irx.error().message;
        } else if (!irx->has_iopmod) {
            row.note = "no .iopmod";
        } else {
            row.module = irx->name;
            row.version = irx->version_string();
            if (irx->elf_type != kElfTypeIrx) {
                row.note = "ELF type " + std::to_string(irx->elf_type);
            }
        }
        return row;
    };
    std::string upper_path(path);
    for (auto& c : upper_path) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    if (upper_path.ends_with(".IRX")) {
        rows.push_back(describe(std::string(path), data));
        return rows;
    }
    if (has_tag(data, 0, "RESET")) {
        const auto dir = parse_romdir(data);
        if (!dir) {
            rows.push_back({std::string(path), data.size(), {}, {}, dir.error().message});
            return rows;
        }
        for (const auto& e : *dir) {
            if (e.size == 0 || e.name == "ROMDIR" || e.name == "EXTINFO") {
                continue; // directory metadata, not modules
            }
            rows.push_back(describe(std::string(path) + ":" + e.name, *sub(data, e.offset, e.size)));
        }
    }
    return rows;
}

} // namespace k2disc
