// SPDX-License-Identifier: GPL-3.0-or-later
#include "bytes.hpp"
#include "disc.hpp"
#include "crc32.hpp"
#include "iso_builder.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace k2test;
namespace fs = std::filesystem;

namespace {

std::string slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

} // namespace

int main() {
    return run({
        {"inspect finds boot ELF, serial and CRC", [] {
             k2disc::MemorySource src(sample_iso());
             const auto info = k2disc::inspect_disc(src);
             if (!check(info.has_value(), "inspect ok")) {
                 return;
             }
             check_eq(info->cnf.serial, std::string("SLUS-12345"), "serial");
             check_eq(info->label, std::string("SLUS-12345"), "label");
             check_eq(info->boot.path, std::string("SLUS_123.45"), "boot path");
             const auto elf = bytes_of("\x7F" "ELFfakeelf!!");
             check_eq(elf.size(), std::size_t{13}, "fixture length");
             check_eq(info->boot_crc32, k2disc::crc32(elf), "CRC of whole boot file");
         }},
        {"inspect fails when boot ELF missing", [] {
             k2disc::MemorySource src(sample_iso("BOOT2 = cdrom0:\\SLUS_999.99;1\n"));
             const auto info = k2disc::inspect_disc(src);
             check(!info && info.error().message.find("not on the disc") != std::string::npos, "missing boot");
         }},
        {"extract writes tree and sorted manifest", [] {
             const fs::path out = fs::temp_directory_path() / "k2disc_test_extract";
             fs::remove_all(out);
             k2disc::MemorySource src(sample_iso());
             const auto info = k2disc::inspect_disc(src);
             if (!check(info.has_value(), "inspect ok")) {
                 return;
             }
             const auto sum = k2disc::extract_disc(src, info->volume, out);
             if (!check(sum.has_value(), "extract ok")) {
                 return;
             }
             check(sum->files == 4 && sum->dirs == 1, "counts");
             check_eq(slurp(out / "iso" / "DATA" / "FILE.BIN"), std::string("hello"), "file content");
             check_eq(slurp(out / "iso" / "DATA" / "README"), std::string("abc"), "renamed file content");
             const auto m = slurp(out / "manifest.tsv");
             const auto hello = k2disc::hex32(k2disc::crc32(bytes_of("hello")));
             check(m.starts_with("# path\tlba\tsize\tcrc32\nDATA/FILE.BIN\t26\t5\t" + hello + "\n"),
                   "manifest sorted with crc");
             check(m.find("SYSTEM.CNF") > m.find("SLUS_123.45"), "manifest order");
             fs::remove_all(out);
         }},
    });
}
