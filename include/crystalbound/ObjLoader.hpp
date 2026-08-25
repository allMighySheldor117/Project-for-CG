#pragma once

#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include "crystalbound/MeshData.hpp"

namespace crystalbound {

class ModelLoadError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct ModelLoadResult {
    MeshData mesh;
    std::vector<std::string> warnings;
};

[[nodiscard]] ModelLoadResult load_obj(const std::filesystem::path& path);

}  // namespace crystalbound
