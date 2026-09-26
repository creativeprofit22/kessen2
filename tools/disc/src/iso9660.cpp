// SPDX-License-Identifier: GPL-3.0-or-later
#include "iso9660.hpp"

#include "bytes.hpp"
#include "paths.hpp"

#include <algorithm>
#include <cctype>
#include <set>

namespace k2disc {
namespace {

constexpr std::uint32_t kFirstVdSector = 16;
constexpr std::uint32_t kMaxVdSectors = 64;
constexpr std::uint8_t kFlagDir = 0x02;
constexpr std::uint8_t kFlagMultiExtent = 0x80;

std::string trim_right(std::string_view s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\0')) {
        s.remove_suffix(1);
    }
    return std::string(s);
}

std::string upper(std::string_view s) {
    std::string out(s);
    std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return out;
}

// ISO file identifier -> plain name ("FOO.BIN;1" -> "FOO.BIN", "README.;1" -> "README").
std::string iso_name(std::string_view id, bool is_dir) {
    if (!is_dir) {
        if (const auto semi = id.rfind(';'); semi != std::string_view::npos) {
            id = id.substr(0, semi);
        }
        while (!id.empty() && id.back() == '.') {
            id.remove_suffix(1);
        }
    }
    return std::string(id);
}

struct DirJob {
    std::uint32_t lba;
    std::uint32_t length;
    std::string prefix;
    std::size_t depth;
};

std::string where(std::uint64_t off) {
    return "offset 0x" + hex32(static_cast<std::uint32_t>(off >> 32)) + hex32(static_cast<std::uint32_t>(off));
}

Result<void> check_extent(const ByteSource& image, std::uint32_t lba, std::uint32_t length, std::string_view what) {
    if (length == 0) {
        return {}; // empty files may carry any LBA; nothing is read
    }
    const std::uint64_t off = static_cast<std::uint64_t>(lba) * kSectorSize;
    if (!range_ok(off, length, image.size())) {
        return fail(std::string(what) + ": extent LBA " + std::to_string(lba) + " +" + std::to_string(length) +
                    " bytes lies outside the image (" + std::to_string(image.size()) + " bytes)");
    }
    return {};
}

} // namespace

const IsoEntry* IsoVolume::find(std::string_view path) const {
    const auto key = upper(path);
    for (const auto& e : entries) {
        if (upper(e.path) == key) {
            return &e;
        }
    }
    return nullptr;
}

