// SPDX-License-Identifier: GPL-3.0-or-later
// PS2 SYSTEM.CNF parser: BOOT2 = cdrom0:\SLUS_203.61;1 -> boot ELF path + serial.
#pragma once

#include "error.hpp"

#include <string>
#include <string_view>

namespace k2disc {

struct SystemCnf {
    std::string boot_path;  // disc path of the boot ELF, '/'-separated, version stripped ("SLUS_203.61")
    std::string boot_name;  // last component ("SLUS_203.61")
    std::string serial;     // "SLUS-20361"; empty if the name does not follow the XXXX_nnn.nn pattern
    std::string version;    // VER value, may be empty
    std::string video_mode; // VMODE value, may be empty
};

[[nodiscard]] Result<SystemCnf> parse_system_cnf(std::string_view text);

// "SLUS_203.61" -> "SLUS-20361"; empty if not of the form AAAA_nnn.nn.
[[nodiscard]] std::string serial_from_boot_name(std::string_view name);

} // namespace k2disc
