// SPDX-License-Identifier: GPL-3.0-or-later
// Builds tiny synthetic ISO9660 images in memory for tests.
#pragma once

#include "test_support.hpp"

#include <string_view>
#include <vector>

namespace k2test {

constexpr std::uint32_t kSec = 2048;

inline Buf iso_record(std::string_view name, std::uint32_t lba, std::uint32_t len, std::uint8_t flags) {
    const auto name_len = static_cast<std::uint8_t>(name.size());
    std::size_t rec_len = 33 + name_len;
    if (rec_len % 2 != 0) {
        ++rec_len;
    }
    Buf r(rec_len, 0);
    r[0] = static_cast<std::uint8_t>(rec_len);
    put32(r, 2, lba);
    put32be(r, 6, lba);
    put32(r, 10, len);
    put32be(r, 14, len);
    r[25] = flags;
    put16(r, 28, 1);
    r[32] = name_len;
    put_str(r, 33, name);
    return r;
}

inline Buf dot(std::uint32_t lba, std::uint32_t len) {
    return iso_record(std::string_view("\0", 1), lba, len, 2);
}
inline Buf dotdot(std::uint32_t lba, std::uint32_t len) {
    return iso_record(std::string_view("\1", 1), lba, len, 2);
}

class IsoBuilder {
public:
    explicit IsoBuilder(std::uint32_t sectors = 40) { img.resize(static_cast<std::size_t>(sectors) * kSec, 0); }

    // Writes a volume descriptor set: PVD + terminator (+ optional UDF recognition sequence).
    void volume(std::uint32_t root_lba, std::uint32_t root_len, bool udf = false) {
        const std::size_t pvd = 16 * kSec;
        img[pvd] = 1;
        put_str(img, pvd + 1, "CD001");
        img[pvd + 6] = 1;
        put_str(img, pvd + 8, "PLAYSTATION                     ");
        put_str(img, pvd + 40, "TESTVOL                         ");
        put32(img, pvd + 80, static_cast<std::uint32_t>(img.size() / kSec));
        put16(img, pvd + 128, static_cast<std::uint16_t>(kSec));
        const auto root = dot(root_lba, root_len);
        std::copy(root.begin(), root.end(), img.begin() + static_cast<std::ptrdiff_t>(pvd + 156));
        const std::size_t term = 17 * kSec;
        img[term] = 255;
        put_str(img, term + 1, "CD001");
        if (udf) {
            put_str(img, 18 * kSec + 1, "BEA01");
            put_str(img, 19 * kSec + 1, "NSR02");
            put_str(img, 20 * kSec + 1, "TEA01");
        }
    }

    // Packs records into consecutive sectors starting at lba (records never span sectors).
    void dir(std::uint32_t lba, const std::vector<Buf>& records) {
        std::size_t pos = static_cast<std::size_t>(lba) * kSec;
        for (const auto& r : records) {
            if ((pos % kSec) + r.size() > kSec) {
                pos = (pos / kSec + 1) * kSec;
            }
            ensure(img, pos + r.size());
            std::copy(r.begin(), r.end(), img.begin() + static_cast<std::ptrdiff_t>(pos));
            pos += r.size();
        }
    }

    void data(std::uint32_t lba, std::string_view content) { put_str(img, static_cast<std::size_t>(lba) * kSec, content); }

    Buf img;
};

// A small valid image:
//   /SYSTEM.CNF  (lba 24)   /SLUS_123.45 (lba 25)   /DATA (dir, lba 22)
//   /DATA/FILE.BIN (lba 26) /DATA/README (lba 27, ISO name "README.;1")
inline Buf sample_iso(std::string_view cnf = "BOOT2 = cdrom0:\\SLUS_123.45;1\r\nVER = 1.00\r\nVMODE = NTSC\r\n",
                      bool udf = true) {
    IsoBuilder b;
    b.volume(20, kSec, udf);
    const auto cnf_len = static_cast<std::uint32_t>(cnf.size());
    b.dir(20, {dot(20, kSec), dotdot(20, kSec), iso_record("DATA", 22, kSec, 2),
               iso_record("SLUS_123.45;1", 25, 13, 0), iso_record("SYSTEM.CNF;1", 24, cnf_len, 0)});
    b.dir(22, {dot(22, kSec), dotdot(20, kSec), iso_record("FILE.BIN;1", 26, 5, 0),
               iso_record("README.;1", 27, 3, 0)});
    b.data(24, cnf);
    b.data(25, "\x7F" "ELFfakeelf!!");
    b.data(26, "hello");
    b.data(27, "abc");
    return b.img;
}

} // namespace k2test
