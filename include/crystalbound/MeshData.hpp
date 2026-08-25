#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace crystalbound {

struct Vertex {
    std::array<float, 3> position{};
    std::array<float, 3> normal{};
    std::array<float, 2> texture_coordinates{};
};

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
};

void validate_mesh_data(const MeshData& mesh);

}  // namespace crystalbound
