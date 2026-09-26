// SPDX-License-Identifier: GPL-3.0-or-later
// LINKDATA container parser tests. Every fixture is synthesised here (never real game data).
#include "archive_linkdata.hpp"
#include "test_support.hpp"
#include "tree.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace k2test;
using k2disc::MemorySource;
namespace fs = std::filesystem;

namespace {

// Builds a link archive: count, offsets[count+1], zero pad to 16, entries back-to-back.
Buf link(const std::vector<Buf>& entries) {
    const auto count = static_cast<std::uint32_t>(entries.size());
    const auto hdr = static_cast<std::uint32_t>(k2disc::link_header_size(count));
    Buf b(hdr, 0);
    put32(b, 0, count);
    std::uint32_t off = hdr;
    for (std::uint32_t i = 0; i < count; ++i) {
        put32(b, 4 + 4 * i, off);
        off += static_cast<std::uint32_t>(entries[i].size());
    }
    put32(b, 4 + 4 * count, off);
    for (const auto& e : entries) {
        b.insert(b.end(), e.begin(), e.end());
    }
    return b;
}

Buf pad_sector(Buf b) {
    b.resize((b.size() + 2047) / 2048 * 2048, 0);
    return b;
}

Buf concat(const std::vector<Buf>& parts) {
    Buf out;
    for (const auto& p : parts) {
        out.insert(out.end(), p.begin(), p.end());
    }
    return out;
}

std::string slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

bool parse_fails(const Buf& b, bool exact, std::string_view needle) {
    MemorySource src(b);
    const auto r = k2disc::parse_link_archive(src, 0, b.size(), exact);
    if (r) {
        return false;
    }
    const bool ok = r.error().message.find(needle) != std::string::npos;
    if (!ok) {
        std::fprintf(stderr, "  unexpected error: %s\n", r.error().message.c_str());
    }
    return ok;
}

} // namespace

