// SPDX-License-Identifier: GPL-3.0-or-later
// A read-only file tree: either an ISO image or an extracted directory on disk.
#pragma once

#include "error.hpp"
#include "iso9660.hpp"
#include "source.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace k2disc {

struct TreeFile {
    std::string path; // '/'-separated relative path
    std::uint64_t size = 0;
};

class FileTree {
public:
    FileTree() = default;
    FileTree(const FileTree&) = delete;
    FileTree& operator=(const FileTree&) = delete;
    virtual ~FileTree() = default;

    // Regular files only, sorted by path.
    [[nodiscard]] virtual const std::vector<TreeFile>& files() const noexcept = 0;
    // Random access to one file's bytes. The source may borrow from the tree.
    [[nodiscard]] virtual Result<std::unique_ptr<ByteSource>> open(const TreeFile& file) const = 0;
};

// Metadata files k2disc itself writes at the root of an output directory (`archive extract`
// writes index.tsv, `extract` writes manifest.tsv). Not part of the scanned content.
inline constexpr std::array<std::string_view, 2> kToolMetadataFiles{"index.tsv", "manifest.tsv"};

// Opens a directory (walked recursively, symlinks not followed) or an ISO image file.
// For directories, files directly at the root whose name is in `exclude_root` are skipped;
// same-named files in subdirectories and all ISO image files are kept.
[[nodiscard]] Result<std::unique_ptr<FileTree>> open_tree(const std::filesystem::path& path,
                                                          std::span<const std::string_view> exclude_root = {});

// Reads the whole file if it is <= max_size bytes.
[[nodiscard]] Result<std::vector<std::uint8_t>> read_all(const ByteSource& src, std::uint64_t max_size);

} // namespace k2disc
