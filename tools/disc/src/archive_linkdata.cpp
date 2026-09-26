// SPDX-License-Identifier: GPL-3.0-or-later
#include "archive_linkdata.hpp"

#include "catalogue.hpp"
#include "paths.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <string>
#include <system_error>

namespace k2disc {
namespace {

constexpr std::uint32_t kSector = 2048;

struct Probe {
    bool ok = false;
    std::string why;
    LinkArchive archive;
};

// Shared implementation; `want_entries` avoids building the entry vector for probes.
Probe probe(const ByteSource& src, std::uint64_t base, std::uint64_t limit, bool exact, bool want_entries) {
    Probe p;
    const auto at = [&](std::uint64_t rel) { return " (archive at offset " + std::to_string(base) + ", +" + std::to_string(rel) + ")"; };
    if (!range_ok(base, limit, src.size())) {
        p.why = "archive extent outside source" + at(0);
        return p;
    }
    if (limit < 8) {
        p.why = "too small for a link archive header" + at(0);
        return p;
    }
    std::array<std::uint8_t, 8> head{};
    if (!src.read(base, head)) {
        p.why = "read failed" + at(0);
        return p;
    }
    const std::uint32_t count = *u32le(head, 0);
    const std::uint32_t first = *u32le(head, 4);
    if (count == 0 || count > kLinkMaxCount) {
        p.why = "entry count " + std::to_string(count) + " out of range" + at(0);
        return p;
    }
    const auto hdr = link_header_size(count);
    if (hdr > limit) {
        p.why = "header (" + std::to_string(hdr) + " bytes) larger than archive" + at(0);
        return p;
    }
    if (first != hdr) {
        p.why = "offsets[0] = " + std::to_string(first) + ", expected header size " + std::to_string(hdr) + at(4);
        return p;
    }
    auto table = src.read_vec(base + 4, static_cast<std::size_t>(4) * (count + 1));
    if (!table) {
        p.why = table.error().message;
        return p;
    }
    const Bytes t(*table);
    if (want_entries) {
        p.archive.entries.reserve(count);
    }
    std::uint32_t prev = first;
    for (std::uint32_t i = 1; i <= count; ++i) {
        const std::uint32_t cur = *u32le(t, 4u * i);
        if (cur < prev) {
            p.why = "offsets[" + std::to_string(i) + "] = " + std::to_string(cur) + " decreases" + at(4 + 4ull * i);
            return p;
        }
        if (cur > limit) {
            p.why = "offsets[" + std::to_string(i) + "] = " + std::to_string(cur) + " past archive end " +
                    std::to_string(limit) + at(4 + 4ull * i);
            return p;
        }
        if (want_entries) {
            p.archive.entries.push_back({prev, static_cast<std::uint64_t>(cur) - prev});
        }
        prev = cur;
    }
    if (exact && prev != limit) {
        p.why = "offsets[count] = " + std::to_string(prev) + " does not match entry size " + std::to_string(limit) +
                at(4 + 4ull * count);
        return p;
    }
    p.archive.total_size = prev;
    p.ok = true;
    return p;
}

Result<bool> all_zero(const ByteSource& src, std::uint64_t off, std::uint64_t len) {
    if (len == 0) {
        return true;
    }
    auto b = src.read_vec(off, static_cast<std::size_t>(len));
    if (!b) {
        return std::unexpected(b.error());
    }
    return std::ranges::all_of(*b, [](std::uint8_t x) { return x == 0; });
}

} // namespace

Result<LinkArchive> parse_link_archive(const ByteSource& src, std::uint64_t base, std::uint64_t limit, bool exact) {
    auto p = probe(src, base, limit, exact, true);
    if (!p.ok) {
        return fail("not a link archive: " + p.why);
    }
    return std::move(p.archive);
}

bool is_link_archive(const ByteSource& src, std::uint64_t base, std::uint64_t limit, bool exact) {
    return probe(src, base, limit, exact, false).ok;
}

Result<std::vector<OuterMember>> segment_linkdata(const ByteSource& src) {
    const std::uint64_t size = src.size();
    if (size % kSector != 0) {
        return fail("container size " + std::to_string(size) + " is not a multiple of 2048");
    }
    const auto total = size / kSector;
    if (total > 0xFFFFFFFFull) {
        return fail("container too large");
    }
    std::vector<OuterMember> out;
    std::uint64_t s = 0;
    std::uint64_t region_start = 0;
    bool in_region = false;
    const auto close_region = [&](std::uint64_t end) {
        if (in_region) {
            const auto n = end - region_start;
            out.push_back({MemberKind::Region, static_cast<std::uint32_t>(region_start), static_cast<std::uint32_t>(n),
                           n * kSector, 0});
            in_region = false;
        }
    };
    while (s < total) {
        const auto base = s * kSector;
        auto p = probe(src, base, size - base, false, false);
        if (!p.ok) {
            if (!in_region) {
                in_region = true;
                region_start = s;
            }
            ++s;
            continue;
        }
        close_region(s);
        const auto used = p.archive.total_size;
        const auto sectors = (used + kSector - 1) / kSector;
        auto zero = all_zero(src, base + used, sectors * kSector - used);
        if (!zero) {
            return std::unexpected(zero.error());
        }
        if (!*zero) {
            return fail("link archive at sector " + std::to_string(s) + ": padding after offsets[count] (" +
                        std::to_string(used) + ") is not zero");
        }
        std::array<std::uint8_t, 4> c{};
        if (auto r = src.read(base, c); !r) {
            return std::unexpected(r.error());
        }
        out.push_back({MemberKind::LinkArchive, static_cast<std::uint32_t>(s), static_cast<std::uint32_t>(sectors), used,
                       *u32le(c, 0)});
        s += sectors;
    }
    close_region(total);
    return out;
}

namespace {

std::string padded(std::uint64_t v, int width, bool hex) {
    char buf[32];
    std::snprintf(buf, sizeof buf, hex ? "%0*llX" : "%0*llu", width, static_cast<unsigned long long>(v));
    return buf;
}

class Writer {
public:
    Writer(std::filesystem::path root, const ArchiveExtractOptions& opt) : root_(std::move(root)), opt_(opt) {}

