// SPDX-License-Identifier: GPL-3.0-or-later
// Kessen II LINKDATA container: Koei link archives and the sector-addressed outer stream.
// Spec: docs/formats/linkdata.md
#pragma once

#include "bytes.hpp"
#include "error.hpp"
#include "source.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace k2disc {

inline constexpr std::uint32_t kLinkMaxCount = 65535;

struct LinkEntry {
    std::uint64_t offset = 0; // relative to the archive start
    std::uint64_t size = 0;
};

struct LinkArchive {
    std::uint64_t total_size = 0; // offsets[count]
    std::vector<LinkEntry> entries;
};

// Header size for `count` entries: align16(4 * (count + 2)).
[[nodiscard]] constexpr std::uint64_t link_header_size(std::uint64_t count) noexcept {
    return (4 * (count + 2) + 15) & ~std::uint64_t{15};
}

// Parses a link archive at `base` in `src`, where at most `limit` bytes belong to it.
// If `exact`, offsets[count] must equal `limit` (nested entries); otherwise it must be <= limit.
[[nodiscard]] Result<LinkArchive> parse_link_archive(const ByteSource& src, std::uint64_t base, std::uint64_t limit,
                                                     bool exact);

// Cheap yes/no probe with the same rules (no error message allocation on the hot path).
[[nodiscard]] bool is_link_archive(const ByteSource& src, std::uint64_t base, std::uint64_t limit, bool exact);

enum class MemberKind { LinkArchive, Region };

struct OuterMember {
    MemberKind kind = MemberKind::Region;
    std::uint32_t sector = 0;   // start sector (2048 bytes)
    std::uint32_t sectors = 0;  // length in sectors (including padding)
    std::uint64_t size = 0;     // meaningful bytes: offsets[count] for archives, sectors*2048 for regions
    std::uint32_t entries = 0;  // link archive entry count (0 for regions)
};

// Splits a LINKDATA.BNS-style stream: link archives back-to-back on sector boundaries, anything
// between them is a Region. Fails if the size is not a multiple of 2048 or a link archive's padding
// is not zero.
[[nodiscard]] Result<std::vector<OuterMember>> segment_linkdata(const ByteSource& src);

// One row per written file in an extraction (also written to <out>/index.tsv).
struct ExtractedFile {
    std::string path; // relative to the output directory, '/'-separated
    std::uint64_t size = 0;
    std::string kind; // catalogue kind id
};

struct ArchiveExtractOptions {
    std::size_t max_depth = 8;                   // nested link-archive recursion limit
    std::uint64_t max_member_bytes = 512ull << 20; // members are buffered in memory
};

// Extracts `src` into `out_dir`:
//  - if the whole source is one link archive (offsets[count] == size), its entries;
//  - otherwise the LINKDATA sector stream (segment_linkdata), outer members named
//    NNNNN_sSSSSS.<ext> / NNNNN_sSSSSS.region.bin.
// Link-archive entries that are themselves link archives become subdirectories (recursively).
[[nodiscard]] Result<std::vector<ExtractedFile>> extract_link_container(const ByteSource& src,
                                                                       const std::filesystem::path& out_dir,
                                                                       const ArchiveExtractOptions& opt = {});

} // namespace k2disc
