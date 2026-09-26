// SPDX-License-Identifier: GPL-3.0-or-later
// k2disc: argument parsing and output only; logic lives in k2_disc_core.
#include "archive_linkdata.hpp"
#include "bytes.hpp"
#include "catalogue.hpp"
#include "disc.hpp"
#include "irx.hpp"
#include "paths.hpp"
#include "source.hpp"
#include "tree.hpp"

#include <algorithm>
#include <cctype>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#endif

#ifndef K2DISC_DEFAULT_OUT
#define K2DISC_DEFAULT_OUT "work/disc"
#endif

namespace {

namespace fs = std::filesystem;
using k2disc::Error;

constexpr int kOk = 0;
constexpr int kInputError = 1;
constexpr int kUsage = 2;

constexpr std::string_view kUsageText =
    "usage:\n"
    "  k2disc info    <iso>\n"
    "  k2disc extract <iso> [--out DIR]\n"
    "  k2disc irx     <iso|extracted-dir>\n"
    "  k2disc scan    <iso|dir> [--markdown]\n"
    "  k2disc archive list    <container>\n"
    "  k2disc archive extract <container> [--out DIR]\n"
    "      default DIR: <disc-root>/archives/<container-name> when the container is under\n"
    "      <root>/<SERIAL>/iso/, else " K2DISC_DEFAULT_OUT "/archives/<container-name>\n"
    "\n"
    "Default output root: " K2DISC_DEFAULT_OUT " (git-ignored). Files go to <root>/<SERIAL>/.\n"
    "Exit codes: 0 ok, 1 input/format error, 2 usage.\n";

int usage() {
    std::fputs(kUsageText.data(), stderr);
    return kUsage;
}

int report(const Error& e) {
    std::fprintf(stderr, "k2disc: error: %s\n", e.message.c_str());
    return kInputError;
}

// Command-line arguments as paths. On Windows, argv is in the ANSI code page, so the wide
// command line is decoded instead; elsewhere argv is already in the native path encoding.
std::vector<fs::path> command_line([[maybe_unused]] int argc, [[maybe_unused]] char** argv) {
    std::vector<fs::path> args;
#ifdef _WIN32
    struct LocalFreeDeleter {
        void operator()(LPWSTR* p) const noexcept { LocalFree(p); }
    };
    int n = 0;
    const std::unique_ptr<LPWSTR, LocalFreeDeleter> wide(CommandLineToArgvW(GetCommandLineW(), &n));
    if (wide) {
        const std::span<LPWSTR> items(wide.get(), static_cast<std::size_t>(n));
        for (const LPWSTR w : items) {
            args.emplace_back(std::wstring(w));
        }
    }
#else
    for (int i = 0; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }
#endif
    return args;
}

struct Args {
    std::vector<fs::path> positional;
    std::optional<fs::path> out;
    bool markdown = false;
};

std::optional<Args> parse_args(const std::vector<fs::path>& argv, std::size_t first) {
    Args a;
    for (std::size_t i = first; i < argv.size(); ++i) {
        const std::string s = k2disc::display_path(argv[i]);
        if (s == "--out") {
            if (i + 1 >= argv.size()) {
                return std::nullopt;
            }
            a.out = argv[++i];
        } else if (s == "--markdown") {
            a.markdown = true;
        } else if (s.starts_with("--")) {
            return std::nullopt;
        } else {
            a.positional.push_back(argv[i]);
        }
    }
    return a;
}

// False when a flag the command does not accept was given; callers then return usage().
bool only_flags(const Args& a, bool allow_out, bool allow_markdown) {
    return (allow_out || !a.out) && (allow_markdown || !a.markdown);
}

int cmd_info(const Args& a) {
    if (!only_flags(a, false, false) || a.positional.size() != 1) {
        return usage();
    }
    auto src = k2disc::FileSource::open(a.positional[0]);
    if (!src) {
        return report(src.error());
    }
    auto info = k2disc::inspect_disc(**src);
    if (!info) {
        return report(info.error());
    }
    const auto files = std::ranges::count_if(info->volume.entries, [](const auto& e) { return !e.is_dir; });
    std::printf("volume_id\t%s\n", info->volume.volume_id.c_str());
    std::printf("system_id\t%s\n", info->volume.system_id.c_str());
    std::printf("volume_blocks\t%u\n", info->volume.volume_blocks);
    std::printf("udf\t%s\n", info->volume.udf_nsr.empty() ? "no" : info->volume.udf_nsr.c_str());
    std::printf("files\t%lld\n", static_cast<long long>(files));
    std::printf("boot_elf\t%s\n", info->cnf.boot_path.c_str());
    std::printf("serial\t%s\n", info->cnf.serial.empty() ? "(unknown)" : info->cnf.serial.c_str());
    std::printf("version\t%s\n", info->cnf.version.c_str());
    std::printf("vmode\t%s\n", info->cnf.video_mode.c_str());
    std::printf("boot_elf_lba\t%u\n", info->boot.first_lba());
    std::printf("boot_elf_size\t%llu\n", static_cast<unsigned long long>(info->boot.size));
    std::printf("boot_elf_crc32\t0x%s\n", k2disc::hex32(info->boot_crc32).c_str());
    return kOk;
}

int cmd_extract(const Args& a) {
    if (!only_flags(a, true, false) || a.positional.size() != 1) {
        return usage();
    }
    auto src = k2disc::FileSource::open(a.positional[0]);
    if (!src) {
        return report(src.error());
    }
    auto info = k2disc::inspect_disc(**src);
    if (!info) {
        return report(info.error());
    }
    const fs::path root = a.out ? *a.out : fs::path(K2DISC_DEFAULT_OUT);
    const fs::path out = root / info->label;
    const auto t0 = std::chrono::steady_clock::now();
    auto sum = k2disc::extract_disc(**src, info->volume, out);
    if (!sum) {
        return report(sum.error());
    }
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0);
    std::printf("extracted\t%zu files, %zu dirs, %llu bytes\n", sum->files, sum->dirs,
                static_cast<unsigned long long>(sum->bytes));
    std::printf("output\t%s\n", k2disc::display_path(out / "iso").c_str());
    std::printf("manifest\t%s\n", k2disc::display_path(sum->manifest).c_str());
    std::printf("boot_elf\t%s\tcrc32 0x%s\n", info->cnf.boot_path.c_str(), k2disc::hex32(info->boot_crc32).c_str());
    std::fprintf(stderr, "k2disc: extract took %lld ms\n", static_cast<long long>(ms.count()));
    return kOk;
}

