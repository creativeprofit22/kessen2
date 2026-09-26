// SPDX-License-Identifier: GPL-3.0-or-later
#include "irx.hpp"
#include "test_support.hpp"

#include <string>

using namespace k2test;

namespace {

// Minimal synthetic IOP module: ELF header, optional section headers (null + .iopmod)
// or a program header of type 0x70000080 pointing at the .iopmod block.
Buf make_irx(std::string_view name, std::uint16_t version, bool use_sections = true) {
    Buf e(0x60, 0);
    put_str(e, 0, "\x7F" "ELF");
    e[4] = 1; // ELFCLASS32
    e[5] = 1; // little endian
    e[6] = 1;
    put16(e, 16, 0xFF80);
    put16(e, 18, 8);
    put32(e, 20, 1);
    const std::size_t iopmod = 0x60;
    put32(e, iopmod + 0, 0x1234);    // moduleinfo
    put32(e, iopmod + 4, 0x100);     // entry
    put32(e, iopmod + 8, 0x8000);    // gp
    put32(e, iopmod + 12, 0x400);    // text
    put32(e, iopmod + 16, 0x80);     // data
    put32(e, iopmod + 20, 0x20);     // bss
    put16(e, iopmod + 24, version);
    put_str(e, iopmod + 26, name);
    const auto iopmod_size = static_cast<std::uint32_t>(26 + name.size() + 1);
    put8(e, iopmod + iopmod_size - 1, 0);
    if (use_sections) {
        const std::size_t shoff = 0x100;
        put32(e, 32, shoff);
        put16(e, 46, 40);
        put16(e, 48, 2);
        put32(e, shoff + 40 + 4, 0x70000080);
        put32(e, shoff + 40 + 16, iopmod);
        put32(e, shoff + 40 + 20, iopmod_size);
        ensure(e, shoff + 2 * 40);
    } else {
        const std::size_t phoff = 0x34;
        put32(e, 28, phoff);
        put16(e, 42, 32);
        put16(e, 44, 1);
        put32(e, phoff + 0, 0x70000080);
        put32(e, phoff + 4, iopmod);
        put32(e, phoff + 16, iopmod_size);
    }
    return e;
}

void put_romdir_entry(Buf& b, std::size_t idx, std::string_view name, std::uint32_t size) {
    put_str(b, idx * 16, name);
    put32(b, idx * 16 + 12, size);
}

} // namespace

int main() {
    return run({
        {"iopmod via section headers", [] {
             const auto e = make_irx("padman", 0x0102);
             const auto i = k2disc::parse_irx(e);
             check(i && i->has_iopmod && i->name == "padman", "name");
             check(i && i->version_string() == "1.02" && i->elf_type == 0xFF80, "version/type");
             check(i && i->text_size == 0x400 && i->data_size == 0x80 && i->bss_size == 0x20, "sizes");
         }},
        {"iopmod via program headers", [] {
             const auto i = k2disc::parse_irx(make_irx("libsd", 0x0104, false));
             check(i && i->name == "libsd" && i->version_string() == "1.04", "phdr path");
         }},
        {"ELF without iopmod", [] {
             auto e = make_irx("x", 0x0101);
             put32(e, 0x100 + 40 + 4, 1); // retype the section
             const auto i = k2disc::parse_irx(e);
             check(i && !i->has_iopmod, "no iopmod");
         }},
        {"malformed inputs rejected", [] {
             check(!k2disc::parse_irx(bytes_of("MZ\x90\x00 not an elf")).has_value(), "bad magic");
             auto e = make_irx("x", 0x0101);
             put32(e, 0x100 + 40 + 16, 0xFFFFFF00); // iopmod offset past end
             check(!k2disc::parse_irx(e).has_value(), "section out of range");
             auto f = make_irx("x", 0x0101);
             put16(f, 48, 0x7FFF); // absurd section count
             check(!k2disc::parse_irx(f).has_value(), "section table out of range");
             auto g = make_irx("x", 0x0101);
             put16(g, 18, 3); // x86
             check(!k2disc::parse_irx(g).has_value(), "not MIPS");
         }},
        {"ROMDIR image members are listed and described", [] {
             const auto mod = make_irx("cdvdman", 0x0201);
             Buf img;
             put_romdir_entry(img, 0, "RESET", 0);
             put_romdir_entry(img, 1, "ROMDIR", 6 * 16); // 5 entries + terminator
             put_romdir_entry(img, 2, "EXTINFO", 0);
             put_romdir_entry(img, 3, "CDVDMAN", static_cast<std::uint32_t>(mod.size()));
             put_romdir_entry(img, 4, "IOPBTCONF", 3);
             // data: the ROMDIR table itself occupies [0, 96) incl. zero terminator; CDVDMAN follows
             const std::size_t mod_off = 96;
             img.resize(mod_off);
             img.insert(img.end(), mod.begin(), mod.end());
             img.resize((img.size() + 15) & ~std::size_t{15});
             const std::size_t conf_off = img.size();
             put_str(img, conf_off, "abc");
             const auto dir = k2disc::parse_romdir(img);
             if (!check(dir.has_value(), "romdir parses")) {
                 return;
             }
             check(dir->size() == 5 && (*dir)[3].name == "CDVDMAN" && (*dir)[3].offset == mod_off, "offsets");
             check((*dir)[4].offset == conf_off && (*dir)[4].size == 3, "alignment");
             const auto rows = k2disc::describe_iop_file("MODULES/IOPRP.IMG", img);
             check(rows.size() == 2, "RESET/ROMDIR/EXTINFO skipped");
             check(rows.size() == 2 && rows[0].path == "MODULES/IOPRP.IMG:CDVDMAN" && rows[0].module == "cdvdman" &&
                       rows[0].version == "2.01",
                   "member module");
             check(rows.size() == 2 && rows[1].note == "not ELF", "non-ELF member noted");
         }},
        {"ROMDIR overrun and missing terminator rejected", [] {
             Buf img;
             put_romdir_entry(img, 0, "RESET", 0);
             put_romdir_entry(img, 1, "BIG", 1000000);
             ensure(img, 64);
             check(!k2disc::parse_romdir(img).has_value(), "size past end");
             Buf nt;
             put_romdir_entry(nt, 0, "RESET", 0);
             put_romdir_entry(nt, 1, "A", 0);
             check(!k2disc::parse_romdir(nt).has_value(), "no terminator");
         }},
        {"describe_iop_file on loose IRX and others", [] {
             const auto rows = k2disc::describe_iop_file("MODULES/PADMAN.irx", make_irx("padman", 0x0101));
             check(rows.size() == 1 && rows[0].module == "padman" && rows[0].version == "1.01", "loose irx");
             check(k2disc::describe_iop_file("DATA.BIN", bytes_of("xyz")).empty(), "unrelated file");
         }},
    });
}
