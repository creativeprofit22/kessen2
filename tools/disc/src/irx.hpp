// SPDX-License-Identifier: GPL-3.0-or-later
// IOP module (IRX) identification and IOPRP/ROMDIR image listing.
#pragma once

#include "bytes.hpp"
#include "error.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace k2disc {

inline constexpr std::uint16_t kElfTypeIrx = 0xFF80;
inline constexpr std::uint32_t kIopModType = 0x70000080; // SHT/PT type of the .iopmod block

struct IrxInfo {
    std::uint16_t elf_type = 0;
    bool has_iopmod = false;
    std::string name;            // module name from .iopmod (empty if none)
    std::uint16_t version = 0;   // major = version >> 8, minor = version & 0xFF
    std::uint32_t entry = 0;
    std::uint32_t text_size = 0;
    std::uint32_t data_size = 0;
    std::uint32_t bss_size = 0;

    [[nodiscard]] std::string version_string() const;
};

// Parses an ELF32 LE MIPS IOP module; finds .iopmod via section headers, else program headers.
[[nodiscard]] Result<IrxInfo> parse_irx(Bytes elf);

struct RomdirEntry {
    std::string name;
    std::uint32_t offset = 0; // byte offset of the file inside the image
    std::uint32_t size = 0;
    std::uint16_t ext_info_size = 0;
};

// Lists an IOPRP/ROMDIR image (entries of name[10], u16 extinfo size, u32 size; data 16-byte aligned).
[[nodiscard]] Result<std::vector<RomdirEntry>> parse_romdir(Bytes image);

// One IOP module found on disc: a loose .IRX file or a member of an IOPRP (ROMDIR) image.
struct IopModuleRow {
    std::string path;   // disc path; ROMDIR members as "<image>:<entry>"
    std::uint64_t size = 0;
    std::string module; // .iopmod name, or empty
    std::string version; // "1.01", or empty
    std::string note;   // e.g. "no .iopmod", "not ELF", parse error
};

// Describes an .IRX file (by extension) or ROMDIR image (by RESET magic); other files yield no rows.
[[nodiscard]] std::vector<IopModuleRow> describe_iop_file(std::string_view path, Bytes data);

} // namespace k2disc
