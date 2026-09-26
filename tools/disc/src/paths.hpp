// SPDX-License-Identifier: GPL-3.0-or-later
// Sanitising names taken from untrusted images/containers and joining them under an output root.
#pragma once

#include "error.hpp"

#include <filesystem>
#include <string>
#include <string_view>

namespace k2disc {

// Validates one path component: non-empty, not "."/"..", no separators, drive colons,
// control characters, or Windows reserved device names. Returns the component unchanged.
[[nodiscard]] Result<std::string> sanitize_component(std::string_view name);

// Validates a '/'-separated relative path component by component.
[[nodiscard]] Result<std::string> sanitize_relative(std::string_view rel);

// UTF-8 text of a path for messages and output. Use instead of path::string(), which on Windows
// goes through the ANSI code page and throws for characters it cannot represent.
[[nodiscard]] std::string display_path(const std::filesystem::path& p);

// root / rel after sanitising rel; guaranteed to be lexically inside root.
[[nodiscard]] Result<std::filesystem::path> safe_join(const std::filesystem::path& root, std::string_view rel);

} // namespace k2disc
