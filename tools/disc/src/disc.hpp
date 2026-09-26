// SPDX-License-Identifier: GPL-3.0-or-later
// High-level disc operations: identify the boot ELF and extract the ISO tree.
#pragma once

#include "error.hpp"
#include "iso9660.hpp"
#include "source.hpp"
#include "system_cnf.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

namespace k2disc {

struct DiscInfo {
    IsoVolume volume;
    SystemCnf cnf;
    IsoEntry boot; // the boot ELF entry
    std::uint32_t boot_crc32 = 0;
    std::string label; // serial if known, else sanitised volume id; used as the work subdirectory
};

// Parses the ISO, SYSTEM.CNF, and CRC32s the whole boot ELF (PS2Recomp-compatible).
[[nodiscard]] Result<DiscInfo> inspect_disc(const ByteSource& image);

struct ExtractSummary {
    std::size_t files = 0;
    std::size_t dirs = 0;
    std::uint64_t bytes = 0;
    std::filesystem::path manifest;
};

// Writes every file to <out_root>/iso/<path> and a sorted manifest to <out_root>/manifest.tsv
// (columns: path, lba, size, crc32). Paths are sanitised and contained under out_root.
[[nodiscard]] Result<ExtractSummary> extract_disc(const ByteSource& image, const IsoVolume& volume,
                                                  const std::filesystem::path& out_root);

} // namespace k2disc
