// SPDX-License-Identifier: GPL-3.0-or-later
#include "crc32.hpp"

#include <array>

namespace k2disc {
namespace {

constexpr std::array<std::uint32_t, 256> make_table() {
    std::array<std::uint32_t, 256> t{};
    for (std::uint32_t i = 0; i < 256; ++i) {
        std::uint32_t c = i;
        for (int k = 0; k < 8; ++k) {
            c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        }
        t[i] = c;
    }
    return t;
}

constexpr auto kTable = make_table();

} // namespace

void Crc32::update(std::span<const std::uint8_t> data) noexcept {
    std::uint32_t c = state_;
    for (const std::uint8_t b : data) {
        c = kTable[(c ^ b) & 0xFFu] ^ (c >> 8);
    }
    state_ = c;
}

std::uint32_t crc32(std::span<const std::uint8_t> data) noexcept {
    Crc32 c;
    c.update(data);
    return c.value();
}

} // namespace k2disc
