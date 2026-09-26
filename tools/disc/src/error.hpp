// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <expected>
#include <string>
#include <utility>

namespace k2disc {

// Expected failure (malformed input, I/O error). Message names offset/reason.
struct Error {
    std::string message;
};

template <class T>
using Result = std::expected<T, Error>;

inline std::unexpected<Error> fail(std::string message) {
    return std::unexpected<Error>(Error{std::move(message)});
}

} // namespace k2disc
