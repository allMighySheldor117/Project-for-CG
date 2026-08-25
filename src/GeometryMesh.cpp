#include "crystalbound/Geometry.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

#include "GeometryMath.hpp"

namespace crystalbound {
namespace {

using namespace geometry_detail;

constexpr double pi{3.14159265358979323846};

[[nodiscard]] Vertex make_vertex(
    const GeometryVector3& position,
    const GeometryVector3& normal,
    const double longitudinal_coordinate,
    const double ring_coordinate)
{
    if (!finite(position) || !finite(normal)
        || !std::isfinite(longitudinal_coordinate) || !std::isfinite(ring_coordinate)) {
        throw GeometryError{"Spline ring produced a non-finite vertex attribute."};
    }
    return {
        {
            static_cast<float>(position.x),
            static_cast<float>(position.y),
            static_cast<float>(position.z),
        },
        {
            static_cast<float>(normal.x),
            static_cast<float>(normal.y),
            static_cast<float>(normal.z),
        },
        {
            static_cast<float>(longitudinal_coordinate),
            static_cast<float>(ring_coordinate),
        },
    };
}

}  // namespace

MeshBuilder::MeshBuilder(const std::size_t maximum_vertices)
    : maximum_vertices_{maximum_vertices}
{
    if (maximum_vertices_ == 0U
        || maximum_vertices_ > geometry_budgets.maximum_static_vertices) {
        throw GeometryError{"Mesh builder vertex budget is outside the locked static limit."};
    }
}

std::uint32_t MeshBuilder::append_vertex(const Vertex& vertex)
{
    if (mesh_.vertices.size() >= maximum_vertices_
        || mesh_.vertices.size() >= std::numeric_limits<std::uint32_t>::max()) {
        throw GeometryError{"Mesh builder exceeded its vertex budget."};
    }
    const auto index{static_cast<std::uint32_t>(mesh_.vertices.size())};
    mesh_.vertices.push_back(vertex);
    return index;
}

void MeshBuilder::append_triangle(
    const std::uint32_t first,
    const std::uint32_t second,
    const std::uint32_t third)
{
    if (first >= mesh_.vertices.size() || second >= mesh_.vertices.size()
        || third >= mesh_.vertices.size()) {
        throw GeometryError{"Mesh builder received an out-of-range triangle index."};
    }
    if (first == second || first == third || second == third) {
        throw GeometryError{"Mesh builder received a triangle with repeated indices."};
    }
    mesh_.indices.insert(mesh_.indices.end(), {first, second, third});
}

MeshData MeshBuilder::finish()
{
    MeshData result{std::move(mesh_)};
    mesh_ = {};
    validate_procedural_mesh(result);
    return result;
}

MeshData build_spline_ring_mesh(
    const SplineRouteInput& input,
    const SplineSamplingContract& contract)
{
    const std::vector<SplineSample> samples{
        sample_centripetal_catmull_rom(input, contract)};
    const std::vector<TransportFrame> frames{build_parallel_transport_frames(samples)};
    const std::size_t vertices_per_ring{
        static_cast<std::size_t>(input.ring_side_count) + 1U};
    if (frames.size() > geometry_budgets.maximum_static_vertices / vertices_per_ring) {
        throw GeometryError{"Spline ring mesh exceeds the static vertex budget."};
    }

    MeshBuilder builder{geometry_budgets.maximum_static_vertices};
    const double radius{static_cast<double>(input.radius_millimetres) / 1'000.0};
    for (std::size_t ring{}; ring < frames.size(); ++ring) {
        const TransportFrame& frame{frames[ring]};
        for (std::uint32_t side{}; side <= input.ring_side_count; ++side) {
            const double ring_coordinate{
                static_cast<double>(side) / static_cast<double>(input.ring_side_count)};
            const double angle{
                side == input.ring_side_count ? 0.0 : ring_coordinate * 2.0 * pi};
            const GeometryVector3 radial{add(
                multiply(frame.normal, std::cos(angle)),
                multiply(frame.binormal, std::sin(angle)))};
            const GeometryVector3 position{add(
                frame.position_metres,
                multiply(radial, radius))};
            const GeometryVector3 surface_normal{
                input.facing == SurfaceFacing::inward ? multiply(radial, -1.0) : radial};
            static_cast<void>(builder.append_vertex(make_vertex(
                position,
                surface_normal,
                samples[ring].distance_metres,
                ring_coordinate)));
        }
    }

    for (std::size_t ring{}; ring + 1U < frames.size(); ++ring) {
        const auto first_ring{static_cast<std::uint32_t>(ring * vertices_per_ring)};
        const auto second_ring{static_cast<std::uint32_t>((ring + 1U) * vertices_per_ring)};
        for (std::uint32_t side{}; side < input.ring_side_count; ++side) {
            const std::uint32_t first{first_ring + side};
            const std::uint32_t first_next{first + 1U};
            const std::uint32_t second{second_ring + side};
            const std::uint32_t second_next{second + 1U};
            if (input.facing == SurfaceFacing::inward) {
                builder.append_triangle(first, second, second_next);
                builder.append_triangle(first, second_next, first_next);
            } else {
                builder.append_triangle(first, second_next, second);
                builder.append_triangle(first, first_next, second_next);
            }
        }
    }
    return builder.finish();
}

}  // namespace crystalbound
