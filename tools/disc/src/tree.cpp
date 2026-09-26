// SPDX-License-Identifier: GPL-3.0-or-later
#include "tree.hpp"

#include "paths.hpp"

#include <algorithm>
#include <system_error>

namespace k2disc {
namespace {

class DirTree final : public FileTree {
public:
    DirTree(std::filesystem::path root, std::vector<TreeFile> files) : root_(std::move(root)), files_(std::move(files)) {}
    [[nodiscard]] const std::vector<TreeFile>& files() const noexcept override { return files_; }
    [[nodiscard]] Result<std::unique_ptr<ByteSource>> open(const TreeFile& f) const override {
        const std::u8string rel(f.path.begin(), f.path.end());
        auto src = FileSource::open(root_ / std::filesystem::path(rel));
        if (!src) {
            return std::unexpected(src.error());
        }
        return std::unique_ptr<ByteSource>(std::move(*src));
    }

private:
    std::filesystem::path root_;
    std::vector<TreeFile> files_;
};

class IsoTree final : public FileTree {
public:
    IsoTree(std::unique_ptr<FileSource> image, IsoVolume vol) : image_(std::move(image)), vol_(std::move(vol)) {
        for (const auto& e : vol_.entries) {
            if (!e.is_dir) {
                files_.push_back({e.path, e.size});
            }
        }
    }
    [[nodiscard]] const std::vector<TreeFile>& files() const noexcept override { return files_; }
    [[nodiscard]] Result<std::unique_ptr<ByteSource>> open(const TreeFile& f) const override {
        const IsoEntry* e = vol_.find(f.path);
        if (e == nullptr || e->is_dir) {
            return fail("'" + f.path + "' not in image");
        }
        if (e->extents.size() == 1) {
            return std::unique_ptr<ByteSource>(std::make_unique<SliceSource>(
                *image_, static_cast<std::uint64_t>(e->extents[0].lba) * kSectorSize, e->size));
        }
        // Multi-extent: materialise (rare; PS2 files are < 4 GiB and usually single-extent).
        auto data = read_file(*image_, *e, 1ull << 31);
        if (!data) {
            return std::unexpected(data.error());
        }
        return std::unique_ptr<ByteSource>(std::make_unique<MemorySource>(std::move(*data)));
    }

private:
    std::unique_ptr<FileSource> image_;
    IsoVolume vol_;
    std::vector<TreeFile> files_;
};

} // namespace

Result<std::unique_ptr<FileTree>> open_tree(const std::filesystem::path& path,
                                            std::span<const std::string_view> exclude_root) {
    std::error_code ec;
    if (std::filesystem::is_directory(path, ec)) {
        std::vector<TreeFile> files;
        std::filesystem::recursive_directory_iterator it(path, std::filesystem::directory_options::none, ec);
        if (ec) {
            return fail("cannot list '" + display_path(path) + "': " + ec.message());
        }
        for (const auto& de : it) {
            if (!de.is_regular_file(ec) || de.is_symlink(ec)) {
                continue;
            }
            const auto rel = de.path().lexically_relative(path).generic_u8string();
            std::string rel_path(rel.begin(), rel.end());
            if (rel_path.find('/') == std::string::npos && std::ranges::find(exclude_root, rel_path) != exclude_root.end()) {
                continue;
            }
            files.push_back({std::move(rel_path), de.file_size(ec)});
        }
        std::ranges::sort(files, {}, &TreeFile::path);
        return std::unique_ptr<FileTree>(std::make_unique<DirTree>(path, std::move(files)));
    }
    auto img = FileSource::open(path);
    if (!img) {
        return std::unexpected(img.error());
    }
    auto vol = read_iso9660(**img);
    if (!vol) {
        return std::unexpected(vol.error());
    }
    return std::unique_ptr<FileTree>(std::make_unique<IsoTree>(std::move(*img), std::move(*vol)));
}

Result<std::vector<std::uint8_t>> read_all(const ByteSource& src, std::uint64_t max_size) {
    if (src.size() > max_size) {
        return fail("file is " + std::to_string(src.size()) + " bytes (limit " + std::to_string(max_size) + ")");
    }
    return src.read_vec(0, static_cast<std::size_t>(src.size()));
}

} // namespace k2disc
