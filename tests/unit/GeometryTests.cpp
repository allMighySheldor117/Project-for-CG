#include "GeometryTests.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "crystalbound/Geometry.hpp"

namespace crystalbound::test {
namespace {

constexpr double tolerance{1.0e-8};

class GeometryTestFailure final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

void require(const bool condition, const std::string_view message)
{
    if (!condition) {
        throw GeometryTestFailure{std::string{message}};
    }
}

void require_near(
    const double actual,
    const double expected,
    const std::string_view message,
    const double allowed_error = tolerance)
{
    if (!std::isfinite(actual) || std::abs(actual - expected) > allowed_error) {
        throw GeometryTestFailure{
            std::string{message} + ": expected " + std::to_string(expected)
            + ", got " + std::to_string(actual)};
    }
}

template <typename Exception, typename Function>
void require_throws(
    Function&& function,
    const std::string_view expected_message,
    const std::string_view failure_message)
{
    try {
        function();
    } catch (const Exception& error) {
        if (std::string_view{error.what()}.find(expected_message) == std::string_view::npos) {
            throw GeometryTestFailure{
                std::string{failure_message} + ": unexpected message: " + error.what()};
        }
        return;
    } catch (const std::exception& error) {
        throw GeometryTestFailure{
            std::string{failure_message} + ": wrong exception type: " + error.what()};
    }
    throw GeometryTestFailure{std::string{failure_message} + ": no exception was thrown"};
}

[[nodiscard]] double dot(const GeometryVector3& left, const GeometryVector3& right)
{
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

[[nodiscard]] GeometryVector3 subtract(
    const GeometryVector3& left,
    const GeometryVector3& right)
{
    return {left.x - right.x, left.y - right.y, left.z - right.z};
}

[[nodiscard]] double length(const GeometryVector3& value)
{
    return std::sqrt(dot(value, value));
}

[[nodiscard]] SplineRouteInput straight_route(const SurfaceFacing facing)
{
    return {
        17U,
        {
            {0, 0, 0},
            {2'000, 0, 0},
            {4'000, 0, 0},
            {6'000, 0, 0},
        },
        1'200,
        6U,
        facing,
    };
}

[[nodiscard]] SplineRouteInput curved_route()
{
    return {
        91U,
        {
            {0, 0, 0},
            {3'000, 200, 500},
            {6'000, 400, 1'000},
            {9'000, 600, 500},
            {12'000, 750, 0},
        },
        1'300,
        8U,
        SurfaceFacing::inward,
    };
}

void locked_contract_values_match_blueprint(const std::filesystem::path&)
{
    require(
        spline_sampling_contract.maximum_sample_spacing_millimetres == 500,
        "maximum spline spacing changed");
    require(
        spline_sampling_contract.maximum_overshoot_millimetres == 1'000,
        "maximum spline overshoot changed");
    require(
        spline_sampling_contract.maximum_grade_millidegrees
            == movement_envelope.maximum_slope_millidegrees,
        "spline and movement grade contracts drifted");
    require(
        geometry_budgets.maximum_static_vertices == 250'000U,
        "static vertex budget changed");
    require(
        geometry_budgets.maximum_opaque_draw_calls == 200U,
        "opaque draw budget changed");
    require(geometry_budgets.maximum_point_lights == 8U, "point-light budget changed");
    require(geometry_budgets.maximum_particles == 128U, "particle budget changed");
    require(
        geometry_budgets.maximum_transparent_draw_calls == 16U,
        "transparent draw budget changed");
    require(
        geometry_spatial_contract.chamber_safety_separation_millimetres == 3'000,
        "chamber separation contract changed");
}

void straight_spline_has_exact_endpoints_and_spacing(const std::filesystem::path&)
{
    const auto samples = sample_centripetal_catmull_rom(straight_route(SurfaceFacing::inward));
    require(samples.size() > 2U, "straight route must be subdivided");
    require_near(samples.front().position_metres.x, 0.0, "start x changed");
    require_near(samples.back().position_metres.x, 6.0, "end x changed");
    require_near(samples.front().distance_metres, 0.0, "start distance changed");
    for (std::size_t index{1}; index < samples.size(); ++index) {
        const double spacing = length(subtract(
            samples[index].position_metres,
            samples[index - 1U].position_metres));
        require(spacing <= 0.500001, "spline sample spacing exceeded 0.50 m");
        require(
            samples[index].distance_metres > samples[index - 1U].distance_metres,
            "spline distance must increase");
        require_near(samples[index].tangent.x, 1.0, "straight tangent x changed", 1.0e-6);
        require_near(samples[index].tangent.y, 0.0, "straight tangent y changed", 1.0e-6);
        require_near(samples[index].tangent.z, 0.0, "straight tangent z changed", 1.0e-6);
    }
}

void curved_spline_is_repeatable_and_bounded(const std::filesystem::path&)
{
    const auto first = sample_centripetal_catmull_rom(curved_route());
    const auto second = sample_centripetal_catmull_rom(curved_route());
    require(first.size() == second.size(), "same route changed sample count");
    require(first.size() > curved_route().control_points.size(), "curve was not subdivided");
    for (std::size_t index{}; index < first.size(); ++index) {
        require_near(first[index].position_metres.x, second[index].position_metres.x, "x drift");
        require_near(first[index].position_metres.y, second[index].position_metres.y, "y drift");
        require_near(first[index].position_metres.z, second[index].position_metres.z, "z drift");
        require_near(length(first[index].tangent), 1.0, "curve tangent not unit", 1.0e-6);
        if (index != 0U) {
            require(
                length(subtract(first[index].position_metres, first[index - 1U].position_metres))
                    <= 0.500001,
                "curved sample spacing exceeded contract");
        }
    }
}

void parallel_transport_frames_are_stable(const std::filesystem::path&)
{
    const auto frames = build_parallel_transport_frames(
        sample_centripetal_catmull_rom(curved_route()));
    require(frames.size() > 2U, "curved route must produce frames");
    for (std::size_t index{}; index < frames.size(); ++index) {
        const TransportFrame& frame = frames[index];
        require_near(length(frame.tangent), 1.0, "frame tangent not unit", 1.0e-6);
        require_near(length(frame.normal), 1.0, "frame normal not unit", 1.0e-6);
        require_near(length(frame.binormal), 1.0, "frame binormal not unit", 1.0e-6);
        require_near(dot(frame.tangent, frame.normal), 0.0, "tangent/normal not orthogonal", 1.0e-6);
        require_near(dot(frame.tangent, frame.binormal), 0.0, "tangent/binormal not orthogonal", 1.0e-6);
        require_near(dot(frame.normal, frame.binormal), 0.0, "normal/binormal not orthogonal", 1.0e-6);
        if (index != 0U) {
            require(
                dot(frames[index - 1U].normal, frame.normal) >= -1.0e-8,
                "parallel-transport frame flipped");
        }
    }
}

void vertical_initial_tangent_uses_stable_fallback_axis(const std::filesystem::path&)
{
    const std::vector<SplineSample> samples{
        {{0.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, 0.0},
        {{0.0, 1.0, 0.0}, {0.0, 1.0, 0.0}, 1.0},
    };
    const auto frames = build_parallel_transport_frames(samples);
    require(frames.size() == samples.size(), "vertical route lost a frame");
    require_near(length(frames.front().normal), 1.0, "fallback normal not unit");
    require_near(dot(frames.front().tangent, frames.front().normal), 0.0, "fallback not orthogonal");
}

void invalid_spline_inputs_are_rejected(const std::filesystem::path&)
{
    SplineRouteInput too_few = straight_route(SurfaceFacing::inward);
    too_few.control_points.resize(1U);
    require_throws<GeometryError>(
        [&] { validate_spline_route_input(too_few); },
        "at least two",
        "one control point must fail");

    SplineRouteInput duplicate = straight_route(SurfaceFacing::inward);
    duplicate.control_points[1] = duplicate.control_points[0];
    require_throws<GeometryError>(
        [&] { validate_spline_route_input(duplicate); },
        "adjacent",
        "duplicate adjacent control points must fail");

    SplineRouteInput narrow = straight_route(SurfaceFacing::inward);
    narrow.radius_millimetres = 1'000;
    require_throws<GeometryError>(
        [&] { validate_spline_route_input(narrow); },
        "clearance",
        "undersized tunnel must fail");

    SplineRouteInput coarse = straight_route(SurfaceFacing::inward);
    coarse.ring_side_count = 4U;
    require_throws<GeometryError>(
        [&] { validate_spline_route_input(coarse); },
        "ring sides",
        "four-sided tunnel must fail");

    SplineRouteInput steep = straight_route(SurfaceFacing::inward);
    steep.control_points = {{0, 0, 0}, {1'000, 2'000, 0}};
    require_throws<GeometryError>(
        [&] { static_cast<void>(sample_centripetal_catmull_rom(steep)); },
        "grade",
        "over-steep route must fail");

    SplineSamplingContract invalid_contract = spline_sampling_contract;
    invalid_contract.maximum_sample_spacing_millimetres = 0;
    require_throws<GeometryError>(
        [&] { validate_spline_sampling_contract(invalid_contract); },
        "positive",
        "zero sample spacing must fail");

    SplineRouteInput tight = curved_route();
    tight.control_points = {
        {0, 0, 0},
        {2'000, 250, 1'000},
        {4'000, 500, 0},
        {6'000, 750, -1'000},
        {8'000, 900, 0},
    };
    require_throws<GeometryError>(
        [&] { static_cast<void>(sample_centripetal_catmull_rom(tight)); },
        "curvature",
        "radius-incompatible curvature must fail");
}

void swept_ring_mesh_has_seams_uvs_and_facing(const std::filesystem::path&)
{
    const SplineRouteInput inward_input = straight_route(SurfaceFacing::inward);
    SplineRouteInput outward_input = inward_input;
    outward_input.facing = SurfaceFacing::outward;
    const MeshData inward = build_spline_ring_mesh(inward_input);
    const MeshData outward = build_spline_ring_mesh(outward_input);
    const MeshData curved = build_spline_ring_mesh(curved_route());
    validate_procedural_mesh(inward);
    validate_procedural_mesh(outward);
    validate_procedural_mesh(curved);
    require(inward.vertices.size() == outward.vertices.size(), "facing changed vertex count");
    require(inward.indices.size() == outward.indices.size(), "facing changed index count");

    const std::size_t vertices_per_ring = inward_input.ring_side_count + 1U;
    require(inward.vertices.size() % vertices_per_ring == 0U, "ring vertices misaligned");
    for (std::size_t ring{}; ring < inward.vertices.size() / vertices_per_ring; ++ring) {
        const Vertex& first = inward.vertices[ring * vertices_per_ring];
        const Vertex& seam = inward.vertices[ring * vertices_per_ring + inward_input.ring_side_count];
        require(first.position == seam.position, "ring seam position cracked");
        require(first.normal == seam.normal, "ring seam normal cracked");
        require_near(first.texture_coordinates[1], 0.0, "ring UV must begin at zero");
        require_near(seam.texture_coordinates[1], 1.0, "ring UV must end at one");
    }

    const Vertex& inward_vertex = inward.vertices.front();
    const GeometryVector3 inward_radial{
        inward_vertex.position[0],
        inward_vertex.position[1],
        inward_vertex.position[2],
    };
    const GeometryVector3 inward_normal{
        inward_vertex.normal[0],
        inward_vertex.normal[1],
        inward_vertex.normal[2],
    };
    require(dot(inward_radial, inward_normal) < 0.0, "cave normal must face inward");
    const GeometryVector3 outward_normal{
        outward.vertices.front().normal[0],
        outward.vertices.front().normal[1],
        outward.vertices.front().normal[2],
    };
    require(dot(inward_radial, outward_normal) > 0.0, "prop normal must face outward");
}

void mesh_builder_enforces_indices_and_budget(const std::filesystem::path&)
{
    const Vertex first{{0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, {0.0F, 0.0F}};
    const Vertex second{{1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, {1.0F, 0.0F}};
    const Vertex third{{0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, {0.0F, 1.0F}};
    MeshBuilder builder{3U};
    const auto first_index = builder.append_vertex(first);
    const auto second_index = builder.append_vertex(second);
    const auto third_index = builder.append_vertex(third);
    require_throws<GeometryError>(
        [&] { static_cast<void>(builder.append_vertex(first)); },
        "budget",
        "builder must enforce vertex budget");
    builder.append_triangle(first_index, second_index, third_index);
    validate_procedural_mesh(builder.finish());

    MeshBuilder invalid_builder{3U};
    static_cast<void>(invalid_builder.append_vertex(first));
    require_throws<GeometryError>(
        [&] { invalid_builder.append_triangle(0U, 1U, 2U); },
        "out-of-range",
        "builder must reject unavailable indices");
}

void geometry_fingerprint_is_stable_and_sensitive(const std::filesystem::path&)
{
    const SplineRouteInput input = curved_route();
    const std::uint64_t first = geometry_contract_fingerprint({123'456'789U}, input);
    const std::uint64_t second = geometry_contract_fingerprint({123'456'789U}, input);
    require(first == second, "same integer contract changed fingerprint");
    if (first != 0x10A061461068C4CEULL) {
        throw GeometryTestFailure{
            "fixed route changed geometry contract fingerprint: got "
            + format_fingerprint(first)};
    }

    SplineRouteInput changed = input;
    changed.control_points[2].z_millimetres += 1;
    require(
        geometry_contract_fingerprint({123'456'789U}, changed) != first,
        "integer geometry change did not change fingerprint");
}

void bounds_margin_detects_separation(const std::filesystem::path&)
{
    const AxisAlignedBounds left{{0.0, 0.0, 0.0}, {1.0, 1.0, 1.0}};
    const AxisAlignedBounds near{{1.5, 0.0, 0.0}, {2.5, 1.0, 1.0}};
    const AxisAlignedBounds far{{4.1, 0.0, 0.0}, {5.1, 1.0, 1.0}};
    require(!separated_by_margin(left, near, 3.0), "near bounds escaped safety margin");
    require(separated_by_margin(left, far, 3.0), "far bounds failed safety margin");
    require(!separated_by_margin(left, far, -1.0), "negative margin must not validate");

    const MeshData mesh = build_spline_ring_mesh(straight_route(SurfaceFacing::outward));
    const AxisAlignedBounds generated = mesh_bounds(mesh);
    require(generated.minimum_metres.x <= 0.0, "mesh minimum x invalid");
    require(generated.maximum_metres.x >= 6.0, "mesh maximum x invalid");
}

}  // namespace

std::vector<TestCase> geometry_test_cases()
{
    return {
        {"geometry contracts match blueprint", locked_contract_values_match_blueprint},
        {"straight spline endpoints and spacing", straight_spline_has_exact_endpoints_and_spacing},
        {"curved spline is repeatable", curved_spline_is_repeatable_and_bounded},
        {"parallel-transport frames are stable", parallel_transport_frames_are_stable},
        {"vertical frame initialization is stable", vertical_initial_tangent_uses_stable_fallback_axis},
        {"invalid spline inputs are rejected", invalid_spline_inputs_are_rejected},
        {"swept ring mesh has valid seams and facing", swept_ring_mesh_has_seams_uvs_and_facing},
        {"mesh builder enforces contracts", mesh_builder_enforces_indices_and_budget},
        {"geometry contract fingerprint is stable", geometry_fingerprint_is_stable_and_sensitive},
        {"bounds enforce separation margin", bounds_margin_detects_separation},
    };
}

}  // namespace crystalbound::test