int cmd_irx(const Args& a) {
    if (!only_flags(a, false, false) || a.positional.size() != 1) {
        return usage();
    }
    auto tree = k2disc::open_tree(a.positional[0], k2disc::kToolMetadataFiles);
    if (!tree) {
        return report(tree.error());
    }
    constexpr std::uint64_t kMaxModuleFile = 64ull << 20;
    std::printf("# path\tsize\tmodule\tversion\tnote\n");
    std::size_t count = 0;
    for (const auto& f : (*tree)->files()) {
        std::string upper = f.path;
        std::ranges::transform(upper, upper.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        auto src = (*tree)->open(f);
        if (!src) {
            return report(src.error());
        }
        // Candidates: *.IRX, or anything small starting with a ROMDIR "RESET" entry (IOPRP images).
        if (!upper.ends_with(".IRX")) {
            std::uint8_t head[5]{};
            if (f.size < sizeof head || f.size > kMaxModuleFile || !(*src)->read(0, head) ||
                std::string_view(reinterpret_cast<const char*>(head), 5) != "RESET") {
                continue;
            }
        }
        auto data = k2disc::read_all(**src, kMaxModuleFile);
        if (!data) {
            return report(data.error());
        }
        for (const auto& row : k2disc::describe_iop_file(f.path, *data)) {
            std::printf("%s\t%llu\t%s\t%s\t%s\n", row.path.c_str(), static_cast<unsigned long long>(row.size),
                        row.module.c_str(), row.version.c_str(), row.note.c_str());
            ++count;
        }
    }
    std::fprintf(stderr, "k2disc: %zu IOP modules\n", count);
    return kOk;
}

int cmd_scan(const Args& a) {
    if (!only_flags(a, false, true) || a.positional.size() != 1) {
        return usage();
    }
    auto tree = k2disc::open_tree(a.positional[0], k2disc::kToolMetadataFiles);
    if (!tree) {
        return report(tree.error());
    }
    k2disc::Catalogue cat;
    for (const auto& f : (*tree)->files()) {
        auto src = (*tree)->open(f);
        if (!src) {
            return report(src.error());
        }
        std::vector<std::uint8_t> head(static_cast<std::size_t>(std::min<std::uint64_t>(f.size, k2disc::kMagicProbeBytes)));
        if (auto r = (*src)->read(0, head); !r) {
            return report(r.error());
        }
        cat.add(f.path, f.size, k2disc::identify(head));
    }
    const auto text = a.markdown ? k2disc::render_markdown(cat) : k2disc::render_tsv(cat);
    std::fwrite(text.data(), 1, text.size(), stdout);
    return kOk;
}

std::string head_kind(const k2disc::ByteSource& src, std::uint64_t off, std::uint64_t len) {
    std::vector<std::uint8_t> head(static_cast<std::size_t>(std::min<std::uint64_t>(len, k2disc::kMagicProbeBytes)));
    if (!src.read(off, head)) {
        return "?";
    }
    return k2disc::identify(head).id;
}

int cmd_archive_list(const Args& a) {
    if (!only_flags(a, false, false) || a.positional.size() != 2) {
        return usage();
    }
    auto src = k2disc::FileSource::open(a.positional[1]);
    if (!src) {
        return report(src.error());
    }
    const auto& s = **src;
    if (k2disc::is_link_archive(s, 0, s.size(), true)) {
        auto arc = k2disc::parse_link_archive(s, 0, s.size(), true);
        if (!arc) {
            return report(arc.error());
        }
        std::printf("# link archive: %zu entries, %llu bytes\n", arc->entries.size(),
                    static_cast<unsigned long long>(arc->total_size));
        std::printf("# index\toffset\tsize\tkind\n");
        for (std::size_t i = 0; i < arc->entries.size(); ++i) {
            const auto& e = arc->entries[i];
            const bool nested = k2disc::is_link_archive(s, e.offset, e.size, true);
            std::printf("%zu\t%llu\t%llu\t%s\n", i, static_cast<unsigned long long>(e.offset),
                        static_cast<unsigned long long>(e.size),
                        nested ? "link-archive" : head_kind(s, e.offset, e.size).c_str());
        }
        return kOk;
    }
    auto members = k2disc::segment_linkdata(s);
    if (!members) {
        return report(members.error());
    }
    std::uint64_t sectors = 0;
    std::size_t archives = 0;
    std::printf("# index\tsector\tsectors\tbytes\tkind\tentries\n");
    for (std::size_t i = 0; i < members->size(); ++i) {
        const auto& m = (*members)[i];
        const bool arc = m.kind == k2disc::MemberKind::LinkArchive;
        archives += arc ? 1 : 0;
        sectors += m.sectors;
        std::printf("%zu\t0x%05X\t%u\t%llu\t%s\t%u\n", i, m.sector, m.sectors, static_cast<unsigned long long>(m.size),
                    arc ? "link-archive"
                        : ("region:" + head_kind(s, static_cast<std::uint64_t>(m.sector) * 2048, m.size)).c_str(),
                    m.entries);
    }
    std::printf("# total\t%zu members (%zu link archives, %zu regions), %llu sectors = %llu bytes (file %llu)\n",
                members->size(), archives, members->size() - archives, static_cast<unsigned long long>(sectors),
                static_cast<unsigned long long>(sectors * 2048), static_cast<unsigned long long>(s.size()));
    return kOk;
}

// <root>/<SERIAL>/iso/<...>/NAME -> <root>/<SERIAL>/archives/NAME; else <default>/archives/NAME.
fs::path default_archive_out(const fs::path& container) {
    const auto abs = fs::absolute(container).lexically_normal();
    for (auto p = abs.parent_path(); !p.empty() && p != p.parent_path(); p = p.parent_path()) {
        if (p.filename() == "iso") {
            return p.parent_path() / "archives" / abs.filename();
        }
    }
    return fs::path(K2DISC_DEFAULT_OUT) / "archives" / abs.filename();
}

int cmd_archive_extract(const Args& a) {
    if (!only_flags(a, true, false) || a.positional.size() != 2) {
        return usage();
    }
    const auto& in = a.positional[1];
    auto src = k2disc::FileSource::open(in);
    if (!src) {
        return report(src.error());
    }
    const fs::path out = a.out ? *a.out : default_archive_out(in);
    const auto t0 = std::chrono::steady_clock::now();
    auto files = k2disc::extract_link_container(**src, out);
    if (!files) {
        return report(files.error());
    }
    std::uint64_t bytes = 0;
    for (const auto& f : *files) {
        bytes += f.size;
    }
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0);
    std::printf("extracted\t%zu files, %llu bytes\n", files->size(), static_cast<unsigned long long>(bytes));
    std::printf("output\t%s\n", k2disc::display_path(out).c_str());
    std::printf("index\t%s\n", k2disc::display_path(out / "index.tsv").c_str());
    std::fprintf(stderr, "k2disc: archive extract took %lld ms\n", static_cast<long long>(ms.count()));
    return kOk;
}

} // namespace

int main(int argc, char** argv) {
    const auto argv_paths = command_line(argc, argv);
    if (argv_paths.size() < 2) {
        return usage();
    }
    const std::string cmd = k2disc::display_path(argv_paths[1]);
    const auto args = parse_args(argv_paths, 2);
    if (!args) {
        return usage();
    }
    if (cmd == "info") {
        return cmd_info(*args);
    }
    if (cmd == "extract") {
        return cmd_extract(*args);
    }
    if (cmd == "irx") {
        return cmd_irx(*args);
    }
    if (cmd == "scan") {
        return cmd_scan(*args);
    }
    if (cmd == "archive" && !args->positional.empty()) {
        const std::string sub = k2disc::display_path(args->positional[0]);
        if (sub == "list") {
            return cmd_archive_list(*args);
        }
        if (sub == "extract") {
            return cmd_archive_extract(*args);
        }
    }
    return usage();
}
