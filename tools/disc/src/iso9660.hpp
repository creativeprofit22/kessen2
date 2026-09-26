// SPDX-License-Identifier: GPL-3.0-or-later
// ISO9660 reader for PS2 DVD images (ISO9660/UDF bridge). Reads the primary volume
// descriptor tree including multi-extent files; detects (but does not read) UDF.
#pragma once

#include "error.hpp"
#include "source.hpp"

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace k2disc {

inline constexpr std::uint32_t kSectorSize = 2048;

struct Extent {
    std::uint32_t lba = 0;
    std::uint32_t length = 0; // bytes
};

struct IsoEntry {
    std::string path; // '/'-separated, sanitised, no leading '/', version suffix stripped
    bool is_dir = false;
    std::uint64_t size = 0; // sum of extent lengths (files only)
    std::vector<Extent> extents;

    [[nodiscard]] std::uint32_t first_lba() const noexcept { return extents.empty() ? 0 : extents.front().lba; }
};

struct IsoVolume {
    std::string volume_id;
    std::string system_id;
    std::uint32_t volume_blocks = 0;
    std::string udf_nsr; // "NSR02"/"NSR03" if a UDF volume recognition sequence exists, else empty
    std::vector<IsoEntry> entries; // sorted by path; directories included

    // Case-insensitive lookup by path (e.g. "SYSTEM.CNF").
    [[nodiscard]] const IsoEntry* find(std::string_view path) const;
};

struct IsoLimits {
    std::size_t max_depth = 32;
    std::size_t max_entries = 1'000'000;
    std::uint32_t max_dir_bytes = 16u * 1024 * 1024;
};

[[nodiscard]] Result<IsoVolume> read_iso9660(const ByteSource& image, const IsoLimits& limits = {});

// Streams a file's bytes in chunks of at most `chunk` bytes.
[[nodiscard]] Result<void> for_each_chunk(const ByteSource& image, const IsoEntry& file, std::size_t chunk,
                                          const std::function<Result<void>(std::span<const std::uint8_t>)>& sink);

// Reads a whole file; fails if it is larger than max_size.
[[nodiscard]] Result<std::vector<std::uint8_t>> read_file(const ByteSource& image, const IsoEntry& file,
                                                          std::uint64_t max_size);

} // namespace k2disc
