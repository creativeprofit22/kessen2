// SPDX-License-Identifier: GPL-3.0-or-later
// Magic-byte identification and per-type aggregation for disc files / container members.
#pragma once

#include "bytes.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace k2disc {

inline constexpr std::size_t kMagicProbeBytes = 64;

struct FileKind {
    std::string id;          // short stable key, e.g. "elf-irx", "sony-sshd", "unknown:1C000000"
    std::string description; // human-readable
    std::string extension;   // suggested extension for extracted members ("bin" if unknown)
};

// Identifies a file from its first bytes (pass at least kMagicProbeBytes if available).
[[nodiscard]] FileKind identify(Bytes head);

// Upper-case extension of a path ("" if none), e.g. "LINKDATA.ANS" -> "ANS".
[[nodiscard]] std::string extension_of(std::string_view path);

struct CatalogueRow {
    std::string extension;
    std::string kind_id;
    std::string description;
    std::uint64_t count = 0;
    std::uint64_t total_bytes = 0;
    std::vector<std::string> samples; // up to 3 paths, sorted
};

class Catalogue {
public:
    void add(std::string_view path, std::uint64_t size, const FileKind& kind);
    // Rows sorted by (extension, kind id).
    [[nodiscard]] std::vector<CatalogueRow> rows() const;
    [[nodiscard]] std::uint64_t total_files() const noexcept { return files_; }
    [[nodiscard]] std::uint64_t total_bytes() const noexcept { return bytes_; }

private:
    std::map<std::pair<std::string, std::string>, CatalogueRow> rows_;
    std::uint64_t files_ = 0;
    std::uint64_t bytes_ = 0;
};

[[nodiscard]] std::string render_tsv(const Catalogue& c);
[[nodiscard]] std::string render_markdown(const Catalogue& c);

} // namespace k2disc
