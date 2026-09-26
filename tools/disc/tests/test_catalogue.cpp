// SPDX-License-Identifier: GPL-3.0-or-later
#include "catalogue.hpp"
#include "test_support.hpp"

#include <string>

using namespace k2test;
using k2disc::identify;

namespace {

std::string id_of(const Buf& b) {
    return identify(b).id;
}

} // namespace

int main() {
    return run({
        {"magic table", [] {
             struct Row {
                 Buf head;
                 std::string id;
             };
             Buf irx = bytes_of("\x7F" "ELF");
             put16(irx, 16, 0xFF80);
             Buf elf = bytes_of("\x7F" "ELF");
             put16(elf, 16, 2);
             Buf sshd = bytes_of("IECSsreV");
             put_str(sshd, 16, "IECSdaeH");
             Buf sq = bytes_of("IECSsreV");
             put_str(sq, 16, "IECSuqeS");
             const std::vector<Row> rows{
                 {irx, "elf-irx"},
                 {elf, "elf"},
                 {sshd, "sony-sshd"},
                 {sq, "sony-sq"},
                 {bytes_of("IECSsreV"), "sony-scei"},
                 {bytes_of("VAGp\0\0\0\x20"), "vag"},
                 {bytes_of("TIM2\x04\x00"), "tim2"},
                 {Buf{0x00, 0x00, 0x01, 0xBA, 0x44}, "mpeg-ps"},
                 {bytes_of("RESET\0\0\0"), "iop-romdir"},
                 {bytes_of("BOOT2 = cdrom0:\\X;1\r\n"), "system-cnf"},
                 {bytes_of("plain text file\r\n"), "text"},
                 {bytes_of("TMD20060"), "koei-tmd2"},
                 {bytes_of("FCVQ0100"), "koei-fcvq"},
                 {Buf(32, 0), "zeros"},
                 {Buf{}, "empty"},
             };
             for (const auto& r : rows) {
                 check_eq(id_of(r.head), r.id, r.id);
             }
         }},
        {"NUL-leading magic does not match everything", [] {
             check(id_of(Buf{0x00, 0x00, 0x00, 0x01, 0x42}).starts_with("unknown:"), "not mpeg");
             check_eq(id_of(Buf{0x1C, 0x00, 0x32, 0xDB}), std::string("unknown:1C0032DB"), "unknown id is first u32 BE");
             check_eq(id_of(Buf{0xAB}), std::string("unknown:AB"), "short unknown");
         }},
        {"extension_of", [] {
             check_eq(k2disc::extension_of("DIR/LINKDATA.ans"), std::string("ANS"), "upper");
             check_eq(k2disc::extension_of("SLUS_202.75"), std::string("75"), "serial-style");
             check_eq(k2disc::extension_of("A.DIR/README"), std::string(""), "dot in dir only");
             check_eq(k2disc::extension_of("X.IMG:LOADCORE"), std::string(""), "romdir member");
         }},
        {"aggregation is sorted and deterministic", [] {
             k2disc::Catalogue c;
             const auto vag = identify(bytes_of("VAGp...."));
             const auto txt = identify(bytes_of("hello world text"));
             c.add("B/2.VAG", 10, vag);
             c.add("A/1.VAG", 20, vag);
             c.add("Z/4.VAG", 5, vag);
             c.add("C/3.VAG", 1, vag);
             c.add("README.TXT", 7, txt);
             const auto rows = c.rows();
             check(rows.size() == 2 && rows[0].extension == "TXT" && rows[1].extension == "VAG", "sorted by ext");
             check(rows.size() == 2 && rows[1].count == 4 && rows[1].total_bytes == 36, "totals");
             check(rows.size() == 2 && rows[1].samples == std::vector<std::string>{"A/1.VAG", "B/2.VAG", "C/3.VAG"},
                   "first three samples sorted");
             check(c.total_files() == 5 && c.total_bytes() == 43, "grand total");
             const auto md = k2disc::render_markdown(c);
             check(md.find("| `VAG` | `vag` | 4 | 36 |") != std::string::npos, "markdown row");
             check(k2disc::render_tsv(c).find("VAG\tvag\t4\t36\t") != std::string::npos, "tsv row");
         }},
    });
}
