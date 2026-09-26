// SPDX-License-Identifier: GPL-3.0-or-later
#include "source.hpp"

#include "paths.hpp"

#include <algorithm>
#include <string>
#include <system_error>

namespace k2disc {

Result<std::vector<std::uint8_t>> ByteSource::read_vec(std::uint64_t offset, std::size_t len) const {
    std::vector<std::uint8_t> buf(len);
    if (auto r = read(offset, buf); !r) {
        return std::unexpected(r.error());
    }
    return buf;
}

Result<void> MemorySource::read(std::uint64_t offset, std::span<std::uint8_t> out) const {
    if (!range_ok(offset, out.size(), data_.size())) {
        return fail("read out of range at offset " + std::to_string(offset) + " (+" + std::to_string(out.size()) + ")");
    }
    std::copy_n(data_.begin() + static_cast<std::ptrdiff_t>(offset), out.size(), out.begin());
    return {};
}

Result<void> SpanSource::read(std::uint64_t offset, std::span<std::uint8_t> out) const {
    if (!range_ok(offset, out.size(), data_.size())) {
        return fail("read out of range at offset " + std::to_string(offset) + " (+" + std::to_string(out.size()) + ")");
    }
    std::copy_n(data_.begin() + static_cast<std::ptrdiff_t>(offset), out.size(), out.begin());
    return {};
}

Result<std::unique_ptr<FileSource>> FileSource::open(const std::filesystem::path& path) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) {
        return fail("cannot stat '" + display_path(path) + "': " + ec.message());
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return fail("cannot open '" + display_path(path) + "'");
    }
    return std::unique_ptr<FileSource>(new FileSource(std::move(in), size));
}

Result<void> FileSource::read(std::uint64_t offset, std::span<std::uint8_t> out) const {
    if (!range_ok(offset, out.size(), size_)) {
        return fail("read out of range at offset " + std::to_string(offset) + " (+" + std::to_string(out.size()) + ")");
    }
    in_.clear();
    in_.seekg(static_cast<std::streamoff>(offset));
    in_.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
    if (!in_ || static_cast<std::size_t>(in_.gcount()) != out.size()) {
        return fail("short read at offset " + std::to_string(offset));
    }
    return {};
}

Result<void> SliceSource::read(std::uint64_t offset, std::span<std::uint8_t> out) const {
    if (!range_ok(offset, out.size(), length_)) {
        return fail("read out of range at offset " + std::to_string(offset) + " (+" + std::to_string(out.size()) + ")");
    }
    return parent_.read(base_ + offset, out);
}

} // namespace k2disc
