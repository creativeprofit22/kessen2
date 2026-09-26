// SPDX-License-Identifier: GPL-3.0-or-later
#include "catalogue.hpp"

#include <algorithm>
#include <array>
#include <cctype>

namespace k2disc {
namespace {

using namespace std::string_view_literals; // magics may contain NUL bytes

struct Magic {
    std::size_t offset;
    std::string_view bytes;
    std::string_view id;
    std::string_view description;
    std::string_view extension;
};

// Plain prefix magics. ELF and SCEI chunked audio are handled in identify() first.
constexpr std::array kMagics{
    Magic{0, "VAGp", "vag", "Sony VAG ADPCM sample", "vag"},
    Magic{0, "TIM2", "tim2", "TIM2 texture", "tm2"},
    Magic{0, "CLT2", "clt2", "TIM2 CLUT", "clt2"},
    Magic{0, "RESET", "iop-romdir", "IOP ROMDIR/IOPRP image", "img"},
    Magic{0, "BOOT2", "system-cnf", "PS2 SYSTEM.CNF", "cnf"},
    Magic{0, "\x00\x00\x01\xBA"sv, "mpeg-ps", "MPEG-2 program stream (PSS/IPU movie)", "pss"},
    Magic{0, "\x00\x00\x01\xB3"sv, "mpeg-es", "MPEG video elementary stream", "m2v"},
    Magic{0, "ipum", "ipu", "IPU movie stream", "ipu"},
    Magic{0, "RIFF", "riff", "RIFF container", "riff"},
    Magic{0, "\x89PNG", "png", "PNG image", "png"},
    Magic{0, "PK\x03\x04", "zip", "ZIP archive", "zip"},
    Magic{0, "\x1F\x8B", "gzip", "gzip stream", "gz"},
    Magic{0, "SShd", "sony-sshd-le", "Sony SShd (little-endian tag)", "hd"},
    Magic{0, "SSbd", "sony-ssbd", "Sony SSbd sample body", "bd"},
    Magic{0, "PS2D", "ps2-icon-sys", "PS2 memory-card icon.sys", "sys"},
    // Kessen II / Koei engine formats (docs/formats/INDEX.md)
    Magic{0, "TMD2", "koei-tmd2", "Koei TMD2 model (\"TMD20060\")", "tmd"},
    Magic{0, "TOD2", "koei-tod2", "Koei TOD2 animation", "tod"},
    Magic{0, "FCVQ", "koei-fcvq", "Koei FCVQ (\"FCVQ0100\") VQ-compressed image/video", "fcvq"},
    Magic{0, "MOP0", "koei-mop0", "Koei MOP0 data", "mop"},
};

bool matches(Bytes head, const Magic& m) {
    return has_tag(head, m.offset, m.bytes);
}

bool mostly_text(Bytes head) {
    if (head.size() < 8) {
        return false;
    }
    std::size_t printable = 0;
    for (const auto b : head) {
        if (b == '\r' || b == '\n' || b == '\t' || (b >= 0x20 && b < 0x7F)) {
            ++printable;
        }
    }
    return printable == head.size();
}

} // namespace

FileKind identify(Bytes head) {
    if (head.empty()) {
        return {"empty", "empty file", "bin"};
    }
    if (has_tag(head, 0, "\x7F" "ELF")) {
        const auto type = u16le(head, 16);
        if (type && *type == 0xFF80) {
            return {"elf-irx", "IOP module (ELF type 0xFF80)", "irx"};
        }
        return {"elf", "ELF executable", "elf"};
    }
    // SShd vs SQ share the "IECSsreV" (reversed "SCEIVers") header; the second chunk decides.
    if (has_tag(head, 0, "IECSsreV")) {
        if (has_tag(head, 16, "IECSdaeH")) {
            return {"sony-sshd", "Sony SShd sound bank header (SCEI Vers/Head/Vagi)", "hd"};
        }
        if (has_tag(head, 16, "IECSuqeS")) {
            return {"sony-sq", "Sony SQ MIDI sequence (SCEI Vers/Sequ)", "sq"};
        }
        return {"sony-scei", "Sony SCEI chunked audio data", "scei"};
    }
    for (const auto& m : kMagics) {
        if (matches(head, m)) {
            return {std::string(m.id), std::string(m.description), std::string(m.extension)};
        }
    }
    if (mostly_text(head.first(std::min<std::size_t>(head.size(), kMagicProbeBytes)))) {
        return {"text", "ASCII text", "txt"};
    }
    if (std::ranges::all_of(head, [](std::uint8_t b) { return b == 0; })) {
        return {"zeros", "all-zero header (padding or headerless data)", "bin"};
    }
    std::string id = "unknown:";
    for (std::size_t i = 0; i < std::min<std::size_t>(4, head.size()); ++i) {
        id += hex32(head[i]).substr(6);
    }
    return {id, "unknown", "bin"};
}

std::string extension_of(std::string_view path) {
    const auto slash = path.find_last_of("/:");
    const auto name = slash == std::string_view::npos ? path : path.substr(slash + 1);
    const auto dot = name.rfind('.');
    if (dot == std::string_view::npos || dot + 1 == name.size()) {
        return {};
    }
    std::string ext(name.substr(dot + 1));
    std::ranges::transform(ext, ext.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return ext;
}

void Catalogue::add(std::string_view path, std::uint64_t size, const FileKind& kind) {
    auto& row = rows_[{extension_of(path), kind.id}];
    if (row.count == 0) {
        row.extension = extension_of(path);
        row.kind_id = kind.id;
        row.description = kind.description;
    }
    ++row.count;
    row.total_bytes += size;
    row.samples.emplace_back(path);
    std::ranges::sort(row.samples);
    if (row.samples.size() > 3) {
        row.samples.resize(3);
    }
    ++files_;
    bytes_ += size;
}

std::vector<CatalogueRow> Catalogue::rows() const {
    std::vector<CatalogueRow> out;
    out.reserve(rows_.size());
    for (const auto& [key, row] : rows_) {
        out.push_back(row);
    }
    return out;
}

std::string render_tsv(const Catalogue& c) {
    std::string s = "# extension\tkind\tcount\ttotal_bytes\tdescription\tsamples\n";
    for (const auto& r : c.rows()) {
        std::string samples;
        for (const auto& p : r.samples) {
            samples += (samples.empty() ? "" : ",") + p;
        }
        s += (r.extension.empty() ? "-" : r.extension) + "\t" + r.kind_id + "\t" + std::to_string(r.count) + "\t" +
             std::to_string(r.total_bytes) + "\t" + r.description + "\t" + samples + "\n";
    }
    s += "# total\t\t" + std::to_string(c.total_files()) + "\t" + std::to_string(c.total_bytes()) + "\n";
    return s;
}

std::string render_markdown(const Catalogue& c) {
    std::string s = "| Extension | Kind (magic) | Count | Total bytes | Description | Samples |\n"
                    "|---|---|---:|---:|---|---|\n";
    for (const auto& r : c.rows()) {
        std::string samples;
        for (const auto& p : r.samples) {
            samples += (samples.empty() ? "" : ", ") + ("`" + p + "`");
        }
        s += "| " + (r.extension.empty() ? std::string("—") : "`" + r.extension + "`") + " | `" + r.kind_id +
             "` | " + std::to_string(r.count) + " | " + std::to_string(r.total_bytes) + " | " + r.description +
             " | " + samples + " |\n";
    }
    s += "| **total** | | " + std::to_string(c.total_files()) + " | " + std::to_string(c.total_bytes()) + " | | |\n";
    return s;
}

} // namespace k2disc
