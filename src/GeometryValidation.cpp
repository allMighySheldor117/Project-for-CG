#include "crystalbound/Geometry.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>

#include "GeometryMath.hpp"

namespace crystalbound {
namespace {

using namespace geometry_detail;

[[nodiscard]] GeometryVector3 position_of(const Vertex& vertex) noexcept
{
    return {vertex.position[0], vertex.position[1], vertex.position[2]};
}

[[nodiscard]] GeometryVector3 normal_of(const Vertex& vertex) noexcept
{
    return {vertex.normal[0], vertex.normal[1], vertex.normal[2]};
}

[[nodiscard]] bool valid_bounds(const AxisAlignedBounds& bounds) noexcept
{
    return finite(bounds.minimum_metres) && finite(bounds.maximum_metres)
        && bounds.minimum_metres.x <= bounds.maximum_metres.x
        && bounds.minimum_metres.y <= bounds.maximum_metres.y
        && bounds.minimum_metres.z <= bounds.maximum_metres.z;
}

}  // namespace

void validate_procedural_mesh(const MeshData& mesh)
{
    try {
        validate_mesh_data(mesh);
    } catch (const std::invalid_argument& error) {
        throw GeometryError{std::string{"Procedural mesh failed base validation: "} + error.what()};
    }
    if (mesh.vertices.size() > geometry_budgets.maximum_static_vertices) {
        throw GeometryError{"Procedural mesh exceeds the static vertex budget."};
    }

    constexpr double relative_area_epsilon{1.0e-18};
    constexpr double minimum_normal_alignment{0.05};
    for (std::size_t offset{}; offset < mesh.indices.size(); offset += 3U) {
        const Vertex& first_vertex{mesh.vertices[mesh.indices[offset]]};
        const Vertex& second_vertex{mesh.vertices[mesh.indices[offset + 1U]]};
        const Vertex& third_vertex{mesh.vertices[mesh.indices[offset + 2U]]};
        const GeometryVector3 first{position_of(first_vertex)};
        const GeometryVector3 second{position_of(second_vertex)};
        const GeometryVector3 third{position_of(third_vertex)};
        const GeometryVector3 first_edge{subtract(second, first)};
        const GeometryVector3 second_edge{subtract(third, first)};
        const GeometryVector3 third_edge{subtract(third, second)};
        const GeometryVector3 face_cross{cross(first_edge, second_edge)};
        const double longest_edge_squared{std::max({
            squared_length(first_edge),
            squared_length(second_edge),
            squared_length(third_edge)})};
        const double area_squared{squared_length(face_cross)};
        if (!std::isfinite(longest_edge_squared) || !std::isfinite(area_squared)
            || longest_edge_squared <= vector_epsilon_squared
            || area_squared <= longest_edge_squared * longest_edge_squared
                * relative_area_epsilon) {
            throw GeometryError{"Procedural mesh contains a zero-area triangle."};
        }
        const GeometryVector3 face_normal{normalized(face_cross, "Triangle face normal")};
        const GeometryVector3 average_normal{normalized(
            add(add(normal_of(first_vertex), normal_of(second_vertex)), normal_of(third_vertex)),
            "Triangle average normal")};
        if (dot(face_normal, average_normal) < minimum_normal_alignment) {
            throw GeometryError{
                "Procedural mesh triangle " + std::to_string(offset / 3U)
                + " winding does not agree with its vertex normals (alignment "
                + std::to_string(dot(face_normal, average_normal)) + ")."};
        }
    }
}

AxisAlignedBounds mesh_bounds(const MeshData& mesh)
{
    validate_procedural_mesh(mesh);
    const double infinity{std::numeric_limits<double>::infinity()};
    AxisAlignedBounds bounds{
        {infinity, infinity, infinity},
        {-infinity, -infinity, -infinity},
    };
    for (const Vertex& vertex : mesh.vertices) {
        const GeometryVector3 position{position_of(vertex)};
        bounds.minimum_metres.x = std::min(bounds.minimum_metres.x, position.x);
        bounds.minimum_metres.y = std::min(bounds.minimum_metres.y, position.y);
        bounds.minimum_metres.z = std::min(bounds.minimum_metres.z, position.z);
        bounds.maximum_metres.x = std::max(bounds.maximum_metres.x, position.x);
        bounds.maximum_metres.y = std::max(bounds.maximum_metres.y, position.y);
        bounds.maximum_metres.z = std::max(bounds.maximum_metres.z, position.z);
    }
    return bounds;
}

bool separated_by_margin(
    const AxisAlignedBounds& left,
    const AxisAlignedBounds& right,
    const double margin_metres) noexcept
{
    if (!valid_bounds(left) || !valid_bounds(right) || !std::isfinite(margin_metres)
        || margin_metres < 0.0) {
        return false;
    }
    const double x_gap{std::max(
        right.minimum_metres.x - left.maximum_metres.x,
        left.minimum_metres.x - right.maximum_metres.x)};
    const double y_gap{std::max(
        right.minimum_metres.y - left.maximum_metres.y,
        left.minimum_metres.y - right.maximum_metres.y)};
    const double z_gap{std::max(
        right.minimum_metres.z - left.maximum_metres.z,
        left.minimum_metres.z - right.maximum_metres.z)};
    return x_gap >= margin_metres || y_gap >= margin_metres || z_gap >= margin_metres;
}

}  // namespace crystalbound
