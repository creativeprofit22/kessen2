// SPDX-License-Identifier: GPL-3.0-or-later
// Random-access byte sources: a file on disk (the disc image, a container) or memory (tests).
#pragma once

#include "error.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <vector>

namespace k2disc {

class ByteSource {
public:
    ByteSource() = default;
    ByteSource(const ByteSource&) = delete;
    ByteSource& operator=(const ByteSource&) = delete;
    virtual ~ByteSource() = default;

    [[nodiscard]] virtual std::uint64_t size() const noexcept = 0;
    // Fills `out` from `offset`; fails if [offset, offset+out.size()) is out of range.
    [[nodiscard]] virtual Result<void> read(std::uint64_t offset, std::span<std::uint8_t> out) const = 0;

    // Convenience: read `len` bytes into a new vector.
    [[nodiscard]] Result<std::vector<std::uint8_t>> read_vec(std::uint64_t offset, std::size_t len) const;
};

class MemorySource final : public ByteSource {
public:
    explicit MemorySource(std::vector<std::uint8_t> data) : data_(std::move(data)) {}
    [[nodiscard]] std::uint64_t size() const noexcept override { return data_.size(); }
    [[nodiscard]] Result<void> read(std::uint64_t offset, std::span<std::uint8_t> out) const override;

private:
    std::vector<std::uint8_t> data_;
};

// Non-owning view over bytes that outlive this source.
class SpanSource final : public ByteSource {
public:
    explicit SpanSource(std::span<const std::uint8_t> data) : data_(data) {}
    [[nodiscard]] std::uint64_t size() const noexcept override { return data_.size(); }
    [[nodiscard]] Result<void> read(std::uint64_t offset, std::span<std::uint8_t> out) const override;

private:
    std::span<const std::uint8_t> data_;
};

class FileSource final : public ByteSource {
public:
    static Result<std::unique_ptr<FileSource>> open(const std::filesystem::path& path);
    [[nodiscard]] std::uint64_t size() const noexcept override { return size_; }
    [[nodiscard]] Result<void> read(std::uint64_t offset, std::span<std::uint8_t> out) const override;

private:
    FileSource(std::ifstream in, std::uint64_t size) : in_(std::move(in)), size_(size) {}
    mutable std::ifstream in_;
    std::uint64_t size_;
};

// A window [base, base+length) of another source (e.g. one file inside the disc image).
class SliceSource final : public ByteSource {
public:
    SliceSource(const ByteSource& parent, std::uint64_t base, std::uint64_t length)
        : parent_(parent), base_(base), length_(length) {}
    [[nodiscard]] std::uint64_t size() const noexcept override { return length_; }
    [[nodiscard]] Result<void> read(std::uint64_t offset, std::span<std::uint8_t> out) const override;

private:
    const ByteSource& parent_;
    std::uint64_t base_;
    std::uint64_t length_;
};

// True if [offset, offset+len) fits in [0, total) without overflow.
[[nodiscard]] constexpr bool range_ok(std::uint64_t offset, std::uint64_t len, std::uint64_t total) noexcept {
    return offset <= total && len <= total - offset;
}

} // namespace k2disc