    Result<void> write_file(const std::string& rel, Bytes data) {
        auto dest = safe_join(root_, rel);
        if (!dest) {
            return std::unexpected(dest.error());
        }
        std::error_code ec;
        std::filesystem::create_directories(dest->parent_path(), ec);
        if (ec) {
            return fail("cannot create '" + display_path(dest->parent_path()) + "': " + ec.message());
        }
        std::ofstream out(*dest, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        out.close();
        if (!out) {
            return fail("cannot write '" + dest->string() + "'");
        }
        const auto head = data.first(std::min<std::size_t>(data.size(), kMagicProbeBytes));
        files.push_back({rel, data.size(), identify(head).id});
        return {};
    }

    // Writes one blob: expanded into `stem/` if it is an exact link archive, else `stem.<ext>`.
    Result<void> write_blob(const std::string& stem, Bytes data, std::size_t depth, std::string_view forced_ext = {}) {
        const SpanSource mem(data);
        if (forced_ext.empty() && depth < opt_.max_depth && is_link_archive(mem, 0, data.size(), true)) {
            auto arc = parse_link_archive(mem, 0, data.size(), true);
            if (!arc) {
                return std::unexpected(arc.error());
            }
            for (std::size_t i = 0; i < arc->entries.size(); ++i) {
                const auto& e = arc->entries[i];
                const auto child = data.subspan(static_cast<std::size_t>(e.offset), static_cast<std::size_t>(e.size));
                if (auto r = write_blob(stem + "/" + padded(i, 4, false), child, depth + 1); !r) {
                    return r;
                }
            }
            return {};
        }
        const auto head = data.first(std::min<std::size_t>(data.size(), kMagicProbeBytes));
        const std::string ext = forced_ext.empty() ? identify(head).extension : std::string(forced_ext);
        return write_file(stem + "." + ext, data);
    }

    Result<void> write_index() {
        std::ranges::sort(files, {}, &ExtractedFile::path);
        std::string s = "# path\tsize\tkind\n";
        for (const auto& f : files) {
            s += f.path + "\t" + std::to_string(f.size) + "\t" + f.kind + "\n";
        }
        const auto p = root_ / "index.tsv";
        std::ofstream out(p, std::ios::binary | std::ios::trunc);
        out << s;
        out.close();
        if (!out) {
            return fail("cannot write '" + display_path(p) + "'");
        }
        return {};
    }

    std::vector<ExtractedFile> files;

private:
    std::filesystem::path root_;
    const ArchiveExtractOptions& opt_;
};

Result<std::vector<std::uint8_t>> read_member(const ByteSource& src, std::uint64_t off, std::uint64_t len,
                                              const ArchiveExtractOptions& opt) {
    if (len > opt.max_member_bytes) {
        return fail("member at offset " + std::to_string(off) + " is " + std::to_string(len) +
                    " bytes, above the extraction limit");
    }
    return src.read_vec(off, static_cast<std::size_t>(len));
}

} // namespace

Result<std::vector<ExtractedFile>> extract_link_container(const ByteSource& src, const std::filesystem::path& out_dir,
                                                         const ArchiveExtractOptions& opt) {
    Writer w(out_dir, opt);
    if (is_link_archive(src, 0, src.size(), true)) {
        auto data = read_member(src, 0, src.size(), opt);
        if (!data) {
            return std::unexpected(data.error());
        }
        auto arc = parse_link_archive(src, 0, src.size(), true);
        if (!arc) {
            return std::unexpected(arc.error());
        }
        const Bytes all(*data);
        for (std::size_t i = 0; i < arc->entries.size(); ++i) {
            const auto& e = arc->entries[i];
            if (auto r = w.write_blob(padded(i, 4, false),
                                      all.subspan(static_cast<std::size_t>(e.offset), static_cast<std::size_t>(e.size)), 1);
                !r) {
                return std::unexpected(r.error());
            }
        }
    } else {
        auto members = segment_linkdata(src);
        if (!members) {
            return std::unexpected(members.error());
        }
        for (std::size_t i = 0; i < members->size(); ++i) {
            const auto& m = (*members)[i];
            auto data = read_member(src, static_cast<std::uint64_t>(m.sector) * kSector, m.size, opt);
            if (!data) {
                return std::unexpected(data.error());
            }
            const auto stem = padded(i, 5, false) + "_s" + padded(m.sector, 5, true);
            auto r = m.kind == MemberKind::Region ? w.write_blob(stem, *data, 0, "region.bin")
                                                  : w.write_blob(stem, *data, 0);
            if (!r) {
                return std::unexpected(r.error());
            }
        }
    }
    if (auto r = w.write_index(); !r) {
        return std::unexpected(r.error());
    }
    return std::move(w.files);
}

} // namespace k2disc
