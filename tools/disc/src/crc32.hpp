// SPDX-License-Identifier: GPL-3.0-or-later
// Standard reflected CRC-32 (poly 0xEDB88320, init/xorout 0xFFFFFFFF), identical to
// PS2Recomp's computeFileCrc32 used by PS2_REGISTER_GAME_OVERRIDE.
#pragma once

#include <cstdint>
#include <span>

namespace k2disc {

class Crc32 {
public:
    void update(std::span<const std::uint8_t> data) noexcept;
    [[nodiscard]] std::uint32_t value() const noexcept { return state_ ^ 0xFFFFFFFFu; }

private:
    std::uint32_t state_ = 0xFFFFFFFFu;
};

[[nodiscard]] std::uint32_t crc32(std::span<const std::uint8_t> data) noexcept;

} // namespace k2disc
