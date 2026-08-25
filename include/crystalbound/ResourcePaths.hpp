#pragma once

#include <filesystem>

namespace crystalbound {

[[nodiscard]] std::filesystem::path executable_path();
[[nodiscard]] std::filesystem::path resource_root();

}  // namespace crystalbound