Result<IsoVolume> read_iso9660(const ByteSource& image, const IsoLimits& limits) {
    IsoVolume vol;
    std::vector<std::uint8_t> root_record;
    bool have_pvd = false;

    for (std::uint32_t s = kFirstVdSector; s < kFirstVdSector + kMaxVdSectors; ++s) {
        const std::uint64_t off = static_cast<std::uint64_t>(s) * kSectorSize;
        if (!range_ok(off, kSectorSize, image.size())) {
            break;
        }
        auto sec = image.read_vec(off, kSectorSize);
        if (!sec) {
            return std::unexpected(sec.error());
        }
        const Bytes b(*sec);
        const auto id = chars(b.subspan(1, 5));
        if (id == "CD001") {
            if (b[0] == 1 && !have_pvd) {
                have_pvd = true;
                vol.system_id = trim_right(chars(b.subspan(8, 32)));
                vol.volume_id = trim_right(chars(b.subspan(40, 32)));
                vol.volume_blocks = *u32le(b, 80);
                const auto block = *u16le(b, 128);
                if (block != kSectorSize) {
                    return fail("PVD at " + where(off) + ": logical block size " + std::to_string(block) +
                                " is not 2048");
                }
                root_record.assign(sec->begin() + 156, sec->begin() + 156 + 34);
            }
            continue; // also skips terminator (type 255) and supplementary descriptors
        }
        if (id == "NSR02" || id == "NSR03") {
            vol.udf_nsr = std::string(id);
            continue;
        }
        if (id == "BEA01" || id == "TEA01" || id == "BOOT2" || id == "CDW02") {
            continue;
        }
        break; // end of volume recognition area
    }
    if (!have_pvd) {
        return fail("no ISO9660 primary volume descriptor found (sector 16+): not an ISO9660 image");
    }

    const Bytes root(root_record);
    if (root[0] < 34 || (root[25] & kFlagDir) == 0) {
        return fail("PVD root directory record is malformed");
    }
    std::vector<DirJob> stack{{*u32le(root, 2), *u32le(root, 10), "", 0}};
    std::set<std::uint32_t> visited;

    while (!stack.empty()) {
        const DirJob job = stack.back();
        stack.pop_back();
        const std::string dir_label = job.prefix.empty() ? "/" : job.prefix;
        if (job.depth > limits.max_depth) {
            return fail("directory nesting deeper than " + std::to_string(limits.max_depth) + " at '" + dir_label + "'");
        }
        if (!visited.insert(job.lba).second) {
            return fail("directory loop: LBA " + std::to_string(job.lba) + " reached twice ('" + dir_label + "')");
        }
        if (job.length > limits.max_dir_bytes) {
            return fail("directory '" + dir_label + "' too large (" + std::to_string(job.length) + " bytes)");
        }
        if (auto r = check_extent(image, job.lba, job.length, "directory '" + dir_label + "'"); !r) {
            return std::unexpected(r.error());
        }
        auto data = image.read_vec(static_cast<std::uint64_t>(job.lba) * kSectorSize, job.length);
        if (!data) {
            return std::unexpected(data.error());
        }
        const Bytes d(*data);

        std::set<std::string> seen;
        IsoEntry pending; // multi-extent accumulator
        bool have_pending = false;
        std::size_t pos = 0;
        while (pos < d.size()) {
            const std::size_t sector_end = std::min<std::size_t>((pos / kSectorSize + 1) * kSectorSize, d.size());
            const std::uint8_t len = d[pos];
            if (len == 0) {
                pos = sector_end; // records never span sectors; rest of sector is padding
                continue;
            }
            const auto rec_off = static_cast<std::uint64_t>(job.lba) * kSectorSize + pos;
            if (len < 34 || pos + len > sector_end) {
                return fail("directory '" + dir_label + "': bad record length " + std::to_string(len) + " at " +
                            where(rec_off));
            }
            const Bytes rec = d.subspan(pos, len);
            pos += len;

            const std::uint8_t name_len = rec[32];
            if (name_len == 0 || 33u + name_len > len) {
                return fail("directory '" + dir_label + "': bad name length at " + where(rec_off));
            }
            const auto raw_name = chars(rec.subspan(33, name_len));
            if (name_len == 1 && (raw_name[0] == '\0' || raw_name[0] == '\1')) {
                continue; // "." and ".."
            }
            const std::uint8_t flags = rec[25];
            const bool is_dir = (flags & kFlagDir) != 0;
            const Extent ext{*u32le(rec, 2), *u32le(rec, 10)};

            auto name = sanitize_component(iso_name(raw_name, is_dir));
            if (!name) {
                return fail("directory '" + dir_label + "': " + name.error().message + " at " + where(rec_off));
            }
            const std::string path = job.prefix.empty() ? *name : job.prefix + "/" + *name;
            if (auto r = check_extent(image, ext.lba, ext.length, "'" + path + "'"); !r) {
                return std::unexpected(r.error());
            }

            if (have_pending) {
                if (pending.path != path || is_dir) {
                    return fail("'" + pending.path + "': multi-extent file not continued at " + where(rec_off));
                }
                pending.extents.push_back(ext);
                pending.size += ext.length;
            } else {
                if (!seen.insert(upper(path)).second) {
                    return fail("duplicate directory entry '" + path + "' at " + where(rec_off));
                }
                pending = IsoEntry{path, is_dir, is_dir ? 0 : ext.length, {ext}};
            }
            if (!is_dir && (flags & kFlagMultiExtent) != 0) {
                have_pending = true;
                continue;
            }
            have_pending = false;
            if (vol.entries.size() >= limits.max_entries) {
                return fail("more than " + std::to_string(limits.max_entries) + " entries");
            }
            if (is_dir) {
                stack.push_back({ext.lba, ext.length, path, job.depth + 1});
            }
            vol.entries.push_back(std::move(pending));
            pending = IsoEntry{};
        }
        if (have_pending) {
            return fail("'" + pending.path + "': multi-extent file truncated at end of directory");
        }
    }

    std::ranges::sort(vol.entries, {}, &IsoEntry::path);
    return vol;
}

Result<void> for_each_chunk(const ByteSource& image, const IsoEntry& file, std::size_t chunk,
                            const std::function<Result<void>(std::span<const std::uint8_t>)>& sink) {
    if (chunk == 0) {
        return fail("chunk size must be > 0");
    }
    std::vector<std::uint8_t> buf(chunk);
    for (const auto& ext : file.extents) {
        if (auto r = check_extent(image, ext.lba, ext.length, "'" + file.path + "'"); !r) {
            return r;
        }
        std::uint64_t done = 0;
        while (done < ext.length) {
            const auto n = static_cast<std::size_t>(std::min<std::uint64_t>(chunk, ext.length - done));
            const auto part = std::span(buf).first(n);
            if (auto r = image.read(static_cast<std::uint64_t>(ext.lba) * kSectorSize + done, part); !r) {
                return r;
            }
            if (auto r = sink(part); !r) {
                return r;
            }
            done += n;
        }
    }
    return {};
}

Result<std::vector<std::uint8_t>> read_file(const ByteSource& image, const IsoEntry& file, std::uint64_t max_size) {
    if (file.is_dir) {
        return fail("'" + file.path + "' is a directory");
    }
    if (file.size > max_size) {
        return fail("'" + file.path + "' is " + std::to_string(file.size) + " bytes (limit " +
                    std::to_string(max_size) + ")");
    }
    std::vector<std::uint8_t> out;
    out.reserve(static_cast<std::size_t>(file.size));
    auto r = for_each_chunk(image, file, 1u << 20, [&](std::span<const std::uint8_t> p) -> Result<void> {
        out.insert(out.end(), p.begin(), p.end());
        return {};
    });
    if (!r) {
        return std::unexpected(r.error());
    }
    return out;
}

} // namespace k2disc
