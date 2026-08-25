#pragma once

#include <filesystem>

namespace npr {

[[nodiscard]] std::filesystem::path executable_path();
[[nodiscard]] std::filesystem::path resource_root();

}  // namespace npr
