// SPDX-License-Identifier: GPL-3.0-or-later
#include "crc32.hpp"
#include "bytes.hpp"
#include "source.hpp"
#include "test_support.hpp"

using namespace k2test;

int main() {
    return run({
        {"check value 123456789", [] {
             const auto b = bytes_of("123456789");
             check_eq(k2disc::crc32(b), 0xCBF43926u, "standard CRC-32 check value");
         }},
        {"empty input", [] { check_eq(k2disc::crc32({}), 0u, "CRC of empty is 0"); }},
        {"chunked equals one-shot", [] {
             Buf b(1000);
             for (std::size_t i = 0; i < b.size(); ++i) {
                 b[i] = static_cast<std::uint8_t>(i * 7 + 3);
             }
             k2disc::Crc32 c;
             c.update(std::span(b).first(1));
             c.update(std::span(b).subspan(1, 499));
             c.update(std::span(b).subspan(500));
             check_eq(c.value(), k2disc::crc32(b), "chunked == one-shot");
         }},
        {"hex32 formatting", [] { check_eq(k2disc::hex32(0x0BADF00Du), std::string("0BADF00D"), "hex32"); }},
        {"bounds-checked readers", [] {
             const Buf b{1, 2, 3, 4, 5};
             check(k2disc::u32le(b, 1) == 0x05040302u, "u32le in range");
             check(!k2disc::u32le(b, 2).has_value(), "u32le past end rejected");
             check(!k2disc::sub(b, SIZE_MAX, 2).has_value(), "overflowing sub rejected");
             k2disc::MemorySource src(b);
             std::uint8_t out[2]{};
             check(!src.read(4, out).has_value(), "source read past end rejected");
             check(src.read(3, out).has_value() && out[0] == 4 && out[1] == 5, "source read in range");
             k2disc::SliceSource s(src, 1, 3);
             check(!s.read(2, out).has_value(), "slice read past end rejected");
         }},
    });
}
