#include "crystalbound/CaveScene.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace crystalbound {
namespace {

[[nodiscard]] bool finite_vector(const GeometryVector3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] bool valid_bounds(const AxisAlignedBounds& bounds) noexcept
{
    return finite_vector(bounds.minimum_metres) && finite_vector(bounds.maximum_metres)
        && bounds.minimum_metres.x <= bounds.maximum_metres.x
        && bounds.minimum_metres.y <= bounds.maximum_metres.y
        && bounds.minimum_metres.z <= bounds.maximum_metres.z;
}

[[nodiscard]] bool topology_has_edge(const TopologyData& topology, const Edge edge)
{
    return std::find(topology.edges.begin(), topology.edges.end(), edge)
        != topology.edges.end();
}

[[nodiscard]] bool topology_has_node(const TopologyData& topology, const NodeId id)
{
    return std::any_of(
        topology.nodes.begin(), topology.nodes.end(), [id](const ChamberNode& node) {
            return node.id == id;
        });
}

[[nodiscard]] bool has_collider_kind(
    const CaveSceneData& scene,
    const ColliderKind kind)
{
    return std::any_of(
        scene.colliders.begin(), scene.colliders.end(), [kind](const SceneCollider& collider) {
            return collider.kind == kind;
        });
}

[[nodiscard]] bool route_is_curved(const RouteGeometryContract& route) noexcept
{
    if (route.spline.control_points.size() < 4U) {
        return false;
    }
    const IntegerPoint3& first{route.spline.control_points.front()};
    const IntegerPoint3& last{route.spline.control_points.back()};
    const std::int64_t dx{
        static_cast<std::int64_t>(last.x_millimetres) - first.x_millimetres};
    const std::int64_t dz{
        static_cast<std::int64_t>(last.z_millimetres) - first.z_millimetres};
    for (std::size_t index{1U}; index + 1U < route.spline.control_points.size(); ++index) {
        const IntegerPoint3& point{route.spline.control_points[index]};
        const std::int64_t point_x{
            static_cast<std::int64_t>(point.x_millimetres) - first.x_millimetres};
        const std::int64_t point_z{
            static_cast<std::int64_t>(point.z_millimetres) - first.z_millimetres};
        if (dx * point_z - dz * point_x != 0) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] const PortalContract* find_portal(
    const CaveSceneData& scene,
    const NodeId chamber,
    const Edge edge)
{
    const auto found = std::find_if(
        scene.portals.begin(), scene.portals.end(), [&](const PortalContract& portal) {
            return portal.chamber_id == chamber && portal.route == edge;
        });
    return found == scene.portals.end() ? nullptr : &*found;
}

void validate_contract_counts(
    const TopologyData& topology,
    const CaveSceneData& scene,
    std::vector<std::string>& errors)
{
    if (scene.chambers.size() != topology.nodes.size()) {
        errors.push_back("Every topology node must have one chamber geometry contract.");
    }
    if (scene.routes.size() != topology.routes.size()) {
        errors.push_back("Every topology route must have one route geometry contract.");
    }
    if (scene.portals.size() != topology.routes.size() * 2U) {
        errors.push_back("Every route must have one portal at each endpoint.");
    }
    if (scene.bridge_routes.empty()) {
        errors.push_back("Every accepted cave must contain at least one bridge route.");
    }
}

void validate_chambers(
    const TopologyData& topology,
    const CaveSceneData& scene,
    std::vector<std::string>& errors)
{
    for (const ChamberGeometryContract& chamber : scene.chambers) {
        if (!topology_has_node(topology, chamber.node_id)) {
            errors.push_back("Chamber geometry references an unknown node.");
        }
        if (chamber.base_radius_millimetres <= 0
            || chamber.wall_height_millimetres
                < movement_envelope.minimum_clearance_height_millimetres
                    + movement_envelope.safety_margin_millimetres
            || chamber.side_count < 8U
            || chamber.radial_offsets_millimetres.size() != chamber.side_count) {
            errors.push_back("Chamber geometry violates its radius, height, or facet contract.");
        }
    }
}

void validate_routes(
    const TopologyData& topology,
    const CaveSceneData& scene,
    std::vector<std::string>& errors)
{
    std::size_t bridge_count{};
    bool curved_route{};
    for (const RouteGeometryContract& route : scene.routes) {
        if (!topology_has_edge(topology, route.edge)) {
            errors.push_back("Route geometry references an unknown topology edge.");
        }
        if (route.spline.stable_object_id != stable_edge_id(route.edge)
            || route.spline.control_points.size() < 4U
            || route.ring_offsets_millimetres.size() != route.spline.ring_side_count) {
            errors.push_back("Route geometry does not match its stable spline contract.");
        }
        if (route.spline.radius_millimetres * 2
                < movement_envelope.minimum_clearance_width_millimetres
                    + movement_envelope.safety_margin_millimetres
            || route.spline.radius_millimetres * 2
                < movement_envelope.minimum_clearance_height_millimetres
                    + movement_envelope.safety_margin_millimetres) {
            errors.push_back("Route geometry does not meet traversal clearance.");
        }
        const PortalContract* first{find_portal(scene, route.edge.first, route.edge)};
        const PortalContract* second{find_portal(scene, route.edge.second, route.edge)};
        if (first == nullptr || second == nullptr
            || route.spline.control_points.front() != first->center_millimetres
            || route.spline.control_points.back() != second->center_millimetres) {
            errors.push_back("Route endpoints do not agree with their declared portals.");
        }
        curved_route = curved_route || route_is_curved(route);
        if (route.bridge) {
            ++bridge_count;
            if (route.bridge_width_millimetres
                    < movement_envelope.minimum_clearance_width_millimetres
                        + movement_envelope.safety_margin_millimetres
                || route.bridge_rail_height_millimetres <= 0) {
                errors.push_back("Bridge dimensions violate the locked traversal envelope.");
            }
        }
    }
    if (!curved_route) {
        errors.push_back("The generated cave must contain curved route geometry.");
    }
    if (bridge_count != scene.bridge_routes.size() || bridge_count == 0U) {
        errors.push_back("Bridge route markers and bridge contracts disagree.");
    }
    for (const Edge edge : scene.bridge_routes) {
        const auto found = std::find_if(
            scene.routes.begin(), scene.routes.end(), [edge](const RouteGeometryContract& route) {
                return route.edge == edge && route.bridge;
            });
        if (found == scene.routes.end()) {
            errors.push_back("Bridge route list references a non-bridge route.");
        }
    }
}

void validate_meshes_and_budgets(
    const CaveSceneData& scene,
    std::vector<std::string>& errors)
{
    std::uint64_t vertex_count{};
    for (const SceneMeshPiece& piece : scene.mesh_pieces) {
        try {
            validate_procedural_mesh(piece.mesh);
        } catch (const GeometryError& error) {
            errors.push_back(std::string{"Scene mesh failed validation: "} + error.what());
            continue;
        }
        vertex_count += piece.mesh.vertices.size();
        if (!valid_bounds(piece.bounds)) {
            errors.push_back("Scene mesh has invalid bounds.");
        }
        if (!std::all_of(piece.albedo.begin(), piece.albedo.end(), [](const float value) {
                return std::isfinite(value) && value >= 0.0F && value <= 1.0F;
            })) {
            errors.push_back("Scene mesh has an invalid debug albedo.");
        }
    }
    if (vertex_count != scene.static_vertex_count
        || vertex_count > geometry_budgets.maximum_static_vertices) {
        errors.push_back("Scene static vertex count violates the locked budget.");
    }
    if (scene.mesh_pieces.size() != scene.opaque_draw_call_count
        || scene.opaque_draw_call_count > geometry_budgets.maximum_opaque_draw_calls) {
        errors.push_back("Scene opaque draw-call count violates the locked budget.");
    }
    if (vertex_count + scene.elemental_visuals.generated_vertex_count
            > geometry_budgets.maximum_static_vertices
        || scene.opaque_draw_call_count + scene.elemental_visuals.opaque_draw_call_count
            > geometry_budgets.maximum_opaque_draw_calls) {
        errors.push_back("Combined structural and elemental geometry exceeds a locked budget.");
    }
}

void validate_colliders(const CaveSceneData& scene, std::vector<std::string>& errors)
{
    for (const SceneCollider& collider : scene.colliders) {
        if (!valid_bounds(collider.bounds)) {
            errors.push_back("Scene collider contains invalid or non-finite bounds.");
        }
    }
    for (const ColliderKind required : {
             ColliderKind::chamber_floor,
             ColliderKind::chamber_boundary,
             ColliderKind::tunnel,
             ColliderKind::bridge_deck,
             ColliderKind::bridge_rail,
             ColliderKind::fall_region}) {
        if (!has_collider_kind(scene, required)) {
            errors.push_back("Scene is missing a required collider category.");
        }
    }
}

}  // namespace

std::vector<std::string> validate_cave_scene(
    const TopologyData& topology,
    const CaveSceneData& scene)
{
    std::vector<std::string> errors{validate_topology(topology)};
    validate_contract_counts(topology, scene, errors);
    validate_chambers(topology, scene, errors);
    validate_routes(topology, scene, errors);
    validate_meshes_and_budgets(scene, errors);
    validate_colliders(scene, errors);
    const std::vector<std::string> elemental_errors{
        validate_elemental_scene(topology, scene.elemental_visuals)};
    errors.insert(errors.end(), elemental_errors.begin(), elemental_errors.end());

    const double forward_length{std::sqrt(
        scene.start_camera_forward.x * scene.start_camera_forward.x
        + scene.start_camera_forward.y * scene.start_camera_forward.y
        + scene.start_camera_forward.z * scene.start_camera_forward.z)};
    if (!finite_vector(scene.start_camera_position_metres)
        || !finite_vector(scene.start_camera_forward)
        || !std::isfinite(forward_length)
        || std::abs(forward_length - 1.0) > 1.0e-6) {
        errors.push_back("Start camera pose is invalid.");
    }
    if (scene.fingerprint == 0U) {
        errors.push_back("Scene fingerprint must be nonzero.");
    }
    return errors;
}

}  // namespace crystalbound
