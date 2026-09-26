// SPDX-License-Identifier: GPL-3.0-or-later
#include "paths.hpp"

#include <algorithm>
#include <array>
#include <cctype>

namespace k2disc {
namespace {

bool is_reserved_device(std::string_view name) {
    // Compare the stem (before the first '.') case-insensitively.
    const auto dot = name.find('.');
    std::string stem(name.substr(0, dot));
    std::ranges::transform(stem, stem.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    constexpr std::array<std::string_view, 6> fixed{"CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$"};
    if (std::ranges::find(fixed, stem) != fixed.end()) {
        return true;
    }
    return stem.size() == 4 && (stem.starts_with("COM") || stem.starts_with("LPT")) && stem[3] >= '0' &&
           stem[3] <= '9';
}

} // namespace

Result<std::string> sanitize_component(std::string_view name) {
    if (name.empty() || name == "." || name == "..") {
        return fail("invalid path component '" + std::string(name) + "'");
    }
    if (name.size() > 255) {
        return fail("path component too long");
    }
    for (const char ch : name) {
        const auto c = static_cast<unsigned char>(ch);
        if (c < 0x20 || c == 0x7F || ch == '/' || ch == '\\' || ch == ':' || ch == '*' || ch == '?' || ch == '"' ||
            ch == '<' || ch == '>' || ch == '|') {
            return fail("forbidden character in path component '" + std::string(name) + "'");
        }
    }
    if (name.back() == '.' || name.back() == ' ') {
        return fail("path component ends with '.' or space: '" + std::string(name) + "'");
    }
    if (is_reserved_device(name)) {
        return fail("reserved device name '" + std::string(name) + "'");
    }
    return std::string(name);
}

Result<std::string> sanitize_relative(std::string_view rel) {
    if (rel.empty()) {
        return fail("empty relative path");
    }
    std::string out;
    std::size_t start = 0;
    while (start <= rel.size()) {
        const auto end = std::min(rel.find('/', start), rel.size());
        auto comp = sanitize_component(rel.substr(start, end - start));
        if (!comp) {
            return std::unexpected(comp.error());
        }
        if (!out.empty()) {
            out += '/';
        }
        out += *comp;
        start = end + 1;
    }
    return out;
}

std::string display_path(const std::filesystem::path& p) {
    const auto u8 = p.u8string();
    return {u8.begin(), u8.end()};
}

Result<std::filesystem::path> safe_join(const std::filesystem::path& root, std::string_view rel) {
    auto clean = sanitize_relative(rel);
    if (!clean) {
        return std::unexpected(clean.error());
    }
    const auto base = root.lexically_normal();
    auto joined = base;
    std::size_t start = 0;
    while (start <= clean->size()) {
        const auto end = std::min(clean->find('/', start), clean->size());
        const auto part = std::string_view(*clean).substr(start, end - start);
        joined /= std::filesystem::path(std::u8string(part.begin(), part.end()));
        start = end + 1;
    }
    joined = joined.lexically_normal();
    const auto rel_check = joined.lexically_relative(base);
    if (rel_check.empty() || *rel_check.begin() == "..") {
        return fail("path escapes output root: '" + *clean + "'");
    }
    return joined;
}

} // namespace k2disc
