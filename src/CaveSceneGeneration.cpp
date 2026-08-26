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
#include "crystalbound/DeterministicRandom.hpp"

namespace crystalbound {
namespace {

using namespace geometry_detail;

constexpr std::int32_t chamber_radius_millimetres{1'750};
constexpr std::int32_t chamber_radial_variation_millimetres{120};
constexpr std::int32_t tunnel_radius_millimetres{1'350};
constexpr std::uint32_t chamber_side_count{16U};
constexpr std::uint32_t tunnel_side_count{8U};
constexpr std::int32_t tunnel_radial_variation_millimetres{80};
constexpr std::int32_t junction_depth_millimetres{500};
constexpr std::int32_t bridge_width_millimetres{1'900};
constexpr std::int32_t bridge_rail_height_millimetres{850};
constexpr double millimetres_per_metre{1'000.0};

constexpr std::uint64_t chamber_shell_domain{0x1000'0000'0000'0000ULL};
constexpr std::uint64_t chamber_floor_domain{0x2000'0000'0000'0000ULL};
constexpr std::uint64_t tunnel_piece_domain{0x3000'0000'0000'0000ULL};
constexpr std::uint64_t junction_piece_domain{0x4000'0000'0000'0000ULL};
constexpr std::uint64_t bridge_piece_domain{0x5000'0000'0000'0000ULL};

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

[[nodiscard]] bool opening_contains_side(
    const std::vector<PortalContract>& portals,
    const NodeId chamber_id,
    const std::uint32_t side) noexcept
{
    for (const PortalContract& portal : portals) {
        if (portal.chamber_id != chamber_id) {
            continue;
        }
        const std::uint32_t previous{
            (portal.opening_side_index + chamber_side_count - 1U) % chamber_side_count};
        const std::uint32_t next{
            (portal.opening_side_index + 1U) % chamber_side_count};
        if (side == previous || side == portal.opening_side_index || side == next) {
            return true;
        }
    }
    return false;
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

[[nodiscard]] GeometryVector3 chamber_ring_position(
    const ChamberGeometryContract& chamber,
    const std::uint32_t side,
    const double height_metres,
    const double radius_scale = 1.0)
{
    const IntegerDirection2 direction{chamber_directions[side % chamber_side_count]};
    const std::int32_t radius_millimetres{
        chamber.base_radius_millimetres
        + chamber.radial_offsets_millimetres[side % chamber.side_count]};
    const double radius{
        static_cast<double>(radius_millimetres) / millimetres_per_metre * radius_scale};
    return {
        static_cast<double>(chamber.center_millimetres.x_millimetres)
                / millimetres_per_metre
            + static_cast<double>(direction.x) / 10'000.0 * radius,
        height_metres,
        static_cast<double>(chamber.center_millimetres.z_millimetres)
                / millimetres_per_metre
            + static_cast<double>(direction.z) / 10'000.0 * radius,
    };
}

[[nodiscard]] MeshData build_chamber_floor(const ChamberGeometryContract& chamber)
{
    MeshBuilder builder;
    const double floor_y{
        static_cast<double>(chamber.center_millimetres.y_millimetres)
        / millimetres_per_metre};
    const GeometryVector3 center{
        static_cast<double>(chamber.center_millimetres.x_millimetres)
            / millimetres_per_metre,
        floor_y,
        static_cast<double>(chamber.center_millimetres.z_millimetres)
            / millimetres_per_metre,
    };
    for (std::uint32_t side{}; side < chamber.side_count; ++side) {
        const GeometryVector3 current{chamber_ring_position(chamber, side, floor_y)};
        const GeometryVector3 next{
            chamber_ring_position(chamber, (side + 1U) % chamber.side_count, floor_y)};
        append_triangle_face(builder, center, next, current, {0.0, 1.0, 0.0});
    }
    return builder.finish();
}

[[nodiscard]] MeshData build_chamber_shell(
    const ChamberGeometryContract& chamber,
    const std::vector<PortalContract>& portals)
{
    MeshBuilder builder;
    const double floor_y{
        static_cast<double>(chamber.center_millimetres.y_millimetres)
        / millimetres_per_metre};
    const double wall_top{
        floor_y + static_cast<double>(chamber.wall_height_millimetres) / millimetres_per_metre
            * 0.72};
    const GeometryVector3 center{
        static_cast<double>(chamber.center_millimetres.x_millimetres)
            / millimetres_per_metre,
        floor_y + 1.5,
        static_cast<double>(chamber.center_millimetres.z_millimetres)
            / millimetres_per_metre,
    };
    const GeometryVector3 apex{
        center.x,
        floor_y + static_cast<double>(chamber.wall_height_millimetres)
            / millimetres_per_metre,
        center.z,
    };
    for (std::uint32_t side{}; side < chamber.side_count; ++side) {
        const std::uint32_t next_side{(side + 1U) % chamber.side_count};
        const GeometryVector3 bottom_first{chamber_ring_position(chamber, side, floor_y)};
        const GeometryVector3 bottom_second{
            chamber_ring_position(chamber, next_side, floor_y)};
        const GeometryVector3 top_first{
            chamber_ring_position(chamber, side, wall_top, 0.78)};
        const GeometryVector3 top_second{
            chamber_ring_position(chamber, next_side, wall_top, 0.78)};
        if (!opening_contains_side(portals, chamber.node_id, side)) {
            const GeometryVector3 wall_midpoint{multiply(
                add(add(bottom_first, bottom_second), add(top_first, top_second)), 0.25)};
            append_quad_face(
                builder,
                bottom_first,
                bottom_second,
                top_second,
                top_first,
                subtract(center, wall_midpoint));
        }
        const GeometryVector3 ceiling_midpoint{
            multiply(add(add(top_first, top_second), apex), 1.0 / 3.0)};
        append_triangle_face(
            builder,
            top_first,
            top_second,
            apex,
            subtract(center, ceiling_midpoint));
    }
    return builder.finish();
}

[[nodiscard]] MeshData build_perturbed_tunnel(const RouteGeometryContract& route)
{
    const std::vector<SplineSample> samples{
        sample_centripetal_catmull_rom(route.spline)};
    const std::vector<TransportFrame> frames{build_parallel_transport_frames(samples)};
    const std::size_t vertices_per_ring{
        static_cast<std::size_t>(route.spline.ring_side_count) + 1U};
    if (frames.size() > geometry_budgets.maximum_static_vertices / vertices_per_ring) {
        throw GeometryError{"Tunnel sweep exceeds the static vertex budget."};
    }

    MeshBuilder builder;
    constexpr double pi{3.14159265358979323846};
    for (std::size_t ring{}; ring < frames.size(); ++ring) {
        const TransportFrame& frame{frames[ring]};
        for (std::uint32_t side{}; side <= route.spline.ring_side_count; ++side) {
            const std::uint32_t wrapped_side{side % route.spline.ring_side_count};
            const double radial_millimetres{
                static_cast<double>(route.spline.radius_millimetres
                    + route.ring_offsets_millimetres[wrapped_side])};
            const double ring_coordinate{
                static_cast<double>(side) / route.spline.ring_side_count};
            const double angle{wrapped_side == 0U && side != 0U
                    ? 0.0
                    : ring_coordinate * 2.0 * pi};
            const GeometryVector3 radial{add(
                multiply(frame.normal, std::cos(angle)),
                multiply(frame.binormal, std::sin(angle)))};
            const GeometryVector3 position{add(
                frame.position_metres,
                multiply(radial, radial_millimetres / millimetres_per_metre))};
            const GeometryVector3 normal{multiply(radial, -1.0)};
            static_cast<void>(builder.append_vertex(vertex_for(
                position,
                normal,
                static_cast<float>(samples[ring].distance_metres),
                static_cast<float>(ring_coordinate))));
        }
    }
    for (std::size_t ring{}; ring + 1U < frames.size(); ++ring) {
        const auto first_ring{static_cast<std::uint32_t>(ring * vertices_per_ring)};
        const auto second_ring{static_cast<std::uint32_t>((ring + 1U) * vertices_per_ring)};
        for (std::uint32_t side{}; side < route.spline.ring_side_count; ++side) {
            const std::uint32_t first{first_ring + side};
            const std::uint32_t next{first + 1U};
            const std::uint32_t second{second_ring + side};
            const std::uint32_t second_next{second + 1U};
            builder.append_triangle(first, second, second_next);
            builder.append_triangle(first, second_next, next);
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
    const std::array<float, 3>& albedo)
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
    const MaterialKind material{
        kind == ScenePieceKind::bridge ? MaterialKind::wood : MaterialKind::rock};
    scene.mesh_pieces.push_back({kind, stable_id, std::move(mesh), bounds, material, albedo});
}

[[nodiscard]] ChamberGeometryContract make_chamber_contract(
    const ChamberNode& node,
    const Seed effective_seed)
{
    SplitMix64 random{make_substream(
        effective_seed.value, random_domain::geometry, node.id.value)};
    ChamberGeometryContract chamber{
        node.id,
        {node.anchor.x_millimetres, node.anchor.elevation_millimetres,
         node.anchor.z_millimetres},
        chamber_radius_millimetres,
        3'800 + static_cast<std::int32_t>(random.bounded(601U)),
        chamber_side_count,
        {},
    };
    chamber.radial_offsets_millimetres.reserve(chamber.side_count);
    for (std::uint32_t side{}; side < chamber.side_count; ++side) {
        chamber.radial_offsets_millimetres.push_back(
            signed_sample(random, chamber_radial_variation_millimetres));
    }
    return chamber;
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
                chambers[left].base_radius_millimetres
                + chambers[right].base_radius_millimetres
                + 2 * chamber_radial_variation_millimetres
                + geometry_spatial_contract.chamber_safety_separation_millimetres};
            if (dx * dx + dz * dz < required * required) {
                throw GeometryError{"Chamber bounds violate the locked safety separation."};
            }
        }
    }
}

[[nodiscard]] Edge select_bridge_route(
    const TopologyData& topology,
    const Seed effective_seed)
{
    if (topology.routes.empty()) {
        throw GeometryError{"A cave requires at least one route for its bridge."};
    }
    Edge selected{topology.routes.front().edge};
    std::uint64_t selected_score{std::numeric_limits<std::uint64_t>::max()};
    for (const RouteDescriptor& route : topology.routes) {
        SplitMix64 random{make_substream(
            effective_seed.value,
            random_domain::geometry,
            stable_edge_id(route.edge) ^ bridge_piece_domain)};
        const std::uint64_t score{random.next()};
        if (score < selected_score
            || (score == selected_score && route.edge < selected)) {
            selected = route.edge;
            selected_score = score;
        }
    }
    return selected;
}

[[nodiscard]] PortalContract make_portal(
    const ChamberNode& chamber,
    const ChamberNode& other,
    const Edge edge)
{
    const std::int32_t dx{other.anchor.x_millimetres - chamber.anchor.x_millimetres};
    const std::int32_t dz{other.anchor.z_millimetres - chamber.anchor.z_millimetres};
    const IntegerDirection2 outward{
        scaled_horizontal_direction(dx, dz, chamber_radius_millimetres)};
    const IntegerDirection2 inward{scaled_horizontal_direction(dx, dz, -1'000)};
    return {
        chamber.id,
        edge,
        {chamber.anchor.x_millimetres + outward.x,
         chamber.anchor.elevation_millimetres + tunnel_radius_millimetres,
         chamber.anchor.z_millimetres + outward.z},
        {inward.x, 0, inward.z},
        nearest_chamber_side(dx, dz),
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
    const Edge bridge_edge,
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
    std::int32_t bend{
        300 + std::abs(descriptor.lateral_offset_millimetres) / 8};
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
    const std::int32_t vertical_bend{};
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
        descriptor.edge == bridge_edge,
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
                static_cast<double>(chamber.base_radius_millimetres
                    + chamber_radial_variation_millimetres
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

[[nodiscard]] MeshData build_junction_mesh(const PortalContract& portal)
{
    const IntegerPoint3 inner{
        portal.center_millimetres.x_millimetres
            + rounded_ratio(
                static_cast<std::int64_t>(portal.inward_direction_millimetres.x_millimetres)
                    * junction_depth_millimetres,
                1'000),
        portal.center_millimetres.y_millimetres,
        portal.center_millimetres.z_millimetres
            + rounded_ratio(
                static_cast<std::int64_t>(portal.inward_direction_millimetres.z_millimetres)
                    * junction_depth_millimetres,
                1'000),
    };
    return build_spline_ring_mesh({
        stable_edge_id(portal.route) ^ portal.chamber_id.value,
        {portal.center_millimetres, inner},
        tunnel_radius_millimetres,
        tunnel_side_count,
        SurfaceFacing::inward,
    });
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
    const auto portal = std::find_if(
        scene.portals.begin(), scene.portals.end(), [&](const PortalContract& candidate) {
            return candidate.chamber_id == start->id;
        });
    if (portal == scene.portals.end()) {
        throw GeometryError{"Start chamber has no route portal."};
    }
    scene.start_camera_position_metres = {
        static_cast<double>(start->anchor.x_millimetres) / millimetres_per_metre,
        static_cast<double>(start->anchor.elevation_millimetres
            + movement_envelope.camera_height_millimetres) / millimetres_per_metre,
        static_cast<double>(start->anchor.z_millimetres) / millimetres_per_metre,
    };
    scene.start_camera_forward = normalized(
        subtract(from_millimetres(portal->center_millimetres),
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
    scene.chambers.reserve(topology.nodes.size());
    for (const ChamberNode& node : topology.nodes) {
        scene.chambers.push_back(make_chamber_contract(node, effective_seed));
    }
    validate_chamber_spacing(scene.chambers);

    scene.portals.reserve(topology.routes.size() * 2U);
    for (const RouteDescriptor& route : topology.routes) {
        const ChamberNode& first{node_for(topology, route.edge.first)};
        const ChamberNode& second{node_for(topology, route.edge.second)};
        scene.portals.push_back(make_portal(first, second, route.edge));
        scene.portals.push_back(make_portal(second, first, route.edge));
    }

    const Edge bridge_edge{select_bridge_route(topology, effective_seed)};
    scene.bridge_routes.push_back(bridge_edge);
    scene.routes.reserve(topology.routes.size());
    for (const RouteDescriptor& descriptor : topology.routes) {
        scene.routes.push_back(make_route_contract(
            descriptor, scene.portals, bridge_edge, effective_seed));
    }
    validate_route_spatial_separation(scene.routes, scene.chambers);

    for (const ChamberGeometryContract& chamber : scene.chambers) {
        MeshData floor{build_chamber_floor(chamber)};
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
            {0.30F, 0.33F, 0.37F});

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
            {0.36F, 0.39F, 0.44F});
    }

    for (const RouteGeometryContract& route : scene.routes) {
        MeshData tunnel{build_perturbed_tunnel(route)};
        const AxisAlignedBounds tunnel_bounds{mesh_bounds(tunnel)};
        scene.colliders.push_back({
            ColliderKind::tunnel,
            tunnel_piece_domain ^ stable_edge_id(route.edge),
            tunnel_bounds,
        });
        append_piece(
            scene,
            ScenePieceKind::tunnel,
            tunnel_piece_domain ^ stable_edge_id(route.edge),
            std::move(tunnel),
            route.bridge ? std::array<float, 3>{0.29F, 0.31F, 0.35F}
                         : std::array<float, 3>{0.33F, 0.36F, 0.41F});

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
        append_piece(
            scene,
            ScenePieceKind::junction,
            junction_piece_domain ^ stable_edge_id(portal.route)
                ^ (static_cast<std::uint64_t>(portal.chamber_id.value) << 32U),
            build_junction_mesh(portal),
            {0.31F, 0.34F, 0.39F});
    }

    set_start_camera(scene, topology);
    scene.fingerprint = cave_scene_fingerprint(effective_seed, scene);
    const std::vector<std::string> errors{validate_cave_scene(topology, scene)};
    if (!errors.empty()) {
        throw GeometryError{errors.front()};
    }
    return scene;
}

}  // namespace crystalbound
