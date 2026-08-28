#pragma once

#include <array>
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

struct MaterialMeshBatch {
    std::string material_name;
    std::array<float, 3> diffuse{};
    std::array<float, 3> emission{};
    MeshData mesh;
};

struct MaterialModelObject {
    std::string name;
    std::string material_name;
    std::array<float, 3> minimum_bounds{};
    std::array<float, 3> maximum_bounds{};
};

struct MaterialModelLoadOptions {
    std::vector<std::string> excluded_object_names{};
};

struct MaterialModelLoadResult {
    std::vector<MaterialMeshBatch> batches;
    std::vector<MaterialModelObject> objects;
    std::array<float, 3> minimum_bounds{};
    std::array<float, 3> maximum_bounds{};
    std::vector<std::string> warnings;
};

[[nodiscard]] ModelLoadResult load_obj(const std::filesystem::path& path);
[[nodiscard]] MaterialModelLoadResult load_obj_material_batches(
    const std::filesystem::path& path,
    const MaterialModelLoadOptions& options = {});

}  // namespace crystalbound