int main() {
    return run({
        {"valid archive with header padding", [] {
             // count=3 -> header 4*(3+2)=20 -> aligned 32
             const auto b = link({bytes_of("VAGp1"), bytes_of("abc"), bytes_of("TIM2xyzw")});
             MemorySource src(b);
             const auto a = k2disc::parse_link_archive(src, 0, b.size(), true);
             if (!check(a.has_value(), "parse ok")) {
                 return;
             }
             check_eq(a->entries.size(), std::size_t{3}, "count");
             check(a->entries[0].offset == 32 && a->entries[0].size == 5, "entry 0");
             check(a->entries[1].offset == 37 && a->entries[1].size == 3, "entry 1 unaligned start");
             check(a->entries[2].offset == 40 && a->entries[2].size == 8, "entry 2");
             check_eq(a->total_size, std::uint64_t{48}, "total");
         }},
        {"header sizes match align16(4*(count+2))", [] {
             check_eq(k2disc::link_header_size(1), std::uint64_t{16}, "1");
             check_eq(k2disc::link_header_size(2), std::uint64_t{16}, "2");
             check_eq(k2disc::link_header_size(3), std::uint64_t{32}, "3");
             check_eq(k2disc::link_header_size(66), std::uint64_t{272}, "66 (0x110)");
         }},
        {"zero-length entries allowed", [] {
             const auto b = link({Buf{}, bytes_of("x"), Buf{}});
             MemorySource src(b);
             const auto a = k2disc::parse_link_archive(src, 0, b.size(), true);
             check(a && a->entries[0].size == 0 && a->entries[2].size == 0, "empty entries");
         }},
        {"zero members rejected", [] {
             Buf b(16, 0);
             put32(b, 4, 16);
             check(parse_fails(b, false, "entry count 0"), "count 0");
         }},
        {"absurd count rejected", [] {
             Buf b(16, 0);
             put32(b, 0, 0x7FFFFFFF);
             check(parse_fails(b, false, "out of range"), "huge count");
         }},
        {"truncated header rejected", [] {
             auto b = link({bytes_of("aaaa"), bytes_of("bbbb"), bytes_of("cccc")});
             b.resize(12);
             check(parse_fails(b, false, "larger than archive"), "truncated table");
             check(parse_fails(Buf{1, 0, 0}, false, "too small"), "tiny");
         }},
        {"truncated data rejected", [] {
             auto b = link({bytes_of("aaaa"), bytes_of("bbbbbbbb")});
             b.resize(b.size() - 3);
             check(parse_fails(b, false, "past archive end"), "last offset beyond data");
         }},
        {"out-of-range offset rejected", [] {
             auto b = link({bytes_of("aaaa"), bytes_of("bbbb")});
             put32(b, 8, 0xFFFFFFF0u);
             check(parse_fails(b, false, "past archive end"), "huge offset");
         }},
        {"overlapping (decreasing) offsets rejected", [] {
             auto b = link({bytes_of("aaaa"), bytes_of("bbbb"), bytes_of("cccc")});
             put32(b, 8, 18); // offsets[1] < offsets[0]=32? no: set offsets[2] below offsets[1]
             put32(b, 12, 17);
             check(parse_fails(b, false, "decreases"), "decreasing");
         }},
        {"wrong first offset rejected", [] {
             auto b = link({bytes_of("aaaa")});
             put32(b, 4, 20);
             check(parse_fails(b, false, "expected header size"), "offsets[0] != header");
         }},
        {"exact mode requires total == size", [] {
             auto b = link({bytes_of("aaaa")});
             b.push_back(0);
             check(parse_fails(b, true, "does not match"), "trailing byte in exact mode");
             MemorySource src(b);
             check(k2disc::parse_link_archive(src, 0, b.size(), false).has_value(), "ok in non-exact mode");
         }},
        {"segment: archives, regions, nesting", [] {
             const auto inner = link({bytes_of("VAGpdata"), bytes_of("TIM2tex!")});
             const auto a0 = pad_sector(link({inner, bytes_of("hello")}));
             Buf region(2 * 2048, 0xFF); // not a link archive (count 0xFFFFFFFF)
             const auto a1 = pad_sector(link({bytes_of("x")}));
             const auto image = concat({a0, region, a1});
             MemorySource src(image);
             const auto m = k2disc::segment_linkdata(src);
             if (!check(m.has_value(), "segment ok")) {
                 return;
             }
             check_eq(m->size(), std::size_t{3}, "three members");
             check((*m)[0].kind == k2disc::MemberKind::LinkArchive && (*m)[0].sector == 0 && (*m)[0].sectors == 1 &&
                       (*m)[0].entries == 2,
                   "archive 0");
             check((*m)[1].kind == k2disc::MemberKind::Region && (*m)[1].sector == 1 && (*m)[1].sectors == 2,
                   "region");
             check((*m)[2].kind == k2disc::MemberKind::LinkArchive && (*m)[2].sector == 3, "archive 1");
         }},
        {"segment rejects non-zero padding and unaligned size", [] {
             auto a = pad_sector(link({bytes_of("x")}));
             a[2047] = 1;
             MemorySource src(a);
             const auto m = k2disc::segment_linkdata(src);
             check(!m && m.error().message.find("padding") != std::string::npos, "dirty padding");
             MemorySource odd(Buf(100, 0));
             check(!k2disc::segment_linkdata(odd).has_value(), "size not multiple of 2048");
         }},
        {"extract writes nested tree and sorted index", [] {
             const fs::path out = fs::temp_directory_path() / "k2disc_test_archive";
             fs::remove_all(out);
             const auto inner = link({bytes_of("VAGpdata"), bytes_of("plain text")});
             const auto a0 = pad_sector(link({inner, bytes_of("TIM2tex!")}));
             Buf region(2048, 0xFF);
             MemorySource src(concat({a0, region}));
             const auto files = k2disc::extract_link_container(src, out);
             if (!check(files.has_value(), "extract ok")) {
                 std::fprintf(stderr, "  %s\n", files.error().message.c_str());
                 return;
             }
             check_eq(files->size(), std::size_t{4}, "4 leaves");
             check_eq(slurp(out / "00000_s00000" / "0000" / "0000.vag"), std::string("VAGpdata"), "nested leaf");
             check_eq(slurp(out / "00000_s00000" / "0000" / "0001.txt"), std::string("plain text"), "text leaf");
             check_eq(slurp(out / "00000_s00000" / "0001.tm2"), std::string("TIM2tex!"), "leaf ext by magic");
             check(fs::file_size(out / "00001_s00001.region.bin") == 2048, "region written");
             const auto idx = slurp(out / "index.tsv");
             check(idx.starts_with("# path\tsize\tkind\n00000_s00000/0000/0000.vag\t8\tvag\n"), "index sorted");
             fs::remove_all(out);
         }},
        {"scan tree of an extraction excludes root metadata only", [] {
             const fs::path out = fs::temp_directory_path() / "k2disc_test_archive_tree";
             fs::remove_all(out);
             const auto inner = link({bytes_of("VAGpdata"), bytes_of("plain text")});
             const auto a0 = pad_sector(link({inner, bytes_of("TIM2tex!")}));
             MemorySource src(concat({a0, Buf(2048, 0xFF)}));
             const auto files = k2disc::extract_link_container(src, out);
             if (!check(files.has_value(), "extract ok")) {
                 return;
             }
             check(fs::exists(out / "index.tsv"), "index written into the scanned dir");
             { std::ofstream(out / "manifest.tsv") << "x"; }
             { std::ofstream(out / "00000_s00000" / "index.tsv") << "real"; } // not at root: kept

             const auto all = k2disc::open_tree(out);
             const auto tree = k2disc::open_tree(out, k2disc::kToolMetadataFiles);
             if (!check(all && tree, "open_tree ok")) {
                 return;
             }
             check_eq((*all)->files().size(), files->size() + 3, "unfiltered sees metadata");
             check_eq((*tree)->files().size(), files->size() + 1, "members plus nested index.tsv");
             bool nested = false;
             for (const auto& f : (*tree)->files()) {
                 check(f.path != "index.tsv" && f.path != "manifest.tsv", "root metadata excluded");
                 nested = nested || f.path == "00000_s00000/index.tsv";
             }
             check(nested, "subdirectory index.tsv kept");

             fs::remove(out / "00000_s00000" / "index.tsv");
             const auto exact = k2disc::open_tree(out, k2disc::kToolMetadataFiles);
             check(exact && (*exact)->files().size() == files->size(), "count equals extracted members");
             fs::remove_all(out);
         }},
        {"extract of a single (exact) link archive file", [] {
             const fs::path out = fs::temp_directory_path() / "k2disc_test_archive_single";
             fs::remove_all(out);
             MemorySource src(link({bytes_of("aa"), bytes_of("bb")}));
             const auto files = k2disc::extract_link_container(src, out);
             check(files && files->size() == 2, "two entries");
             check(fs::exists(out / "0000.bin") || fs::exists(out / "0000.txt"), "entry 0 written");
             fs::remove_all(out);
         }},
        {"nesting depth is bounded", [] {
             Buf b = bytes_of("leaf");
             for (int i = 0; i < 12; ++i) {
                 b = link({b});
             }
             const fs::path out = fs::temp_directory_path() / "k2disc_test_archive_deep";
             fs::remove_all(out);
             MemorySource src(b);
             const auto files = k2disc::extract_link_container(src, out);
             check(files && files->size() == 1, "stops recursing, writes one blob");
             fs::remove_all(out);
         }},
    });
}
