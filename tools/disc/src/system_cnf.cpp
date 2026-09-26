// SPDX-License-Identifier: GPL-3.0-or-later
#include "system_cnf.hpp"

#include "paths.hpp"

#include <algorithm>
#include <cctype>

namespace k2disc {
namespace {

std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
        s.remove_prefix(1);
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.remove_suffix(1);
    }
    return s;
}

bool iequals(std::string_view a, std::string_view b) {
    return std::ranges::equal(a, b, [](char x, char y) {
        return std::toupper(static_cast<unsigned char>(x)) == std::toupper(static_cast<unsigned char>(y));
    });
}

} // namespace

std::string serial_from_boot_name(std::string_view n) {
    // AAAA_nnn.nn
    if (n.size() != 11 || n[4] != '_' || n[8] != '.') {
        return {};
    }
    for (std::size_t i = 0; i < 4; ++i) {
        if (!std::isalpha(static_cast<unsigned char>(n[i]))) {
            return {};
        }
    }
    for (const std::size_t i : {5u, 6u, 7u, 9u, 10u}) {
        if (!std::isdigit(static_cast<unsigned char>(n[i]))) {
            return {};
        }
    }
    std::string s(n.substr(0, 4));
    std::ranges::transform(s, s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s + "-" + std::string(n.substr(5, 3)) + std::string(n.substr(9, 2));
}

Result<SystemCnf> parse_system_cnf(std::string_view text) {
    if (text.size() > 64 * 1024) {
        return fail("SYSTEM.CNF larger than 64 KiB");
    }
    SystemCnf cnf;
    std::string boot2;
    bool have_boot2 = false;
    std::size_t start = 0;
    while (start < text.size()) {
        auto end = text.find('\n', start);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        const auto line = trim(text.substr(start, end - start));
        start = end + 1;
        const auto eq = line.find('=');
        if (line.empty() || eq == std::string_view::npos) {
            continue;
        }
        const auto key = trim(line.substr(0, eq));
        const auto value = trim(line.substr(eq + 1));
        if (iequals(key, "BOOT2")) {
            boot2 = std::string(value);
            have_boot2 = true;
        } else if (iequals(key, "VER")) {
            cnf.version = std::string(value);
        } else if (iequals(key, "VMODE")) {
            cnf.video_mode = std::string(value);
        }
    }
    if (!have_boot2) {
        return fail("SYSTEM.CNF has no BOOT2 line (not a PS2 game disc?)");
    }
    constexpr std::string_view kDevice = "cdrom0:";
    if (boot2.size() < kDevice.size() || !iequals(std::string_view(boot2).substr(0, kDevice.size()), kDevice)) {
        return fail("BOOT2 '" + boot2 + "' does not start with cdrom0:");
    }
    std::string path = boot2.substr(kDevice.size());
    if (const auto semi = path.rfind(';'); semi != std::string::npos) {
        path.resize(semi);
    }
    std::ranges::replace(path, '\\', '/');
    while (!path.empty() && path.front() == '/') {
        path.erase(path.begin());
    }
    auto clean = sanitize_relative(path);
    if (!clean) {
        return fail("BOOT2 path '" + boot2 + "': " + clean.error().message);
    }
    cnf.boot_path = *clean;
    const auto slash = cnf.boot_path.rfind('/');
    cnf.boot_name = slash == std::string::npos ? cnf.boot_path : cnf.boot_path.substr(slash + 1);
    cnf.serial = serial_from_boot_name(cnf.boot_name);
    return cnf;
}

} // namespace k2disc
