// SPDX-License-Identifier: GPL-3.0-or-later
#include "system_cnf.hpp"
#include "test_support.hpp"

#include <string>

using namespace k2test;
using k2disc::parse_system_cnf;

int main() {
    return run({
        {"SLUS with CRLF", [] {
             const auto c = parse_system_cnf("BOOT2 = cdrom0:\\SLUS_203.61;1\r\nVER = 1.00\r\nVMODE = NTSC\r\n");
             check(c.has_value(), "parse ok");
             check(c && c->boot_path == "SLUS_203.61" && c->boot_name == "SLUS_203.61", "boot path");
             check(c && c->serial == "SLUS-20361", "serial");
             check(c && c->version == "1.00" && c->video_mode == "NTSC", "VER/VMODE");
         }},
        {"SLPM, LF only, no spaces, lowercase key", [] {
             const auto c = parse_system_cnf("boot2=cdrom0:\\SLPM_650.61;1\nver=1.01\n");
             check(c && c->serial == "SLPM-65061" && c->version == "1.01", "SLPM");
         }},
        {"SLES in subdirectory, no version suffix", [] {
             const auto c = parse_system_cnf("BOOT2 = cdrom0:\\GAME\\SLES_512.34\n");
             check(c && c->boot_path == "GAME/SLES_512.34" && c->boot_name == "SLES_512.34", "subdir path");
             check(c && c->serial == "SLES-51234", "serial");
         }},
        {"non-standard boot name has empty serial", [] {
             const auto c = parse_system_cnf("BOOT2 = cdrom0:\\MAIN.ELF;1\n");
             check(c && c->boot_name == "MAIN.ELF" && c->serial.empty(), "no serial");
         }},
        {"missing BOOT2 rejected", [] {
             const auto c = parse_system_cnf("BOOT = cdrom0:\\SLUS_203.61;1\nVER = 1.00\n");
             check(!c && c.error().message.find("BOOT2") != std::string::npos, "PS1-style BOOT only");
             check(!parse_system_cnf("").has_value(), "empty");
         }},
        {"wrong device or traversal rejected", [] {
             check(!parse_system_cnf("BOOT2 = host0:\\X;1\n").has_value(), "host0");
             check(!parse_system_cnf("BOOT2 = cdrom0:\\..\\X;1\n").has_value(), "traversal");
             check(!parse_system_cnf("BOOT2 = cdrom0:\n").has_value(), "empty path");
         }},
    });
}
