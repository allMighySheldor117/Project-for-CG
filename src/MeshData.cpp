#include "crystalbound/MeshData.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace crystalbound {
namespace {

[[nodiscard]] bool finite_vector(const std::array<float, 3>& value)
{
    return std::isfinite(value[0]) && std::isfinite(value[1]) && std::isfinite(value[2]);
}

}  // namespace

void validate_mesh_data(const MeshData& mesh)
{
    if (mesh.vertices.empty()) {
        throw std::invalid_argument("Mesh data contains no vertices.");
    }
    if (mesh.indices.empty() || mesh.indices.size() % 3 != 0) {
        throw std::invalid_argument("Mesh indices must contain one or more complete triangles.");
    }
    if (mesh.vertices.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::invalid_argument("Mesh has more vertices than 32-bit indices can address.");
    }

    constexpr float unit_normal_tolerance{1.0e-3F};
    for (const Vertex& vertex : mesh.vertices) {
        if (!finite_vector(vertex.position) || !finite_vector(vertex.normal)
            || !std::isfinite(vertex.texture_coordinates[0])
            || !std::isfinite(vertex.texture_coordinates[1])) {
            throw std::invalid_argument("Mesh contains a non-finite vertex attribute.");
        }
        const float squared_length = vertex.normal[0] * vertex.normal[0]
            + vertex.normal[1] * vertex.normal[1]
            + vertex.normal[2] * vertex.normal[2];
        if (std::abs(squared_length - 1.0F) > unit_normal_tolerance) {
            throw std::invalid_argument("Mesh contains a normal that is not unit length.");
        }
    }

    for (const std::uint32_t index : mesh.indices) {
        if (index >= mesh.vertices.size()) {
            throw std::invalid_argument("Mesh contains an out-of-range index.");
        }
    }
}

}  // namespace crystalbound
