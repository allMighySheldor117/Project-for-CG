#include "crystalbound/CaveScene.hpp"

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

#include "crystalbound/DeterministicRandom.hpp"

namespace crystalbound {
namespace {

constexpr std::uint64_t fnv_offset_basis{14695981039346656037ULL};
constexpr std::uint64_t fnv_prime{1099511628211ULL};
constexpr std::uint32_t cave_scene_contract_version{2U};

template <typename Integer>
void append_little_endian(std::vector<std::uint8_t>& bytes, const Integer value)
{
    static_assert(std::is_integral_v<Integer>);
    using Unsigned = std::make_unsigned_t<Integer>;
    Unsigned bits{static_cast<Unsigned>(value)};
    for (std::size_t index{}; index < sizeof(Integer); ++index) {
        bytes.push_back(static_cast<std::uint8_t>(bits & Unsigned{0xFFU}));
        bits >>= 8U;
    }
}

void append_point(std::vector<std::uint8_t>& bytes, const IntegerPoint3& point)
{
    append_little_endian(bytes, point.x_millimetres);
    append_little_endian(bytes, point.y_millimetres);
    append_little_endian(bytes, point.z_millimetres);
}

void append_edge(std::vector<std::uint8_t>& bytes, const Edge edge)
{
    append_little_endian(bytes, edge.first.value);
    append_little_endian(bytes, edge.second.value);
}

[[nodiscard]] std::uint64_t fnv1a(const std::vector<std::uint8_t>& bytes) noexcept
{
    std::uint64_t hash{fnv_offset_basis};
    for (const std::uint8_t byte : bytes) {
        hash ^= byte;
        hash *= fnv_prime;
    }
    return hash;
}

}  // namespace

std::uint64_t cave_scene_fingerprint(
    const Seed effective_seed,
    const CaveSceneData& scene)
{
    std::vector<std::uint8_t> bytes;
    append_little_endian(bytes, cave_scene_contract_version);
    append_little_endian(bytes, current_generator_version.value);
    append_little_endian(bytes, random_domain::geometry);
    append_little_endian(bytes, effective_seed.value);
    append_little_endian(bytes, scene.template_gameplay_fingerprint);
    append_little_endian(bytes,
        static_cast<std::uint32_t>(scene.template_socket_assignments.size()));
    for (const ChamberSocketAssignment& assignment :
        scene.template_socket_assignments) {
        append_little_endian(bytes, assignment.chamber_id.value);
        append_little_endian(bytes, assignment.orientation_octant);
        append_little_endian(bytes,
            static_cast<std::uint32_t>(assignment.incident_edges.size()));
        for (const AssignedTemplateSocket& edge_socket :
            assignment.incident_edges) {
            append_edge(bytes, edge_socket.edge);
            append_little_endian(bytes, edge_socket.socket_index);
        }
    }

    append_little_endian(bytes, static_cast<std::uint32_t>(scene.chambers.size()));
    for (const ChamberGeometryContract& chamber : scene.chambers) {
        append_little_endian(bytes, chamber.node_id.value);
        append_point(bytes, chamber.center_millimetres);
        append_little_endian(bytes, chamber.base_radius_millimetres);
        append_little_endian(bytes, chamber.wall_height_millimetres);
        append_little_endian(bytes, chamber.side_count);
        append_little_endian(
            bytes,
            static_cast<std::uint32_t>(chamber.radial_offsets_millimetres.size()));
        for (const std::int32_t offset : chamber.radial_offsets_millimetres) {
            append_little_endian(bytes, offset);
        }
        append_little_endian(bytes, chamber.minimum_safe_ring_radius_millimetres);
        append_little_endian(bytes, static_cast<std::uint8_t>(chamber.identity.floor));
        append_little_endian(bytes, static_cast<std::uint8_t>(chamber.identity.shell));
        append_little_endian(bytes, static_cast<std::uint8_t>(chamber.identity.entrance));
        append_little_endian(bytes, static_cast<std::uint8_t>(chamber.identity.landmark));
        append_little_endian(bytes, chamber.identity.vertical_profile);
        append_little_endian(bytes, static_cast<std::uint32_t>(chamber.rings.size()));
        for (const ChamberRingContract& ring : chamber.rings) {
            append_little_endian(bytes, ring.height_millimetres);
            append_little_endian(bytes,
                static_cast<std::uint32_t>(ring.radii_millimetres.size()));
            for (const std::int32_t radius : ring.radii_millimetres) {
                append_little_endian(bytes, radius);
            }
        }
    }

    append_little_endian(bytes, static_cast<std::uint32_t>(scene.portals.size()));
    for (const PortalContract& portal : scene.portals) {
        append_little_endian(bytes, portal.chamber_id.value);
        append_edge(bytes, portal.route);
        append_point(bytes, portal.center_millimetres);
        append_point(bytes, portal.inward_direction_millimetres);
        append_little_endian(bytes, portal.opening_side_index);
        append_little_endian(bytes, portal.approach_depth_millimetres);
    }

    append_little_endian(bytes, static_cast<std::uint32_t>(scene.routes.size()));
    for (const RouteGeometryContract& route : scene.routes) {
        append_edge(bytes, route.edge);
        append_little_endian(bytes, route.spline.stable_object_id);
        append_little_endian(bytes, route.spline.radius_millimetres);
        append_little_endian(bytes, route.spline.ring_side_count);
        append_little_endian(bytes, static_cast<std::uint8_t>(route.spline.facing));
        append_little_endian(
            bytes,
            static_cast<std::uint32_t>(route.spline.control_points.size()));
        for (const IntegerPoint3& point : route.spline.control_points) {
            append_point(bytes, point);
        }
        append_little_endian(
            bytes,
            static_cast<std::uint32_t>(route.ring_offsets_millimetres.size()));
        for (const std::int32_t offset : route.ring_offsets_millimetres) {
            append_little_endian(bytes, offset);
        }
        append_little_endian(bytes, static_cast<std::uint8_t>(route.bridge ? 1U : 0U));
        append_little_endian(bytes, route.tunnel_clear_width_millimetres);
        append_little_endian(bytes, route.tunnel_side_height_millimetres);
        append_little_endian(bytes, route.tunnel_crown_height_millimetres);
        append_little_endian(bytes, route.vestibule_length_millimetres);
        append_little_endian(bytes, route.join_overlap_millimetres);
        append_little_endian(bytes, route.bridge_width_millimetres);
        append_little_endian(bytes, route.bridge_rail_height_millimetres);
    }
    return fnv1a(bytes);
}

}  // namespace crystalbound
