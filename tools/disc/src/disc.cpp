// SPDX-License-Identifier: GPL-3.0-or-later
#include "disc.hpp"

#include "bytes.hpp"
#include "crc32.hpp"
#include "paths.hpp"

#include <fstream>
#include <system_error>

namespace k2disc {
namespace {

constexpr std::size_t kChunk = 4u << 20;

std::string label_for(const SystemCnf& cnf, const IsoVolume& vol) {
    if (!cnf.serial.empty()) {
        return cnf.serial;
    }
    if (auto v = sanitize_component(vol.volume_id); v) {
        return *v;
    }
    return "UNKNOWN";
}

Result<void> ensure_dir(const std::filesystem::path& p) {
    std::error_code ec;
    std::filesystem::create_directories(p, ec);
    if (ec) {
        return fail("cannot create directory '" + display_path(p) + "': " + ec.message());
    }
    return {};
}

} // namespace

Result<DiscInfo> inspect_disc(const ByteSource& image) {
    auto vol = read_iso9660(image);
    if (!vol) {
        return std::unexpected(vol.error());
    }
    const IsoEntry* cnf_entry = vol->find("SYSTEM.CNF");
    if (cnf_entry == nullptr || cnf_entry->is_dir) {
        return fail("SYSTEM.CNF not found in the ISO root");
    }
    auto cnf_bytes = read_file(image, *cnf_entry, 64 * 1024);
    if (!cnf_bytes) {
        return std::unexpected(cnf_bytes.error());
    }
    auto cnf = parse_system_cnf(chars(*cnf_bytes));
    if (!cnf) {
        return std::unexpected(cnf.error());
    }
    const IsoEntry* boot = vol->find(cnf->boot_path);
    if (boot == nullptr || boot->is_dir) {
        return fail("boot ELF '" + cnf->boot_path + "' named by SYSTEM.CNF is not on the disc");
    }
    Crc32 crc;
    auto r = for_each_chunk(image, *boot, kChunk, [&](std::span<const std::uint8_t> p) -> Result<void> {
        crc.update(p);
        return {};
    });
    if (!r) {
        return std::unexpected(r.error());
    }
    DiscInfo info{std::move(*vol), std::move(*cnf), *boot, crc.value(), {}};
    info.label = label_for(info.cnf, info.volume);
    return info;
}

Result<ExtractSummary> extract_disc(const ByteSource& image, const IsoVolume& volume,
                                    const std::filesystem::path& out_root) {
    ExtractSummary sum;
    const auto iso_root = out_root / "iso";
    if (auto r = ensure_dir(iso_root); !r) {
        return std::unexpected(r.error());
    }
    std::string manifest = "# path\tlba\tsize\tcrc32\n";
    for (const auto& e : volume.entries) { // already sorted by path
        auto dest = safe_join(iso_root, e.path);
        if (!dest) {
            return std::unexpected(dest.error());
        }
        if (e.is_dir) {
            if (auto r = ensure_dir(*dest); !r) {
                return std::unexpected(r.error());
            }
            ++sum.dirs;
            continue;
        }
        if (auto r = ensure_dir(dest->parent_path()); !r) {
            return std::unexpected(r.error());
        }
        std::ofstream out(*dest, std::ios::binary | std::ios::trunc);
        if (!out) {
            return fail("cannot write '" + dest->string() + "'");
        }
        Crc32 crc;
        auto r = for_each_chunk(image, e, kChunk, [&](std::span<const std::uint8_t> p) -> Result<void> {
            crc.update(p);
            out.write(reinterpret_cast<const char*>(p.data()), static_cast<std::streamsize>(p.size()));
            if (!out) {
                return fail("write failed for '" + dest->string() + "'");
            }
            return {};
        });
        if (!r) {
            return std::unexpected(r.error());
        }
        out.close();
        if (!out) {
            return fail("close failed for '" + dest->string() + "'");
        }
        manifest += e.path + "\t" + std::to_string(e.first_lba()) + "\t" + std::to_string(e.size) + "\t" +
                    hex32(crc.value()) + "\n";
        ++sum.files;
        sum.bytes += e.size;
    }
    sum.manifest = out_root / "manifest.tsv";
    std::ofstream m(sum.manifest, std::ios::binary | std::ios::trunc);
    m << manifest;
    m.close();
    if (!m) {
        return fail("cannot write manifest '" + display_path(sum.manifest) + "'");
    }
    return sum;
}

} // namespace k2disc
