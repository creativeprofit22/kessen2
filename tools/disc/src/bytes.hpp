// SPDX-License-Identifier: GPL-3.0-or-later
// Bounds-checked little/big-endian readers over untrusted byte spans.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace k2disc {

using Bytes = std::span<const std::uint8_t>;

// Sub-range [off, off+len) or nullopt if it does not fit (overflow-safe).
inline std::optional<Bytes> sub(Bytes b, std::size_t off, std::size_t len) noexcept {
    if (off > b.size() || len > b.size() - off) {
        return std::nullopt;
    }
    return b.subspan(off, len);
}

inline std::optional<std::uint8_t> u8(Bytes b, std::size_t off) noexcept {
    if (off >= b.size()) {
        return std::nullopt;
    }
    return b[off];
}

inline std::optional<std::uint16_t> u16le(Bytes b, std::size_t off) noexcept {
    const auto s = sub(b, off, 2);
    if (!s) {
        return std::nullopt;
    }
    return static_cast<std::uint16_t>((*s)[0] | ((*s)[1] << 8));
}

inline std::optional<std::uint32_t> u32le(Bytes b, std::size_t off) noexcept {
    const auto s = sub(b, off, 4);
    if (!s) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>((*s)[0]) | (static_cast<std::uint32_t>((*s)[1]) << 8) |
           (static_cast<std::uint32_t>((*s)[2]) << 16) | (static_cast<std::uint32_t>((*s)[3]) << 24);
}

inline std::optional<std::uint32_t> u32be(Bytes b, std::size_t off) noexcept {
    const auto s = sub(b, off, 4);
    if (!s) {
        return std::nullopt;
    }
    return (static_cast<std::uint32_t>((*s)[0]) << 24) | (static_cast<std::uint32_t>((*s)[1]) << 16) |
           (static_cast<std::uint32_t>((*s)[2]) << 8) | static_cast<std::uint32_t>((*s)[3]);
}

// View bytes as characters (no copy). Caller guarantees the lifetime of b.
inline std::string_view chars(Bytes b) noexcept {
    return {reinterpret_cast<const char*>(b.data()), b.size()};
}

// True if b starts with the given ASCII tag at offset off.
inline bool has_tag(Bytes b, std::size_t off, std::string_view tag) noexcept {
    const auto s = sub(b, off, tag.size());
    return s && chars(*s) == tag;
}

inline std::string hex32(std::uint32_t v) {
    constexpr char digits[] = "0123456789ABCDEF";
    std::string out(8, '0');
    for (int i = 7; i >= 0; --i) {
        out[static_cast<std::size_t>(i)] = digits[v & 0xF];
        v >>= 4;
    }
    return out;
}

} // namespace k2disc
