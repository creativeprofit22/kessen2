// SPDX-License-Identifier: GPL-3.0-or-later
#include "iso9660.hpp"
#include "iso_builder.hpp"
#include "paths.hpp"

#include <string>

using namespace k2test;
using k2disc::MemorySource;
using k2disc::read_iso9660;

namespace {

bool fails_with(const Buf& img, std::string_view needle) {
    MemorySource src(img);
    const auto v = read_iso9660(src);
    if (v) {
        return false;
    }
    const bool ok = v.error().message.find(needle) != std::string::npos;
    if (!ok) {
        std::fprintf(stderr, "  unexpected error: %s\n", v.error().message.c_str());
    }
    return ok;
}

Buf with_root_entry(const Buf& entry) {
    IsoBuilder b;
    b.volume(20, kSec);
    b.dir(20, {dot(20, kSec), dotdot(20, kSec), entry});
    return b.img;
}

} // namespace

int main() {
    return run({
        {"reads tree, sizes and sorts by path", [] {
             MemorySource src(sample_iso());
             const auto v = read_iso9660(src);
             if (!check(v.has_value(), "parse ok")) {
                 return;
             }
             check_eq(v->volume_id, std::string("TESTVOL"), "volume id trimmed");
             check_eq(v->udf_nsr, std::string("NSR02"), "UDF detected");
             std::vector<std::string> paths;
             for (const auto& e : v->entries) {
                 paths.push_back(e.path);
             }
             check(paths == std::vector<std::string>{"DATA", "DATA/FILE.BIN", "DATA/README", "SLUS_123.45",
                                                     "SYSTEM.CNF"},
                   "sorted paths, ';1' and trailing '.' stripped");
             const auto* f = v->find("data/file.bin");
             check(f != nullptr && f->size == 5 && f->first_lba() == 26 && !f->is_dir, "case-insensitive find");
             const auto* d = v->find("DATA");
             check(d != nullptr && d->is_dir, "directory entry");
             const auto content = k2disc::read_file(src, *f, 100);
             check(content && std::string(content->begin(), content->end()) == "hello", "file content");
             check(!k2disc::read_file(src, *f, 4).has_value(), "size limit enforced");
         }},
        {"no UDF when recognition sequence absent", [] {
             MemorySource src(sample_iso("BOOT2 = cdrom0:\\X;1\n", false));
             const auto v = read_iso9660(src);
             check(v && v->udf_nsr.empty(), "no NSR");
         }},
        {"multi-extent file is concatenated", [] {
             IsoBuilder b;
             b.volume(20, kSec);
             b.dir(20, {dot(20, kSec), dotdot(20, kSec), iso_record("BIG.DAT;1", 24, kSec, 0x80),
                        iso_record("BIG.DAT;1", 26, 3, 0)});
             b.data(24, "A");
             b.data(26, "xyz");
             MemorySource src(b.img);
             const auto v = read_iso9660(src);
             if (!check(v.has_value(), "parse ok")) {
                 return;
             }
             const auto* f = v->find("BIG.DAT");
             check(f != nullptr && f->extents.size() == 2 && f->size == kSec + 3, "two extents");
             const auto c = k2disc::read_file(src, *f, 1u << 20);
             check(c && c->size() == kSec + 3 && (*c)[0] == 'A' && (*c)[kSec] == 'x', "content order");
         }},
        {"unterminated multi-extent rejected", [] {
             check(fails_with(with_root_entry(iso_record("BIG.DAT;1", 24, kSec, 0x80)), "multi-extent"),
                   "truncated multi-extent");
         }},
        {"traversal and unsafe names rejected", [] {
             check(fails_with(with_root_entry(iso_record("..;1", 24, 1, 0)), "invalid path component"), "'..'");
             check(fails_with(with_root_entry(iso_record("A/B;1", 24, 1, 0)), "forbidden character"), "slash");
             check(fails_with(with_root_entry(iso_record("A\\B;1", 24, 1, 0)), "forbidden character"), "backslash");
             check(fails_with(with_root_entry(iso_record("C:X;1", 24, 1, 0)), "forbidden character"), "drive");
             check(fails_with(with_root_entry(iso_record("CON;1", 24, 1, 0)), "reserved"), "device name");
             check(fails_with(with_root_entry(iso_record(std::string_view("A\x01" "B;1"), 24, 1, 0)),
                              "forbidden character"),
                   "control char");
         }},
        {"out-of-range extent rejected", [] {
             check(fails_with(with_root_entry(iso_record("X.BIN;1", 1000000, 10, 0)), "outside the image"),
                   "file extent past end");
             check(fails_with(with_root_entry(iso_record("SUB", 1000000, kSec, 2)), "outside the image"),
                   "dir extent past end");
         }},
        {"directory loop rejected", [] {
             check(fails_with(with_root_entry(iso_record("LOOP", 20, kSec, 2)), "loop"), "loop to root");
         }},
        {"duplicate entry rejected", [] {
             IsoBuilder b;
             b.volume(20, kSec);
             b.dir(20, {dot(20, kSec), dotdot(20, kSec), iso_record("A;1", 24, 1, 0), iso_record("A;1", 24, 1, 0)});
             check(fails_with(b.img, "duplicate"), "duplicate");
         }},
        {"bad record length rejected", [] {
             auto img = with_root_entry(iso_record("A;1", 24, 1, 0));
             img[20 * kSec + 68] = 10; // third record: length < 34
             check(fails_with(img, "bad record length"), "short record");
         }},
        {"non-ISO input rejected", [] {
             check(fails_with(Buf(40 * kSec, 0), "not an ISO9660"), "zeros");
             check(fails_with(Buf(100, 0), "not an ISO9660"), "tiny");
         }},
        {"safe_join keeps paths inside root", [] {
             const std::filesystem::path root = "out/root";
             const auto ok = k2disc::safe_join(root, "DATA/FILE.BIN");
             check(ok && *ok == std::filesystem::path("out/root/DATA/FILE.BIN").lexically_normal(), "normal join");
             check(!k2disc::safe_join(root, "../x").has_value(), "parent rejected");
             check(!k2disc::safe_join(root, "a/../../x").has_value(), "nested parent rejected");
             check(!k2disc::safe_join(root, "/abs").has_value(), "absolute rejected");
             check(!k2disc::safe_join(root, "C:/x").has_value(), "drive rejected");
             check(!k2disc::safe_join(root, "a//b").has_value(), "empty component rejected");
             check(!k2disc::safe_join(root, "").has_value(), "empty rejected");
         }},
    });
}
