#include "crystalbound/CaveScene.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "GeometryMath.hpp"
#include "crystalbound/AuthoredChamber.hpp"
#include "crystalbound/DeterministicRandom.hpp"

namespace crystalbound {
namespace {

using namespace geometry_detail;

constexpr std::int32_t chamber_radial_variation_millimetres{250};
constexpr std::int32_t tunnel_radius_millimetres{1'350};
constexpr std::uint32_t chamber_side_count{16U};
constexpr std::uint32_t tunnel_side_count{8U};
constexpr std::int32_t tunnel_radial_variation_millimetres{80};
constexpr std::int32_t bridge_width_millimetres{1'900};
constexpr std::int32_t bridge_rail_height_millimetres{850};
constexpr double millimetres_per_metre{1'000.0};

constexpr std::uint64_t chamber_shell_domain{0x1000'0000'0000'0000ULL};
constexpr std::uint64_t chamber_floor_domain{0x2000'0000'0000'0000ULL};
constexpr std::uint64_t tunnel_piece_domain{0x3000'0000'0000'0000ULL};
constexpr std::uint64_t junction_piece_domain{0x4000'0000'0000'0000ULL};
constexpr std::uint64_t bridge_piece_domain{0x5000'0000'0000'0000ULL};
constexpr std::uint64_t natural_formation_domain{0x6000'0000'0000'0000ULL};

struct IntegerDirection2 {
    std::int32_t x{};
    std::int32_t z{};
};

constexpr std::array<IntegerDirection2, chamber_side_count> chamber_directions{{
    {10'000, 0},
    {9'239, 3'827},
    {7'071, 7'071},
    {3'827, 9'239},
    {0, 10'000},
    {-3'827, 9'239},
    {-7'071, 7'071},
    {-9'239, 3'827},
    {-10'000, 0},
    {-9'239, -3'827},
    {-7'071, -7'071},
    {-3'827, -9'239},
    {0, -10'000},
    {3'827, -9'239},
    {7'071, -7'071},
    {9'239, -3'827},
}};

[[nodiscard]] const ChamberNode& node_for(const TopologyData& topology, const NodeId id)
{
    const auto found = std::find_if(
        topology.nodes.begin(), topology.nodes.end(), [id](const ChamberNode& node) {
            return node.id == id;
        });
    if (found == topology.nodes.end()) {
        throw GeometryError{"Cave route references an unknown chamber."};
    }
    return *found;
}

[[nodiscard]] std::int32_t signed_sample(
    SplitMix64& random,
    const std::int32_t inclusive_limit)
{
    const std::uint64_t range{
        static_cast<std::uint64_t>(inclusive_limit) * 2U + 1U};
    return static_cast<std::int32_t>(random.bounded(range)) - inclusive_limit;
}

[[nodiscard]] std::uint64_t integer_square_root(std::uint64_t value) noexcept
{
    std::uint64_t result{};
    std::uint64_t bit{std::uint64_t{1} << 62U};
    while (bit > value) {
        bit >>= 2U;
    }
    while (bit != 0U) {
        if (value >= result + bit) {
            value -= result + bit;
            result = (result >> 1U) + bit;
        } else {
            result >>= 1U;
        }
        bit >>= 2U;
    }
    return result;
}

[[nodiscard]] std::int32_t rounded_ratio(
    const std::int64_t numerator,
    const std::int64_t positive_denominator)
{
    if (positive_denominator <= 0) {
        throw GeometryError{"Cave geometry received a non-positive scale denominator."};
    }
    const std::int64_t magnitude{numerator < 0 ? -numerator : numerator};
    const std::int64_t rounded{
        (magnitude + positive_denominator / 2) / positive_denominator};
    const std::int64_t signed_value{numerator < 0 ? -rounded : rounded};
    if (signed_value < std::numeric_limits<std::int32_t>::min()
        || signed_value > std::numeric_limits<std::int32_t>::max()) {
        throw GeometryError{"Cave geometry integer scaling overflowed."};
    }
    return static_cast<std::int32_t>(signed_value);
}

[[nodiscard]] IntegerDirection2 scaled_horizontal_direction(
    const std::int32_t delta_x,
    const std::int32_t delta_z,
    const std::int32_t scale_millimetres)
{
    const std::uint64_t squared{
        static_cast<std::uint64_t>(static_cast<std::int64_t>(delta_x) * delta_x)
        + static_cast<std::uint64_t>(static_cast<std::int64_t>(delta_z) * delta_z)};
    const std::uint64_t length{integer_square_root(squared)};
    if (length == 0U) {
        throw GeometryError{"Connected chamber anchors have no horizontal separation."};
    }
    return {
        rounded_ratio(
            static_cast<std::int64_t>(delta_x) * scale_millimetres,
            static_cast<std::int64_t>(length)),
        rounded_ratio(
            static_cast<std::int64_t>(delta_z) * scale_millimetres,
            static_cast<std::int64_t>(length)),
    };
}

[[nodiscard]] std::uint32_t nearest_chamber_side(
    const std::int32_t delta_x,
    const std::int32_t delta_z) noexcept
{
    std::uint32_t best_index{};
    std::int64_t best_dot{std::numeric_limits<std::int64_t>::min()};
    for (std::uint32_t index{}; index < chamber_directions.size(); ++index) {
        const IntegerDirection2 direction{chamber_directions[index]};
        const std::int64_t candidate{
            static_cast<std::int64_t>(delta_x) * direction.x
            + static_cast<std::int64_t>(delta_z) * direction.z};
        if (candidate > best_dot) {
            best_dot = candidate;
            best_index = index;
        }
    }
    return best_index;
}

[[nodiscard]] Vertex vertex_for(
    const GeometryVector3& position,
    const GeometryVector3& normal,
    const float u,
    const float v)
{
    if (!finite(position) || !finite(normal) || !std::isfinite(u) || !std::isfinite(v)) {
        throw GeometryError{"Cave mesh produced a non-finite vertex attribute."};
    }
    return {
        {static_cast<float>(position.x), static_cast<float>(position.y),
         static_cast<float>(position.z)},
        {static_cast<float>(normal.x), static_cast<float>(normal.y),
         static_cast<float>(normal.z)},
        {u, v},
    };
}

void append_triangle_face(
    MeshBuilder& builder,
    const GeometryVector3& first,
    GeometryVector3 second,
    GeometryVector3 third,
    const GeometryVector3& desired_normal)
{
    const GeometryVector3 normal{normalized(desired_normal, "Cave triangle normal")};
    if (dot(cross(subtract(second, first), subtract(third, first)), normal) < 0.0) {
        std::swap(second, third);
    }
    const std::uint32_t first_index{builder.append_vertex(vertex_for(first, normal, 0.0F, 0.0F))};
    const std::uint32_t second_index{builder.append_vertex(vertex_for(second, normal, 1.0F, 0.0F))};
    const std::uint32_t third_index{builder.append_vertex(vertex_for(third, normal, 0.5F, 1.0F))};
    builder.append_triangle(first_index, second_index, third_index);
}

void append_quad_face(
    MeshBuilder& builder,
    const GeometryVector3& first,
    GeometryVector3 second,
    GeometryVector3 third,
    GeometryVector3 fourth,
    const GeometryVector3& desired_normal)
{
    const GeometryVector3 normal{normalized(desired_normal, "Cave quad normal")};
    if (dot(cross(subtract(second, first), subtract(third, first)), normal) < 0.0) {
        std::swap(second, fourth);
    }
    const std::uint32_t first_index{builder.append_vertex(vertex_for(first, normal, 0.0F, 0.0F))};
    const std::uint32_t second_index{builder.append_vertex(vertex_for(second, normal, 1.0F, 0.0F))};
    const std::uint32_t third_index{builder.append_vertex(vertex_for(third, normal, 1.0F, 1.0F))};
    const std::uint32_t fourth_index{builder.append_vertex(vertex_for(fourth, normal, 0.0F, 1.0F))};
    builder.append_triangle(first_index, second_index, third_index);
    builder.append_triangle(first_index, third_index, fourth_index);
}

[[nodiscard]] MeshData build_chamber_floor(
    const CompiledChamberTemplate& chamber)
{
    MeshBuilder builder;
    for (const CompiledTemplateFloorPatch& patch : chamber.floor_patches) {
        if (!patch.walkable || patch.world_polygon_millimetres.size() < 3U) {
            continue;
        }
        GeometryVector3 center{};
        for (const TemplatePoint2 point : patch.world_polygon_millimetres) {
            center.x += point.x_millimetres / millimetres_per_metre;
            center.z += point.z_millimetres / millimetres_per_metre;
        }
        center.x /= patch.world_polygon_millimetres.size();
        center.y = patch.support_height_millimetres / millimetres_per_metre;
        center.z /= patch.world_polygon_millimetres.size();
        for (std::size_t index{};
             index < patch.world_polygon_millimetres.size(); ++index) {
            const TemplatePoint2 current_point{
                patch.world_polygon_millimetres[index]};
            const TemplatePoint2 next_point{patch.world_polygon_millimetres[
                (index + 1U) % patch.world_polygon_millimetres.size()]};
            const GeometryVector3 current{
                current_point.x_millimetres / millimetres_per_metre, center.y,
                current_point.z_millimetres / millimetres_per_metre};
            const GeometryVector3 next{
                next_point.x_millimetres / millimetres_per_metre, center.y,
                next_point.z_millimetres / millimetres_per_metre};
            append_triangle_face(
                builder, center, next, current, {0.0, 1.0, 0.0});
        }
    }
    return builder.finish();
}

[[nodiscard]] MeshData build_chamber_hazards(
    const CompiledChamberTemplate& chamber)
{
    MeshBuilder builder;
    for (const CompiledTemplateHazardVolume& hazard : chamber.hazards) {
        GeometryVector3 center{};
        for (const TemplatePoint2 point : hazard.world_polygon_millimetres) {
            center.x += point.x_millimetres / millimetres_per_metre;
            center.z += point.z_millimetres / millimetres_per_metre;
        }
        center.x /= hazard.world_polygon_millimetres.size();
        center.y = (hazard.maximum_y_millimetres - 20)
            / millimetres_per_metre;
        center.z /= hazard.world_polygon_millimetres.size();
        for (std::size_t index{};
             index < hazard.world_polygon_millimetres.size(); ++index) {
            const TemplatePoint2 current_point{
                hazard.world_polygon_millimetres[index]};
            const TemplatePoint2 next_point{hazard.world_polygon_millimetres[
                (index + 1U) % hazard.world_polygon_millimetres.size()]};
            append_triangle_face(builder, center,
                {next_point.x_millimetres / millimetres_per_metre, center.y,
                    next_point.z_millimetres / millimetres_per_metre},
                {current_point.x_millimetres / millimetres_per_metre, center.y,
                    current_point.z_millimetres / millimetres_per_metre},
                {0.0, 1.0, 0.0});
        }
    }
    return builder.finish();
}

[[nodiscard]] MeshData build_chamber_shell(
    const ChamberGeometryContract& chamber,
    const std::vector<PortalContract>& portals)
{
    MeshBuilder builder;
    const GeometryVector3 center{
        static_cast<double>(chamber.center_millimetres.x_millimetres)
            / millimetres_per_metre,
        static_cast<double>(chamber.center_millimetres.y_millimetres)
                / millimetres_per_metre
            + 2.0,
        static_cast<double>(chamber.center_millimetres.z_millimetres)
            / millimetres_per_metre,
    };
    for (const ChamberStructuralTriangle& triangle :
        chamber_structure_triangles(chamber, portals)) {
        const GeometryVector3 midpoint{multiply(
            add(add(triangle.first, triangle.second), triangle.third), 1.0 / 3.0)};
        append_triangle_face(
            builder,
            triangle.first,
            triangle.second,
            triangle.third,
            subtract(center, midpoint));
    }
    return builder.finish();
}

[[nodiscard]] MeshData build_general_chamber_formations(
    const ChamberGeometryContract& chamber,
    const Seed effective_seed)
{
    MeshBuilder builder;
    SplitMix64 random{make_substream(effective_seed.value,
        random_domain::decoration,
        natural_formation_domain ^ chamber.node_id.value)};
    const GeometryVector3 center{
        chamber.center_millimetres.x_millimetres / millimetres_per_metre,
        chamber.center_millimetres.y_millimetres / millimetres_per_metre,
        chamber.center_millimetres.z_millimetres / millimetres_per_metre};
    constexpr std::uint32_t spike_sides{5U};
    for (std::uint32_t ordinal{}; ordinal < 8U; ++ordinal) {
        const std::uint32_t chamber_side{(ordinal * 2U + chamber.node_id.value) % chamber.side_count};
        const GeometryVector3 wall{chamber_ring_position(chamber, 0U, chamber_side)};
        const GeometryVector3 base_center{add(center, multiply(subtract(wall, center), 0.82))};
        const double radius{0.24 + static_cast<double>(random.bounded(210U)) / 1'000.0};
        const double height{0.70 + static_cast<double>(random.bounded(801U)) / 1'000.0};
        const GeometryVector3 tip{base_center.x, base_center.y + height, base_center.z};
        for (std::uint32_t side{}; side < spike_sides; ++side) {
            const double first_angle{2.0 * 3.14159265358979323846 * side / spike_sides};
            const double second_angle{
                2.0 * 3.14159265358979323846 * (side + 1U) / spike_sides};
            const GeometryVector3 first{base_center.x + std::cos(first_angle) * radius,
                base_center.y, base_center.z + std::sin(first_angle) * radius};
            const GeometryVector3 second{base_center.x + std::cos(second_angle) * radius,
                base_center.y, base_center.z + std::sin(second_angle) * radius};
            const GeometryVector3 midpoint{multiply(add(add(first, second), tip), 1.0 / 3.0)};
            append_triangle_face(builder, first, second, tip,
                subtract(midpoint, base_center));
        }
    }
    return builder.finish();
}

[[nodiscard]] MeshData build_horseshoe_tunnel(const RouteGeometryContract& route)
{
    if (route.bridge || route.tunnel_clear_width_millimetres != 3'200
        || route.tunnel_side_height_millimetres != 2'200
        || route.tunnel_crown_height_millimetres != 4'000) {
        throw GeometryError{"Horseshoe tunnel received an invalid route profile."};
    }
    const std::vector<SplineSample> samples{
        sample_centripetal_catmull_rom(route.spline)};
    const std::vector<TransportFrame> frames{build_parallel_transport_frames(samples)};
    struct ProfilePoint {
        std::int32_t side_millimetres{};
        std::int32_t up_millimetres{};
    };
    constexpr std::array<ProfilePoint, 11> profile{{
        {-1'600, -1'350}, {1'600, -1'350}, {1'600, 850},
        {1'478, 1'539}, {1'131, 2'123}, {612, 2'510}, {0, 2'650},
        {-612, 2'510}, {-1'131, 2'123}, {-1'478, 1'539}, {-1'600, 850},
    }};
    if (frames.size() > geometry_budgets.maximum_static_vertices
            / (profile.size() * 4U)) {
        throw GeometryError{"Horseshoe tunnel exceeds the static vertex budget."};
    }
    const auto position = [](const TransportFrame& frame,
                              const ProfilePoint point) {
        return add(frame.position_metres,
            add(multiply(frame.binormal,
                    point.side_millimetres / millimetres_per_metre),
                multiply(frame.normal,
                    point.up_millimetres / millimetres_per_metre)));
    };
    MeshBuilder builder;
    for (std::size_t ring{}; ring + 1U < frames.size(); ++ring) {
        const GeometryVector3 interior{add(
            multiply(add(frames[ring].position_metres,
                         frames[ring + 1U].position_metres),
                0.5),
            multiply(add(frames[ring].normal, frames[ring + 1U].normal),
                0.15))};
        for (std::size_t side{}; side < profile.size(); ++side) {
            const std::size_t next{(side + 1U) % profile.size()};
            const GeometryVector3 first{position(frames[ring], profile[side])};
            const GeometryVector3 second{position(frames[ring + 1U], profile[side])};
            const GeometryVector3 third{position(frames[ring + 1U], profile[next])};
            const GeometryVector3 fourth{position(frames[ring], profile[next])};
            const GeometryVector3 midpoint{multiply(
                add(add(first, second), add(third, fourth)), 0.25)};
            append_quad_face(builder, first, second, third, fourth,
                subtract(interior, midpoint));
        }
    }
    return builder.finish();
}

void append_oriented_box(
    MeshBuilder& builder,
    const GeometryVector3& center,
    const GeometryVector3& axis_x,
    const GeometryVector3& axis_y,
    const GeometryVector3& axis_z,
    const GeometryVector3& half_extent)
{
    const GeometryVector3 x{multiply(axis_x, half_extent.x)};
    const GeometryVector3 y{multiply(axis_y, half_extent.y)};
    const GeometryVector3 z{multiply(axis_z, half_extent.z)};
    const auto corner = [&](const double sx, const double sy, const double sz) {
        return add(center, add(multiply(x, sx), add(multiply(y, sy), multiply(z, sz))));
    };
    append_quad_face(builder, corner(1, -1, -1), corner(1, 1, -1),
        corner(1, 1, 1), corner(1, -1, 1), axis_x);
    append_quad_face(builder, corner(-1, -1, 1), corner(-1, 1, 1),
        corner(-1, 1, -1), corner(-1, -1, -1), multiply(axis_x, -1.0));
    append_quad_face(builder, corner(-1, 1, -1), corner(-1, 1, 1),
        corner(1, 1, 1), corner(1, 1, -1), axis_y);
    append_quad_face(builder, corner(-1, -1, 1), corner(-1, -1, -1),
        corner(1, -1, -1), corner(1, -1, 1), multiply(axis_y, -1.0));
    append_quad_face(builder, corner(1, -1, 1), corner(1, 1, 1),
        corner(-1, 1, 1), corner(-1, -1, 1), axis_z);
    append_quad_face(builder, corner(-1, -1, -1), corner(-1, 1, -1),
        corner(1, 1, -1), corner(1, -1, -1), multiply(axis_z, -1.0));
}

[[nodiscard]] MeshData build_bridge(const RouteGeometryContract& route)
{
    const std::vector<SplineSample> samples{
        sample_centripetal_catmull_rom(route.spline)};
    const std::vector<TransportFrame> frames{build_parallel_transport_frames(samples)};
    MeshBuilder builder;
    const double half_width{
        static_cast<double>(route.bridge_width_millimetres) / millimetres_per_metre / 2.0};
    const double rail_height{
        static_cast<double>(route.bridge_rail_height_millimetres) / millimetres_per_metre};
    constexpr double plank_half_thickness{0.08};
    const double deck_surface_offset{
        static_cast<double>(route.spline.radius_millimetres) / millimetres_per_metre};
    for (std::size_t index{}; index + 1U < frames.size(); ++index) {
        const GeometryVector3 segment{subtract(
            frames[index + 1U].position_metres,
            frames[index].position_metres)};
        const double segment_length{length(segment)};
        const GeometryVector3 tangent{normalized(segment, "Bridge plank tangent")};
        const GeometryVector3 up{frames[index].normal};
        const GeometryVector3 side{normalized(cross(tangent, up), "Bridge plank side")};
        const GeometryVector3 route_midpoint{multiply(
            add(frames[index].position_metres, frames[index + 1U].position_metres), 0.5)};
        const GeometryVector3 deck_center{add(
            route_midpoint,
            multiply(up, -(deck_surface_offset + plank_half_thickness)))};
        append_oriented_box(
            builder,
            deck_center,
            side,
            up,
            tangent,
            {half_width, plank_half_thickness,
             std::max(0.02, segment_length * 0.47)});

        for (const double direction : {-1.0, 1.0}) {
            const GeometryVector3 rail_center{add(
                add(deck_center, multiply(side, direction * (half_width - 0.05))),
                multiply(up, rail_height))};
            append_oriented_box(
                builder,
                rail_center,
                side,
                up,
                tangent,
                {0.055, 0.055, segment_length * 0.5});
            if (index % 4U == 0U || index + 2U == frames.size()) {
                const GeometryVector3 post_center{add(
                    add(deck_center, multiply(side, direction * (half_width - 0.05))),
                    multiply(up, rail_height * 0.5))};
                append_oriented_box(
                    builder,
                    post_center,
                    side,
                    up,
                    tangent,
                    {0.065, rail_height * 0.5, 0.065});
            }
        }
    }
    return builder.finish();
}

[[nodiscard]] AxisAlignedBounds expanded_bounds(
    const AxisAlignedBounds& bounds,
    const double x,
    const double y,
    const double z) noexcept
{
    return {
        {bounds.minimum_metres.x - x, bounds.minimum_metres.y - y,
         bounds.minimum_metres.z - z},
        {bounds.maximum_metres.x + x, bounds.maximum_metres.y + y,
         bounds.maximum_metres.z + z},
    };
}

void append_piece(
    CaveSceneData& scene,
    const ScenePieceKind kind,
    const std::uint64_t stable_id,
    MeshData mesh,
    const std::array<float, 3>& albedo,
    const std::optional<MaterialKind> material_override = std::nullopt,
    const std::optional<NodeId> owner_chamber_id = std::nullopt)
{
    validate_procedural_mesh(mesh);
    const AxisAlignedBounds bounds{mesh_bounds(mesh)};
    if (mesh.vertices.size()
        > geometry_budgets.maximum_static_vertices - scene.static_vertex_count) {
        throw GeometryError{"Generated cave exceeds the static vertex budget."};
    }
    if (scene.opaque_draw_call_count >= geometry_budgets.maximum_opaque_draw_calls) {
        throw GeometryError{"Generated cave exceeds the opaque draw-call budget."};
    }
    scene.static_vertex_count += static_cast<std::uint32_t>(mesh.vertices.size());
    ++scene.opaque_draw_call_count;
    const MaterialKind material{material_override.value_or(
        kind == ScenePieceKind::bridge ? MaterialKind::wood : MaterialKind::rock)};
    scene.mesh_pieces.push_back(
        {kind, stable_id, std::move(mesh), bounds, material, albedo,
            owner_chamber_id});
}

[[nodiscard]] MaterialKind chamber_material(
    const ChamberTemplateRole role,
    const bool shell)
{
    switch (role) {
    case ChamberTemplateRole::fire:
        return MaterialKind::basalt_lava_crust;
    case ChamberTemplateRole::water:
        return shell ? MaterialKind::water_marble : MaterialKind::wet_rock;
    case ChamberTemplateRole::earth:
        return MaterialKind::soil_mineral;
    case ChamberTemplateRole::air:
        return shell ? MaterialKind::rock : MaterialKind::wood_bark;
    case ChamberTemplateRole::aether:
        return MaterialKind::aether_crystal;
    case ChamberTemplateRole::start:
    case ChamberTemplateRole::exit:
    case ChamberTemplateRole::neutral:
        return MaterialKind::rock;
    }
    throw GeometryError{"Unknown chamber material role."};
}

[[nodiscard]] ChamberGeometryContract make_chamber_contract(
    const ChamberNode& node,
    const Seed effective_seed)
{
    struct DimensionBand {
        std::int32_t minimum_radius{};
        std::int32_t maximum_radius{};
        std::int32_t minimum_height{};
        std::int32_t maximum_height{};
    };
    const auto dimensions_for = [](const ChamberNode& chamber) {
        if (chamber.role == ChamberRole::start) {
            return DimensionBand{9'000, 10'000, 5'600, 6'400};
        }
        if (chamber.role == ChamberRole::exit) {
            return DimensionBand{9'500, 10'500, 5'900, 6'700};
        }
        if (chamber.role != ChamberRole::elemental || !chamber.element.has_value()) {
            return DimensionBand{8'800, 9'800, 5'500, 6'300};
        }
        switch (*chamber.element) {
            case Element::fire:
                return DimensionBand{32'000, 33'000, 23'000, 24'000};
            case Element::water:
                return DimensionBand{9'300, 10'300, 5'600, 6'500};
            case Element::earth:
                return DimensionBand{24'000, 25'000, 13'000, 14'000};
            case Element::air:
                return DimensionBand{25'000, 26'000, 22'000, 23'000};
            case Element::aether:
                return DimensionBand{8'900, 9'900, 6'000, 7'000};
        }
        throw GeometryError{"Unknown elemental chamber dimension contract."};
    };
    const auto identity_for = [](const ChamberNode& chamber) {
        if (chamber.role != ChamberRole::elemental || !chamber.element.has_value()) {
            return ChamberStructuralIdentity{};
        }
        switch (*chamber.element) {
            case Element::fire:
                return ChamberStructuralIdentity{
                    ChamberFloorMorphology::fractured_terraces,
                    ChamberShellSilhouette::jagged,
                    ChamberEntranceFraming::fractured,
                    ChamberLandmarkAnchor::lava_terrace,
                    1U};
            case Element::water:
                return ChamberStructuralIdentity{
                    ChamberFloorMorphology::eroded_banks,
                    ChamberShellSilhouette::flowing,
                    ChamberEntranceFraming::rounded,
                    ChamberLandmarkAnchor::mist_basin,
                    2U};
            case Element::earth:
                return ChamberStructuralIdentity{
                    ChamberFloorMorphology::grounded_shelves,
                    ChamberShellSilhouette::massive,
                    ChamberEntranceFraming::pillars,
                    ChamberLandmarkAnchor::stone_columns,
                    3U};
            case Element::air:
                return ChamberStructuralIdentity{
                    ChamberFloorMorphology::open_spans,
                    ChamberShellSilhouette::soaring,
                    ChamberEntranceFraming::timbered,
                    ChamberLandmarkAnchor::suspended_bridge,
                    4U};
            case Element::aether:
                return ChamberStructuralIdentity{
                    ChamberFloorMorphology::asymmetric_dais,
                    ChamberShellSilhouette::vaulted,
                    ChamberEntranceFraming::arched,
                    ChamberLandmarkAnchor::crystal_arch,
                    5U};
        }
        throw GeometryError{"Unknown elemental chamber identity contract."};
    };
    const auto ring_scales_for = [](const ChamberStructuralIdentity& identity) {
        switch (identity.shell) {
            case ChamberShellSilhouette::jagged:
                return std::array<std::int32_t, 5U>{1'000, 1'020, 870, 600, 300};
            case ChamberShellSilhouette::flowing:
                return std::array<std::int32_t, 5U>{1'000, 980, 920, 680, 360};
            case ChamberShellSilhouette::massive:
                return std::array<std::int32_t, 5U>{1'000, 1'040, 960, 620, 330};
            case ChamberShellSilhouette::soaring:
                return std::array<std::int32_t, 5U>{1'000, 960, 800, 520, 260};
            case ChamberShellSilhouette::vaulted:
                return std::array<std::int32_t, 5U>{1'000, 1'010, 840, 560, 280};
            case ChamberShellSilhouette::balanced:
                return std::array<std::int32_t, 5U>{1'000, 1'000, 880, 620, 320};
        }
        throw GeometryError{"Unknown chamber shell silhouette."};
    };
    SplitMix64 random{make_substream(
        effective_seed.value, random_domain::geometry, node.id.value)};
    const DimensionBand band{dimensions_for(node)};
    const ChamberStructuralIdentity identity{identity_for(node)};
    const std::int32_t base_radius{band.minimum_radius
        + static_cast<std::int32_t>(random.bounded(
            static_cast<std::uint64_t>(band.maximum_radius - band.minimum_radius + 1)))};
    const std::int32_t height{band.minimum_height
        + static_cast<std::int32_t>(random.bounded(
            static_cast<std::uint64_t>(band.maximum_height - band.minimum_height + 1)))};
    ChamberGeometryContract chamber{
        node.id,
        {node.anchor.x_millimetres, node.anchor.elevation_millimetres,
         node.anchor.z_millimetres},
        base_radius,
        height,
        chamber_side_count,
        {},
        0,
        identity,
        {},
    };
    chamber.radial_offsets_millimetres.reserve(chamber.side_count);
    for (std::uint32_t side{}; side < chamber.side_count; ++side) {
        chamber.radial_offsets_millimetres.push_back(
            signed_sample(random, chamber_radial_variation_millimetres));
    }
    chamber.radial_offsets_millimetres[
        static_cast<std::size_t>(random.bounded(chamber.side_count))]
        = -chamber_radial_variation_millimetres;
    chamber.minimum_safe_ring_radius_millimetres = base_radius
        + *std::min_element(chamber.radial_offsets_millimetres.begin(),
            chamber.radial_offsets_millimetres.end());

    const std::array<std::int32_t, 5U> ring_scales{ring_scales_for(identity)};
    const std::array<std::int32_t, 5U> ring_heights{
        0,
        height * 28 / 100,
        height * 62 / 100,
        height * 84 / 100,
        height,
    };
    chamber.rings.reserve(ring_scales.size());
    for (std::size_t ring{}; ring < ring_scales.size(); ++ring) {
        ChamberRingContract contract;
        contract.height_millimetres = ring_heights[ring];
        contract.radii_millimetres.reserve(chamber.side_count);
        for (std::uint32_t side{}; side < chamber.side_count; ++side) {
            const std::int32_t floor_radius{
                base_radius + chamber.radial_offsets_millimetres[side]};
            const std::int32_t profile_variation{ring == 0U ? 0
                : signed_sample(random, 120 + static_cast<std::int32_t>(ring) * 25)};
            contract.radii_millimetres.push_back(std::max(
                1'500,
                floor_radius * ring_scales[ring] / 1'000 + profile_variation));
        }
        chamber.rings.push_back(std::move(contract));
    }
    return chamber;
}

[[nodiscard]] std::int32_t maximum_chamber_radius(
    const ChamberGeometryContract& chamber)
{
    std::int32_t maximum{};
    for (const ChamberRingContract& ring : chamber.rings) {
        if (!ring.radii_millimetres.empty()) {
            maximum = std::max(maximum,
                *std::max_element(ring.radii_millimetres.begin(),
                    ring.radii_millimetres.end()));
        }
    }
    return maximum;
}

void validate_chamber_spacing(const std::vector<ChamberGeometryContract>& chambers)
{
    for (std::size_t left{}; left < chambers.size(); ++left) {
        for (std::size_t right{left + 1U}; right < chambers.size(); ++right) {
            const std::int64_t dx{
                static_cast<std::int64_t>(chambers[right].center_millimetres.x_millimetres)
                - chambers[left].center_millimetres.x_millimetres};
            const std::int64_t dz{
                static_cast<std::int64_t>(chambers[right].center_millimetres.z_millimetres)
                - chambers[left].center_millimetres.z_millimetres};
            const std::int64_t required{
                maximum_chamber_radius(chambers[left])
                + maximum_chamber_radius(chambers[right])
                + geometry_spatial_contract.chamber_safety_separation_millimetres};
            if (dx * dx + dz * dz < required * required) {
                throw GeometryError{"Chamber bounds violate the locked safety separation."};
            }
        }
    }
}

[[nodiscard]] bool route_has_alternate_path(
    const TopologyData& topology,
    const Edge removed)
{
    std::vector<NodeId> pending{removed.first};
    std::vector<NodeId> visited{removed.first};
    for (std::size_t index{}; index < pending.size(); ++index) {
        const NodeId current{pending[index]};
        for (const Edge edge : topology.edges) {
            if (edge == removed || (edge.first != current && edge.second != current)) {
                continue;
            }
            const NodeId next{edge.first == current ? edge.second : edge.first};
            if (next == removed.second) {
                return true;
            }
            if (std::find(visited.begin(), visited.end(), next) == visited.end()) {
                visited.push_back(next);
                pending.push_back(next);
            }
        }
    }
    return false;
}

[[nodiscard]] bool edges_share_endpoint(
    const Edge left,
    const Edge right) noexcept
{
    return left.first == right.first || left.first == right.second
        || left.second == right.first || left.second == right.second;
}

[[nodiscard]] std::vector<Edge> select_bridge_routes(
    const TopologyData& topology,
    const Seed effective_seed)
{
    if (topology.routes.empty()) {
        throw GeometryError{"A cave requires at least one route for its bridge."};
    }
    std::vector<Edge> cycle_edges;
    for (const RouteDescriptor& route : topology.routes) {
        if (route_has_alternate_path(topology, route.edge)) {
            cycle_edges.push_back(route.edge);
        }
    }
    if (cycle_edges.empty()) {
        throw GeometryError{"A cave bridge requires an alternate-route cycle."};
    }
    Edge selected{cycle_edges.front()};
    std::uint64_t selected_score{std::numeric_limits<std::uint64_t>::max()};
    for (const Edge edge : cycle_edges) {
        SplitMix64 random{make_substream(
            effective_seed.value,
            random_domain::geometry,
            stable_edge_id(edge) ^ bridge_piece_domain)};
        const std::uint64_t score{random.next()};
        if (score < selected_score
            || (score == selected_score && edge < selected)) {
            selected = edge;
            selected_score = score;
        }
    }
    std::vector<Edge> bridges{selected};
    SplitMix64 decision{make_substream(effective_seed.value,
        second_bridge_decision_domain, stable_edge_id(selected))};
    if (decision.bounded(4U) != 0U || topology.edges.size() < 6U) {
        return bridges;
    }
    std::optional<Edge> second;
    std::uint64_t second_score{std::numeric_limits<std::uint64_t>::max()};
    for (const Edge candidate : cycle_edges) {
        if (candidate == selected || edges_share_endpoint(candidate, selected)) {
            continue;
        }
        const std::uint64_t key{stable_edge_id(candidate)
            ^ rotate_left_64(stable_edge_id(selected))};
        SplitMix64 score_random{make_substream(
            effective_seed.value, second_bridge_score_domain, key)};
        const std::uint64_t score{score_random.next()};
        if (!second.has_value() || score < second_score
            || (score == second_score && candidate < *second)) {
            second = candidate;
            second_score = score;
        }
    }
    if (second.has_value()) {
        bridges.push_back(*second);
        std::sort(bridges.begin(), bridges.end());
    }
    return bridges;
}

[[nodiscard]] PortalContract make_portal(
    const ChamberGeometryContract& chamber,
    const CompiledChamberTemplate& compiled,
    const ChamberNode&,
    const Edge edge)
{
    const auto socket{std::find_if(compiled.sockets.begin(),
        compiled.sockets.end(), [&](const CompiledTemplateSocket& candidate) {
            return candidate.active && candidate.route == edge;
        })};
    if (socket == compiled.sockets.end()) {
        throw GeometryError{"Route has no canonical active chamber socket."};
    }
    const std::uint32_t opening_side{nearest_chamber_side(
        socket->world_outward_direction_million.x_millimetres,
        socket->world_outward_direction_million.z_millimetres)};
    const IntegerDirection2 inward{
        rounded_ratio(-static_cast<std::int64_t>(
                          socket->world_outward_direction_million.x_millimetres)
                * 1'000,
            1'000'000),
        rounded_ratio(-static_cast<std::int64_t>(
                          socket->world_outward_direction_million.z_millimetres)
                * 1'000,
            1'000'000)};
    const IntegerPoint3 center{
        socket->world_vestibule_outer_millimetres.x_millimetres,
        chamber.center_millimetres.y_millimetres + tunnel_radius_millimetres
            + (compiled.role == ChamberTemplateRole::water
                    ? authored_water_landing_height_millimetres
                    : compiled.role == ChamberTemplateRole::fire
                        ? authored_fire_landing_height_millimetres
                        : 0),
        socket->world_vestibule_outer_millimetres.z_millimetres};
    const std::int64_t dx{static_cast<std::int64_t>(
        center.x_millimetres - chamber.center_millimetres.x_millimetres)};
    const std::int64_t dz{static_cast<std::int64_t>(
        center.z_millimetres - chamber.center_millimetres.z_millimetres)};
    const std::int32_t portal_radius{static_cast<std::int32_t>(integer_square_root(
        static_cast<std::uint64_t>(dx * dx)
        + static_cast<std::uint64_t>(dz * dz)))};
    const std::int32_t usable_core_radius{
        chamber.minimum_safe_ring_radius_millimetres
        - movement_envelope.capsule_radius_millimetres
        - movement_envelope.safety_margin_millimetres};
    const std::int32_t approach_depth{[&] {
        if (compiled.role == ChamberTemplateRole::aether
            || compiled.role == ChamberTemplateRole::exit) {
            return authored_terminal_junction_depth_millimetres;
        }
        if (compiled.role == ChamberTemplateRole::water) {
            return route_junction_depth_millimetres;
        }
        return std::max(route_junction_depth_millimetres,
            portal_radius - usable_core_radius
                + route_join_overlap_millimetres);
    }()};
    return {
        chamber.node_id,
        edge,
        center,
        {inward.x, 0, inward.z},
        opening_side,
        approach_depth,
    };
}

[[nodiscard]] const PortalContract& portal_for(
    const std::vector<PortalContract>& portals,
    const NodeId chamber,
    const Edge edge)
{
    const auto found = std::find_if(
        portals.begin(), portals.end(), [&](const PortalContract& portal) {
            return portal.chamber_id == chamber && portal.route == edge;
        });
    if (found == portals.end()) {
        throw GeometryError{"Cave route is missing a declared portal."};
    }
    return *found;
}

[[nodiscard]] RouteGeometryContract make_route_contract(
    const RouteDescriptor& descriptor,
    const std::vector<PortalContract>& portals,
    const std::vector<Edge>& bridge_edges,
    const Seed effective_seed)
{
    const PortalContract& first{portal_for(portals, descriptor.edge.first, descriptor.edge)};
    const PortalContract& second{portal_for(portals, descriptor.edge.second, descriptor.edge)};
    const IntegerPoint3 start{first.center_millimetres};
    const IntegerPoint3 finish{second.center_millimetres};
    const std::int32_t dx{finish.x_millimetres - start.x_millimetres};
    const std::int32_t dz{finish.z_millimetres - start.z_millimetres};
    const std::uint64_t horizontal_distance{integer_square_root(
        static_cast<std::uint64_t>(static_cast<std::int64_t>(dx) * dx)
        + static_cast<std::uint64_t>(static_cast<std::int64_t>(dz) * dz))};
    if (horizontal_distance < 2'500U) {
        throw GeometryError{"Route portals are too close for a safe curved connection."};
    }
    const IntegerDirection2 perpendicular{
        scaled_horizontal_direction(-dz, dx, 1'000)};
    std::int32_t bend{0};
    bend = std::min<std::int32_t>(bend, 900);
    bend = std::min<std::int32_t>(
        bend,
        static_cast<std::int32_t>(horizontal_distance / 6U));
    const bool negative_bend{descriptor.lateral_offset_millimetres < 0
        || (descriptor.lateral_offset_millimetres == 0
            && descriptor.heading_millidegrees >= 180'000)};
    if (negative_bend) {
        bend = -bend;
    }
    const std::int32_t lateral_x{rounded_ratio(
        static_cast<std::int64_t>(perpendicular.x) * bend, 1'000)};
    const std::int32_t lateral_z{rounded_ratio(
        static_cast<std::int64_t>(perpendicular.z) * bend, 1'000)};
    const bool bridge{std::find(bridge_edges.begin(), bridge_edges.end(),
        descriptor.edge) != bridge_edges.end()};
    const std::int32_t vertical_bend{bridge ? -500 : 0};
    const IntegerPoint3 first_middle{
        start.x_millimetres + dx / 3 + lateral_x,
        start.y_millimetres + (finish.y_millimetres - start.y_millimetres) / 3
            + vertical_bend,
        start.z_millimetres + dz / 3 + lateral_z,
    };
    const IntegerPoint3 second_middle{
        start.x_millimetres + dx * 2 / 3 + lateral_x,
        start.y_millimetres + (finish.y_millimetres - start.y_millimetres) * 2 / 3
            + vertical_bend,
        start.z_millimetres + dz * 2 / 3 + lateral_z,
    };

    RouteGeometryContract route{
        descriptor.edge,
        {stable_edge_id(descriptor.edge),
         {start, first_middle, second_middle, finish},
         tunnel_radius_millimetres,
         tunnel_side_count,
         SurfaceFacing::inward},
        {},
        bridge,
        tunnel_clear_width_millimetres,
        tunnel_side_height_millimetres,
        tunnel_crown_height_millimetres,
        route_junction_depth_millimetres,
        route_join_overlap_millimetres,
        bridge_width_millimetres,
        bridge_rail_height_millimetres,
    };
    SplitMix64 random{make_substream(
        effective_seed.value,
        random_domain::geometry,
        stable_edge_id(descriptor.edge))};
    route.ring_offsets_millimetres.reserve(route.spline.ring_side_count);
    for (std::uint32_t side{}; side < route.spline.ring_side_count; ++side) {
        route.ring_offsets_millimetres.push_back(
            signed_sample(random, tunnel_radial_variation_millimetres));
    }
    return route;
}

[[nodiscard]] bool shares_endpoint(const Edge& left, const Edge& right) noexcept
{
    return left.first == right.first || left.first == right.second
        || left.second == right.first || left.second == right.second;
}

void validate_route_spatial_separation(
    const std::vector<RouteGeometryContract>& routes,
    const std::vector<ChamberGeometryContract>& chambers)
{
    std::vector<std::vector<SplineSample>> samples;
    samples.reserve(routes.size());
    for (const RouteGeometryContract& route : routes) {
        samples.push_back(sample_centripetal_catmull_rom(route.spline));
    }
    for (std::size_t route_index{}; route_index < routes.size(); ++route_index) {
        const RouteGeometryContract& route{routes[route_index]};
        for (const ChamberGeometryContract& chamber : chambers) {
            if (chamber.node_id == route.edge.first || chamber.node_id == route.edge.second) {
                continue;
            }
            const double required{
                static_cast<double>(maximum_chamber_radius(chamber)
                    + route.spline.radius_millimetres)
                / millimetres_per_metre};
            const GeometryVector3 center{from_millimetres(chamber.center_millimetres)};
            for (const SplineSample& sample : samples[route_index]) {
                const double horizontal_squared{
                    (sample.position_metres.x - center.x)
                        * (sample.position_metres.x - center.x)
                    + (sample.position_metres.z - center.z)
                        * (sample.position_metres.z - center.z)};
                if (horizontal_squared < required * required) {
                    throw GeometryError{
                        "A route intersects a chamber that is not one of its endpoints."};
                }
            }
        }
        for (std::size_t other{route_index + 1U}; other < routes.size(); ++other) {
            if (shares_endpoint(route.edge, routes[other].edge)) {
                continue;
            }
            const double required{
                static_cast<double>(route.spline.radius_millimetres
                    + routes[other].spline.radius_millimetres)
                / millimetres_per_metre};
            for (const SplineSample& first : samples[route_index]) {
                for (const SplineSample& second : samples[other]) {
                    if (squared_length(subtract(first.position_metres, second.position_metres))
                        < required * required) {
                        throw GeometryError{"Two unrelated routes intersect."};
                    }
                }
            }
        }
    }
}

[[nodiscard]] MeshData build_junction_mesh(
    const PortalContract& portal,
    const RouteGeometryContract& route)
{
    const std::int32_t approach_depth{route.bridge
            ? route.vestibule_length_millimetres
            : portal.approach_depth_millimetres};
    const IntegerPoint3 outer{
        portal.center_millimetres.x_millimetres
            - rounded_ratio(
                static_cast<std::int64_t>(portal.inward_direction_millimetres.x_millimetres)
                    * route.join_overlap_millimetres,
                1'000),
        portal.center_millimetres.y_millimetres,
        portal.center_millimetres.z_millimetres
            - rounded_ratio(
                static_cast<std::int64_t>(portal.inward_direction_millimetres.z_millimetres)
                    * route.join_overlap_millimetres,
                1'000),
    };
    const IntegerPoint3 inner{
        portal.center_millimetres.x_millimetres
            + rounded_ratio(
                static_cast<std::int64_t>(portal.inward_direction_millimetres.x_millimetres)
                    * approach_depth,
                1'000),
        portal.center_millimetres.y_millimetres,
        portal.center_millimetres.z_millimetres
            + rounded_ratio(
                static_cast<std::int64_t>(portal.inward_direction_millimetres.z_millimetres)
                    * approach_depth,
                1'000),
    };
    if (!route.bridge) {
        RouteGeometryContract vestibule{route};
        vestibule.spline = {stable_edge_id(portal.route) ^ portal.chamber_id.value,
            {outer, inner}, tunnel_radius_millimetres,
            tunnel_side_count, SurfaceFacing::inward};
        vestibule.ring_offsets_millimetres.assign(tunnel_side_count, 0);
        return build_horseshoe_tunnel(vestibule);
    }

    const GeometryVector3 first{from_millimetres(portal.center_millimetres)};
    const GeometryVector3 second{from_millimetres(inner)};
    const GeometryVector3 segment{subtract(first, second)};
    const double segment_length{length(segment)};
    const GeometryVector3 tangent{normalized(segment, "Bridge landing tangent")};
    const GeometryVector3 up{0.0, 1.0, 0.0};
    const GeometryVector3 side{normalized(cross(tangent, up), "Bridge landing side")};
    GeometryVector3 center{multiply(add(first, second), 0.5)};
    center.y -= tunnel_radius_millimetres / millimetres_per_metre + 0.08;
    MeshBuilder builder;
    append_oriented_box(builder, center, side, up, tangent,
        {route.bridge_width_millimetres / millimetres_per_metre / 2.0,
            0.08, segment_length / 2.0 + route.join_overlap_millimetres
                / millimetres_per_metre});
    return builder.finish();
}

void set_start_camera(
    CaveSceneData& scene,
    const TopologyData& topology)
{
    const auto start = std::find_if(
        topology.nodes.begin(), topology.nodes.end(), [](const ChamberNode& node) {
            return node.role == ChamberRole::start;
        });
    if (start == topology.nodes.end()) {
        throw GeometryError{"Generated cave has no Start chamber."};
    }
    const auto compiled{std::find_if(scene.compiled_chambers.begin(),
        scene.compiled_chambers.end(), [&](const CompiledChamberTemplate& chamber) {
            return chamber.chamber_id == start->id;
        })};
    if (compiled == scene.compiled_chambers.end()) {
        throw GeometryError{"Start chamber has no compiled template."};
    }
    const CompiledTemplateSocket* active_socket{};
    for (const CompiledTemplateSocket& socket : compiled->sockets) {
        if (socket.active && socket.route.has_value()
            && (active_socket == nullptr
                || *socket.route < *active_socket->route)) {
            active_socket = &socket;
        }
    }
    if (active_socket == nullptr) {
        throw GeometryError{"Start chamber has no canonical active route socket."};
    }
    scene.start_camera_position_metres = {
        static_cast<double>(start->anchor.x_millimetres) / millimetres_per_metre,
        static_cast<double>(start->anchor.elevation_millimetres
            + movement_envelope.camera_height_millimetres) / millimetres_per_metre,
        static_cast<double>(start->anchor.z_millimetres) / millimetres_per_metre,
    };
    scene.start_camera_forward = normalized(
        subtract(from_millimetres(
            {active_socket->world_origin_millimetres.x_millimetres,
                start->anchor.elevation_millimetres
                    + movement_envelope.camera_height_millimetres,
                active_socket->world_origin_millimetres.z_millimetres}),
            scene.start_camera_position_metres),
        "Start camera direction");
}

}  // namespace

CaveSceneData build_cave_scene(
    const TopologyData& topology,
    const Seed effective_seed)
{
    const std::vector<std::string> topology_errors{validate_topology(topology)};
    if (!topology_errors.empty()) {
        throw GeometryError{"Cave scene requires a validated topology."};
    }

    CaveSceneData scene;
    try {
        scene.template_socket_assignments = assign_template_sockets(topology);
    } catch (const std::exception& error) {
        throw GeometryError{std::string{"Template socket assignment failed: "}
            + error.what()};
    }
    scene.compiled_chambers = compile_chamber_templates(
        topology, scene.template_socket_assignments);
    scene.template_gameplay_fingerprint = template_gameplay_fingerprint(
        topology, scene.template_socket_assignments);
    scene.chambers.reserve(topology.nodes.size());
    for (const ChamberNode& node : topology.nodes) {
        scene.chambers.push_back(make_chamber_contract(node, effective_seed));
    }
    validate_chamber_spacing(scene.chambers);

    scene.portals.reserve(topology.routes.size() * 2U);
    for (const RouteDescriptor& route : topology.routes) {
        const ChamberNode& first{node_for(topology, route.edge.first)};
        const ChamberNode& second{node_for(topology, route.edge.second)};
        const auto first_geometry = std::find_if(
            scene.chambers.begin(), scene.chambers.end(), [&](const auto& chamber) {
                return chamber.node_id == first.id;
            });
        const auto second_geometry = std::find_if(
            scene.chambers.begin(), scene.chambers.end(), [&](const auto& chamber) {
                return chamber.node_id == second.id;
            });
        if (first_geometry == scene.chambers.end()
            || second_geometry == scene.chambers.end()) {
            throw GeometryError{"Portal construction could not find chamber geometry."};
        }
        const auto first_compiled{std::find_if(scene.compiled_chambers.begin(),
            scene.compiled_chambers.end(), [&](const CompiledChamberTemplate& chamber) {
                return chamber.chamber_id == first.id;
            })};
        const auto second_compiled{std::find_if(scene.compiled_chambers.begin(),
            scene.compiled_chambers.end(), [&](const CompiledChamberTemplate& chamber) {
                return chamber.chamber_id == second.id;
            })};
        if (first_compiled == scene.compiled_chambers.end()
            || second_compiled == scene.compiled_chambers.end()) {
            throw GeometryError{"Portal construction could not find compiled chamber data."};
        }
        scene.portals.push_back(make_portal(
            *first_geometry, *first_compiled, second, route.edge));
        scene.portals.push_back(make_portal(
            *second_geometry, *second_compiled, first, route.edge));
    }

    scene.bridge_routes.clear();
    scene.routes.reserve(topology.routes.size());
    for (const RouteDescriptor& descriptor : topology.routes) {
        scene.routes.push_back(make_route_contract(
            descriptor, scene.portals, scene.bridge_routes, effective_seed));
    }
    validate_route_spatial_separation(scene.routes, scene.chambers);

    for (const ChamberGeometryContract& chamber : scene.chambers) {
        const auto compiled{std::find_if(scene.compiled_chambers.begin(),
            scene.compiled_chambers.end(), [&](const CompiledChamberTemplate& item) {
                return item.chamber_id == chamber.node_id;
            })};
        if (compiled == scene.compiled_chambers.end()) {
            throw GeometryError{"Chamber is missing its compiled template."};
        }
        MeshData floor{build_chamber_floor(*compiled)};
        const AxisAlignedBounds floor_bounds{mesh_bounds(floor)};
        scene.colliders.push_back({
            ColliderKind::chamber_floor,
            chamber_floor_domain ^ chamber.node_id.value,
            expanded_bounds(floor_bounds, 0.0, 0.10, 0.0),
        });
        append_piece(
            scene,
            ScenePieceKind::chamber_floor,
            chamber_floor_domain ^ chamber.node_id.value,
            std::move(floor),
            compiled->role == ChamberTemplateRole::fire
                ? std::array<float, 3>{0.22F, 0.055F, 0.018F}
                : compiled->role == ChamberTemplateRole::water
                    ? std::array<float, 3>{0.10F, 0.24F, 0.29F}
                    : compiled->role == ChamberTemplateRole::earth
                        ? std::array<float, 3>{0.28F, 0.22F, 0.12F}
                        : compiled->role == ChamberTemplateRole::air
                            ? std::array<float, 3>{0.34F, 0.23F, 0.10F}
                            : compiled->role == ChamberTemplateRole::aether
                                ? std::array<float, 3>{0.30F, 0.12F, 0.42F}
                                : std::array<float, 3>{0.30F, 0.33F, 0.37F},
            chamber_material(compiled->role, false), chamber.node_id);

        if (!compiled->hazards.empty()) {
            append_piece(scene, ScenePieceKind::chamber_hazard,
                chamber_floor_domain ^ chamber.node_id.value ^ 0x48415A415244ULL,
                build_chamber_hazards(*compiled),
                compiled->role == ChamberTemplateRole::fire
                    ? std::array<float, 3>{0.95F, 0.16F, 0.02F}
                    : std::array<float, 3>{0.42F, 0.08F, 0.68F},
                compiled->role == ChamberTemplateRole::fire
                    ? MaterialKind::lava : MaterialKind::aether_crystal,
                chamber.node_id);
        }

        MeshData shell{build_chamber_shell(chamber, scene.portals)};
        const AxisAlignedBounds shell_bounds{mesh_bounds(shell)};
        scene.colliders.push_back({
            ColliderKind::chamber_boundary,
            chamber_shell_domain ^ chamber.node_id.value,
            shell_bounds,
        });
        append_piece(
            scene,
            ScenePieceKind::chamber_shell,
            chamber_shell_domain ^ chamber.node_id.value,
            std::move(shell),
            compiled->role == ChamberTemplateRole::fire
                ? std::array<float, 3>{0.18F, 0.045F, 0.018F}
                : compiled->role == ChamberTemplateRole::water
                    ? std::array<float, 3>{0.78F, 0.84F, 0.90F}
                    : compiled->role == ChamberTemplateRole::earth
                        ? std::array<float, 3>{0.30F, 0.24F, 0.14F}
                        : compiled->role == ChamberTemplateRole::aether
                            ? std::array<float, 3>{0.24F, 0.10F, 0.34F}
                            : std::array<float, 3>{0.36F, 0.39F, 0.44F},
            chamber_material(compiled->role, true), chamber.node_id);

        append_piece(scene, ScenePieceKind::natural_formation,
            natural_formation_domain ^ chamber.node_id.value,
            build_general_chamber_formations(chamber, effective_seed),
            compiled->role == ChamberTemplateRole::fire
                ? std::array<float, 3>{0.20F, 0.06F, 0.025F}
                : compiled->role == ChamberTemplateRole::water
                    ? std::array<float, 3>{0.11F, 0.21F, 0.23F}
                    : compiled->role == ChamberTemplateRole::earth
                        ? std::array<float, 3>{0.27F, 0.22F, 0.13F}
                        : std::array<float, 3>{0.29F, 0.32F, 0.34F},
            chamber_material(compiled->role, true), chamber.node_id);
    }

    for (const RouteGeometryContract& route : scene.routes) {
        if (!route.bridge) {
            MeshData tunnel{build_horseshoe_tunnel(route)};
            const AxisAlignedBounds tunnel_bounds{mesh_bounds(tunnel)};
            scene.colliders.push_back({
                ColliderKind::tunnel,
                tunnel_piece_domain ^ stable_edge_id(route.edge),
                tunnel_bounds,
            });
            append_piece(scene, ScenePieceKind::tunnel,
                tunnel_piece_domain ^ stable_edge_id(route.edge),
                std::move(tunnel), {0.33F, 0.36F, 0.41F});
        }

        if (route.bridge) {
            MeshData bridge{build_bridge(route)};
            const AxisAlignedBounds bridge_bounds{mesh_bounds(bridge)};
            scene.colliders.push_back({
                ColliderKind::bridge_deck,
                bridge_piece_domain ^ stable_edge_id(route.edge),
                expanded_bounds(bridge_bounds, 0.0, 0.10, 0.0),
            });
            scene.colliders.push_back({
                ColliderKind::bridge_rail,
                bridge_piece_domain ^ stable_edge_id(route.edge) ^ 1U,
                bridge_bounds,
            });
            AxisAlignedBounds fall_bounds{bridge_bounds};
            fall_bounds.minimum_metres.y -= 4.0;
            fall_bounds.maximum_metres.y = bridge_bounds.minimum_metres.y - 0.25;
            scene.colliders.push_back({
                ColliderKind::fall_region,
                bridge_piece_domain ^ stable_edge_id(route.edge) ^ 2U,
                fall_bounds,
            });
            append_piece(
                scene,
                ScenePieceKind::bridge,
                bridge_piece_domain ^ stable_edge_id(route.edge),
                std::move(bridge),
                {0.46F, 0.29F, 0.13F});
        }
    }

    for (const PortalContract& portal : scene.portals) {
        const auto route{std::find_if(scene.routes.begin(), scene.routes.end(),
            [&](const RouteGeometryContract& candidate) {
                return candidate.edge == portal.route;
            })};
        if (route == scene.routes.end()) {
            throw GeometryError{"Portal has no route variant for its vestibule."};
        }
        append_piece(
            scene,
            ScenePieceKind::junction,
            junction_piece_domain ^ stable_edge_id(portal.route)
                ^ (static_cast<std::uint64_t>(portal.chamber_id.value) << 32U),
            build_junction_mesh(portal, *route),
            {0.31F, 0.34F, 0.39F});
    }

    set_start_camera(scene, topology);
    scene.fingerprint = cave_scene_fingerprint(effective_seed, scene);
    std::vector<ElementalChamberSpatialContract> elemental_spatial;
    elemental_spatial.reserve(scene.chambers.size());
    for (const ChamberGeometryContract& chamber : scene.chambers) {
        const ChamberNode& node{node_for(topology, chamber.node_id)};
        if (node.role != ChamberRole::elemental) {
            continue;
        }
        ElementalChamberSpatialContract spatial{
            chamber.node_id,
            chamber.center_millimetres,
            chamber.minimum_safe_ring_radius_millimetres
                - movement_envelope.capsule_radius_millimetres
                - movement_envelope.safety_margin_millimetres,
            chamber.wall_height_millimetres,
            {},
        };
        for (const PortalContract& portal : scene.portals) {
            if (portal.chamber_id == chamber.node_id) {
                spatial.portal_centers_millimetres.push_back(portal.center_millimetres);
            }
        }
        elemental_spatial.push_back(std::move(spatial));
    }
    scene.elemental_visuals = build_elemental_scene(
        topology, effective_seed, elemental_spatial);
    const std::vector<std::string> errors{validate_cave_scene(topology, scene)};
    if (!errors.empty()) {
        throw GeometryError{errors.front()};
    }
    return scene;
}

}  // namespace crystalbound
