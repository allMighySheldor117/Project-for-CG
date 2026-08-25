#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "crystalbound/Generation.hpp"
#include "crystalbound/MeshData.hpp"

namespace crystalbound {

struct IntegerPoint3 {
    std::int32_t x_millimetres{};
    std::int32_t y_millimetres{};
    std::int32_t z_millimetres{};
};

constexpr bool operator==(const IntegerPoint3& left, const IntegerPoint3& right) noexcept
{
    return left.x_millimetres == right.x_millimetres
        && left.y_millimetres == right.y_millimetres
        && left.z_millimetres == right.z_millimetres;
}

struct GeometryVector3 {
    double x{};
    double y{};
    double z{};
};

struct SplineSamplingContract {
    std::int32_t maximum_sample_spacing_millimetres{};
    std::int32_t maximum_chord_error_millimetres{};
    std::int32_t maximum_overshoot_millimetres{};
    std::int32_t maximum_grade_millidegrees{};
    std::int32_t maximum_frame_turn_millidegrees{};
    std::uint32_t maximum_subdivision_depth{};
};

inline constexpr SplineSamplingContract spline_sampling_contract{
    500,
    50,
    1'000,
    35'000,
    90'000,
    16U,
};

struct GeometryBudgets {
    std::uint32_t maximum_static_vertices{};
    std::uint32_t maximum_opaque_draw_calls{};
    std::uint32_t maximum_point_lights{};
    std::uint32_t maximum_particles{};
    std::uint32_t maximum_transparent_draw_calls{};
};

inline constexpr GeometryBudgets geometry_budgets{
    250'000U,
    200U,
    8U,
    128U,
    16U,
};

struct GeometrySpatialContract {
    std::int32_t chamber_safety_separation_millimetres{};
};

inline constexpr GeometrySpatialContract geometry_spatial_contract{
    3'000,
};

enum class SurfaceFacing : std::uint8_t {
    inward,
    outward,
};

struct SplineRouteInput {
    std::uint64_t stable_object_id{};
    std::vector<IntegerPoint3> control_points{};
    std::int32_t radius_millimetres{};
    std::uint32_t ring_side_count{};
    SurfaceFacing facing{SurfaceFacing::inward};
};

struct SplineSample {
    GeometryVector3 position_metres{};
    GeometryVector3 tangent{};
    double distance_metres{};
};

struct TransportFrame {
    GeometryVector3 position_metres{};
    GeometryVector3 tangent{};
    GeometryVector3 normal{};
    GeometryVector3 binormal{};
};

struct AxisAlignedBounds {
    GeometryVector3 minimum_metres{};
    GeometryVector3 maximum_metres{};
};

class GeometryError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class MeshBuilder final {
public:
    explicit MeshBuilder(
        std::size_t maximum_vertices = geometry_budgets.maximum_static_vertices);

    [[nodiscard]] std::uint32_t append_vertex(const Vertex& vertex);
    void append_triangle(std::uint32_t first, std::uint32_t second, std::uint32_t third);
    [[nodiscard]] MeshData finish();

private:
    std::size_t maximum_vertices_{};
    MeshData mesh_{};
};

void validate_spline_route_input(const SplineRouteInput& input);
void validate_spline_sampling_contract(const SplineSamplingContract& contract);
[[nodiscard]] std::vector<SplineSample> sample_centripetal_catmull_rom(
    const SplineRouteInput& input,
    const SplineSamplingContract& contract = spline_sampling_contract);
[[nodiscard]] std::vector<TransportFrame> build_parallel_transport_frames(
    const std::vector<SplineSample>& samples);
[[nodiscard]] MeshData build_spline_ring_mesh(
    const SplineRouteInput& input,
    const SplineSamplingContract& contract = spline_sampling_contract);
void validate_procedural_mesh(const MeshData& mesh);
[[nodiscard]] AxisAlignedBounds mesh_bounds(const MeshData& mesh);
[[nodiscard]] bool separated_by_margin(
    const AxisAlignedBounds& left,
    const AxisAlignedBounds& right,
    double margin_metres) noexcept;
[[nodiscard]] std::uint64_t geometry_contract_fingerprint(
    Seed effective_seed,
    const SplineRouteInput& input,
    const SplineSamplingContract& contract = spline_sampling_contract);

}  // namespace crystalbound
