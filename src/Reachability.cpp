#include "crystalbound/Reachability.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <deque>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace crystalbound {
namespace {

constexpr double comparison_tolerance{1.0e-9};

[[nodiscard]] bool finite_vector(const GeometryVector3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] bool contains_node(
    const std::vector<NodeId>& nodes,
    const NodeId target) noexcept
{
    return std::binary_search(nodes.begin(), nodes.end(), target);
}

void insert_node(std::vector<NodeId>& nodes, const NodeId node)
{
    const auto position{std::lower_bound(nodes.begin(), nodes.end(), node)};
    if (position == nodes.end() || *position != node) {
        nodes.insert(position, node);
    }
}

void append_issue(
    MechanicalReachabilityReport& report,
    const ReachabilityFailure failure,
    const std::optional<NodeId> chamber_id = std::nullopt,
    const std::optional<Edge> edge = std::nullopt,
    const std::optional<RouteDirection> direction = std::nullopt,
    const std::uint64_t stable_object_id = 0U)
{
    report.issues.push_back({
        failure,
        chamber_id,
        edge,
        direction,
        stable_object_id,
    });
}

[[nodiscard]] const ChamberCollisionRegion* find_chamber(
    const CollisionWorld& world,
    const NodeId chamber_id) noexcept
{
    const auto found{std::find_if(
        world.chambers.begin(), world.chambers.end(),
        [chamber_id](const ChamberCollisionRegion& chamber) {
            return chamber.chamber_id == chamber_id;
        })};
    return found == world.chambers.end() ? nullptr : &*found;
}

[[nodiscard]] const RouteCollisionRegion* find_route(
    const CollisionWorld& world,
    const Edge edge) noexcept
{
    const auto found{std::find_if(
        world.routes.begin(), world.routes.end(),
        [edge](const RouteCollisionRegion& route) {
            return route.edge == edge;
        })};
    return found == world.routes.end() ? nullptr : &*found;
}

[[nodiscard]] const RouteGeometryContract* find_scene_route(
    const CaveSceneData& scene,
    const Edge edge) noexcept
{
    const auto found{std::find_if(
        scene.routes.begin(), scene.routes.end(),
        [edge](const RouteGeometryContract& route) {
            return route.edge == edge;
        })};
    return found == scene.routes.end() ? nullptr : &*found;
}

[[nodiscard]] bool samples_are_finite(const RouteCollisionRegion& route) noexcept
{
    if (route.samples.size() < 2U) {
        return false;
    }
    for (std::size_t index{}; index < route.samples.size(); ++index) {
        const SplineSample& sample{route.samples[index]};
        if (!finite_vector(sample.position_metres) || !finite_vector(sample.tangent)
            || !std::isfinite(sample.distance_metres)
            || (index != 0U
                && sample.distance_metres
                    <= route.samples[index - 1U].distance_metres)) {
            return false;
        }
    }
    return true;
}

void add_failure(
    DirectedRouteTraversal& traversal,
    const ReachabilityFailure failure)
{
    if (std::find(traversal.failures.begin(), traversal.failures.end(), failure)
        == traversal.failures.end()) {
        traversal.failures.push_back(failure);
    }
}

[[nodiscard]] std::size_t direction_index(const RouteDirection direction) noexcept
{
    return direction == RouteDirection::first_to_second ? 0U : 1U;
}

[[nodiscard]] DirectedRouteTraversal inspect_direction(
    const RouteDescriptor& descriptor,
    const RouteCollisionRegion* collision_route,
    const RouteGeometryContract* scene_route,
    const RouteDirection direction)
{
    const bool forward{direction == RouteDirection::first_to_second};
    DirectedRouteTraversal traversal{
        descriptor.edge,
        forward ? descriptor.edge.first : descriptor.edge.second,
        forward ? descriptor.edge.second : descriptor.edge.first,
        direction,
        stable_edge_id(descriptor.edge),
        scene_route != nullptr && scene_route->bridge,
        false,
        {},
    };
    if (collision_route == nullptr) {
        add_failure(traversal, ReachabilityFailure::missing_route_collision);
        return traversal;
    }
    traversal.stable_object_id = collision_route->stable_object_id;
    if (!samples_are_finite(*collision_route)
        || !std::isfinite(collision_route->usable_half_width_metres)
        || !std::isfinite(collision_route->tunnel_radius_metres)
        || !std::isfinite(collision_route->rail_height_metres)) {
        add_failure(traversal, ReachabilityFailure::non_finite_collision_data);
    }
    const std::int32_t required_width{
        movement_envelope.minimum_clearance_width_millimetres
        + movement_envelope.safety_margin_millimetres};
    const std::int32_t required_height{
        movement_envelope.minimum_clearance_height_millimetres
        + movement_envelope.safety_margin_millimetres};
    if (collision_route->clearance_width_millimetres < required_width) {
        add_failure(traversal, ReachabilityFailure::insufficient_clearance_width);
    }
    if (collision_route->clearance_height_millimetres < required_height) {
        add_failure(traversal, ReachabilityFailure::insufficient_clearance_height);
    }
    if (collision_route->maximum_slope_millidegrees
        > movement_envelope.maximum_slope_millidegrees) {
        add_failure(traversal, ReachabilityFailure::excessive_slope);
    }
    const RouteCollisionRegion::DirectedMeasurements& measurements{
        collision_route->directed[direction_index(direction)]};
    if (collision_route->maximum_slope_millidegrees < 0
        || measurements.maximum_step_up_millimetres < 0
        || measurements.maximum_gap_millimetres < 0
        || measurements.minimum_landing_width_millimetres < 0) {
        add_failure(traversal, ReachabilityFailure::non_finite_collision_data);
    }
    if (measurements.maximum_step_up_millimetres
        > movement_envelope.step_height_millimetres) {
        add_failure(traversal, ReachabilityFailure::excessive_step);
    }
    if (measurements.maximum_gap_millimetres
        > movement_envelope.maximum_gap_millimetres) {
        add_failure(traversal, ReachabilityFailure::excessive_gap);
    }
    if (measurements.minimum_landing_width_millimetres
        < movement_envelope.minimum_landing_width_millimetres) {
        add_failure(traversal, ReachabilityFailure::insufficient_landing_width);
    }
    if (!measurements.chamber_to_junction_supported) {
        add_failure(
            traversal, ReachabilityFailure::unsupported_chamber_junction_seam);
    }
    if (!measurements.junction_to_route_supported) {
        add_failure(traversal, ReachabilityFailure::unsupported_junction_route_seam);
    }
    if (!measurements.route_to_chamber_supported) {
        add_failure(traversal, ReachabilityFailure::unsupported_route_chamber_seam);
    }
    if (scene_route == nullptr
        || (scene_route->bridge
            != (collision_route->kind == GroundContactKind::bridge))) {
        add_failure(traversal, ReachabilityFailure::missing_route_collision);
    }
    if (traversal.bridge) {
        const double required_rail_height{
            static_cast<double>(
                movement_envelope.capsule_radius_millimetres
                + movement_envelope.safety_margin_millimetres)
            / 1'000.0};
        if (collision_route->rail_height_metres + comparison_tolerance
                < required_rail_height
            || collision_route->usable_half_width_metres <= 0.0) {
            add_failure(traversal, ReachabilityFailure::unsafe_bridge);
        }
    }
    traversal.traversable = traversal.failures.empty();
    return traversal;
}

void append_traversal_issues(
    MechanicalReachabilityReport& report,
    const DirectedRouteTraversal& traversal)
{
    for (const ReachabilityFailure failure : traversal.failures) {
        append_issue(
            report,
            failure,
            std::nullopt,
            traversal.edge,
            traversal.direction,
            traversal.stable_object_id);
    }
}

[[nodiscard]] bool same_probe(
    const CollisionProbe& left,
    const CollisionProbe& right) noexcept
{
    return left.supported == right.supported
        && left.floor_height_metres == right.floor_height_metres
        && left.ceiling_height_metres == right.ceiling_height_metres
        && left.slope_radians == right.slope_radians
        && left.contact_kind == right.contact_kind
        && left.chamber_id == right.chamber_id
        && left.stable_object_id == right.stable_object_id;
}

[[nodiscard]] ChamberRespawnVerdict inspect_respawn(
    const CollisionWorld& world,
    const ChamberNode& node)
{
    ChamberRespawnVerdict verdict{node.id, node.id.value, false, {}};
    const ChamberCollisionRegion* chamber{find_chamber(world, node.id)};
    if (chamber == nullptr) {
        verdict.failures.push_back(ReachabilityFailure::missing_chamber_collision);
        return verdict;
    }
    verdict.stable_object_id = chamber->stable_object_id;
    const GeometryVector3 respawn{chamber_respawn_position(
        world, *chamber, chamber->center_metres)};
    const bool finite{finite_vector(respawn)
        && std::isfinite(chamber->ceiling_height_metres)
        && std::isfinite(chamber->usable_radius_metres)};
    const bool clear{finite && chamber->usable_radius_metres > 0.0
        && chamber->ceiling_height_metres - chamber->floor_height_metres
                + comparison_tolerance
            >= static_cast<double>(movement_envelope.total_height_millimetres)
                / 1'000.0
        && respawn.y > world.kill_plane_metres
        && !intersects_fall_region(world, respawn)};
    if (!clear) {
        verdict.failures.push_back(ReachabilityFailure::unsafe_respawn);
        return verdict;
    }
    try {
        const PlayerCapsule capsule{locked_player_capsule()};
        const double step{
            static_cast<double>(movement_envelope.step_height_millimetres) / 1'000.0};
        const CollisionProbe first{
            probe_collision_world(world, capsule, respawn, step)};
        const CollisionProbe second{
            probe_collision_world(world, capsule, respawn, step)};
        if (!same_probe(first, second)) {
            verdict.failures.push_back(ReachabilityFailure::unstable_respawn);
        } else if (!first.supported || first.contact_kind != GroundContactKind::chamber
            || first.chamber_id != std::optional<NodeId>{node.id}) {
            verdict.failures.push_back(ReachabilityFailure::unsafe_respawn);
        }
    } catch (const ControllerError&) {
        verdict.failures.push_back(ReachabilityFailure::unsafe_respawn);
    }
    verdict.safe = verdict.failures.empty();
    return verdict;
}

[[nodiscard]] bool route_is_traversable(
    const MechanicalReachabilityReport& report,
    const NodeId from,
    const NodeId to) noexcept
{
    return std::any_of(
        report.directed_routes.begin(), report.directed_routes.end(),
        [from, to](const DirectedRouteTraversal& route) {
            return route.from == from && route.to == to && route.traversable;
        });
}

void build_reachable_set(MechanicalReachabilityReport& report)
{
    if (!report.start_chamber.has_value()) {
        return;
    }
    std::deque<NodeId> pending;
    insert_node(report.reachable_chambers, *report.start_chamber);
    pending.push_back(*report.start_chamber);
    while (!pending.empty()) {
        const NodeId current{pending.front()};
        pending.pop_front();
        for (const DirectedRouteTraversal& route : report.directed_routes) {
            if (route.from != current || !route.traversable
                || contains_node(report.reachable_chambers, route.to)) {
                continue;
            }
            insert_node(report.reachable_chambers, route.to);
            pending.push_back(route.to);
        }
    }
}

[[nodiscard]] bool guaranteed_cycle_is_bidirectional(
    const TopologyData& topology,
    const MechanicalReachabilityReport& report) noexcept
{
    if (topology.guaranteed_cycle.empty()) {
        return true;
    }
    if (topology.guaranteed_cycle.size() < 3U) {
        return false;
    }
    for (std::size_t index{}; index < topology.guaranteed_cycle.size(); ++index) {
        const NodeId first{topology.guaranteed_cycle[index]};
        const NodeId second{
            topology.guaranteed_cycle[(index + 1U) % topology.guaranteed_cycle.size()]};
        if (!route_is_traversable(report, first, second)
            || !route_is_traversable(report, second, first)) {
            return false;
        }
    }
    return true;
}

}  // namespace

MechanicalReachabilityReport validate_mechanical_reachability(
    const TopologyData& topology,
    const CaveSceneData& scene,
    const CollisionWorld& collision_world)
{
    MechanicalReachabilityReport report;
    const std::vector<std::string> collision_errors{
        validate_collision_world(collision_world)};
    if (!collision_errors.empty()) {
        append_issue(report, ReachabilityFailure::invalid_collision_world);
    }

    std::vector<NodeId> starts;
    for (const ChamberNode& node : topology.nodes) {
        if (node.role == ChamberRole::start) {
            starts.push_back(node.id);
        }
        ChamberRespawnVerdict respawn{inspect_respawn(collision_world, node)};
        for (const ReachabilityFailure failure : respawn.failures) {
            append_issue(
                report, failure, node.id, std::nullopt, std::nullopt,
                respawn.stable_object_id);
        }
        report.respawns.push_back(std::move(respawn));
    }
    std::sort(starts.begin(), starts.end());
    if (starts.empty()) {
        append_issue(report, ReachabilityFailure::missing_start);
    } else {
        report.start_chamber = starts.front();
        if (starts.size() != 1U) {
            append_issue(
                report, ReachabilityFailure::multiple_starts, starts.front());
        }
    }

    std::vector<const RouteDescriptor*> routes;
    routes.reserve(topology.routes.size());
    for (const RouteDescriptor& route : topology.routes) {
        routes.push_back(&route);
    }
    std::sort(
        routes.begin(), routes.end(),
        [](const RouteDescriptor* left, const RouteDescriptor* right) {
            const std::uint64_t left_id{stable_edge_id(left->edge)};
            const std::uint64_t right_id{stable_edge_id(right->edge)};
            return left_id < right_id
                || (left_id == right_id && left->edge < right->edge);
        });

    for (const RouteDescriptor* descriptor : routes) {
        const RouteCollisionRegion* collision_route{
            find_route(collision_world, descriptor->edge)};
        const RouteGeometryContract* scene_route{
            find_scene_route(scene, descriptor->edge)};
        for (const RouteDirection direction : {
                 RouteDirection::first_to_second,
                 RouteDirection::second_to_first}) {
            DirectedRouteTraversal traversal{inspect_direction(
                *descriptor, collision_route, scene_route, direction)};
            append_traversal_issues(report, traversal);
            report.directed_routes.push_back(std::move(traversal));
        }
    }

    build_reachable_set(report);
    for (const ChamberNode& node : topology.nodes) {
        if (!contains_node(report.reachable_chambers, node.id)) {
            report.required_unreachable_chambers.push_back(node.id);
            append_issue(
                report,
                ReachabilityFailure::unreachable_required_chamber,
                node.id,
                std::nullopt,
                std::nullopt,
                node.id.value);
        }
    }

    for (const RouteDescriptor* descriptor : routes) {
        if (!descriptor->on_guaranteed_cycle) {
            continue;
        }
        if (!route_is_traversable(
                report, descriptor->edge.first, descriptor->edge.second)
            || !route_is_traversable(
                report, descriptor->edge.second, descriptor->edge.first)) {
            append_issue(
                report,
                ReachabilityFailure::protected_route_not_bidirectional,
                std::nullopt,
                descriptor->edge,
                std::nullopt,
                stable_edge_id(descriptor->edge));
        }
    }
    for (const Edge bridge : scene.bridge_routes) {
        if (!route_is_traversable(report, bridge.first, bridge.second)
            || !route_is_traversable(report, bridge.second, bridge.first)) {
            append_issue(
                report,
                ReachabilityFailure::protected_route_not_bidirectional,
                std::nullopt,
                bridge,
                std::nullopt,
                stable_edge_id(bridge));
        }
    }
    if (!guaranteed_cycle_is_bidirectional(topology, report)) {
        append_issue(report, ReachabilityFailure::guaranteed_loop_invalid);
    }

    report.accepted = report.issues.empty()
        && report.required_unreachable_chambers.empty();
    report.diagnostics.reserve(report.issues.size());
    for (const ReachabilityIssue& issue : report.issues) {
        report.diagnostics.push_back(format_reachability_issue(issue));
    }
    return report;
}

const char* reachability_failure_name(const ReachabilityFailure failure) noexcept
{
    switch (failure) {
    case ReachabilityFailure::missing_start:
        return "missing_start";
    case ReachabilityFailure::multiple_starts:
        return "multiple_starts";
    case ReachabilityFailure::invalid_collision_world:
        return "invalid_collision_world";
    case ReachabilityFailure::missing_chamber_collision:
        return "missing_chamber_collision";
    case ReachabilityFailure::missing_route_collision:
        return "missing_route_collision";
    case ReachabilityFailure::non_finite_collision_data:
        return "non_finite_collision_data";
    case ReachabilityFailure::insufficient_clearance_width:
        return "insufficient_clearance_width";
    case ReachabilityFailure::insufficient_clearance_height:
        return "insufficient_clearance_height";
    case ReachabilityFailure::excessive_slope:
        return "excessive_slope";
    case ReachabilityFailure::excessive_step:
        return "excessive_step";
    case ReachabilityFailure::excessive_gap:
        return "excessive_gap";
    case ReachabilityFailure::insufficient_landing_width:
        return "insufficient_landing_width";
    case ReachabilityFailure::unsupported_chamber_junction_seam:
        return "unsupported_chamber_junction_seam";
    case ReachabilityFailure::unsupported_junction_route_seam:
        return "unsupported_junction_route_seam";
    case ReachabilityFailure::unsupported_route_chamber_seam:
        return "unsupported_route_chamber_seam";
    case ReachabilityFailure::unsafe_bridge:
        return "unsafe_bridge";
    case ReachabilityFailure::unsafe_respawn:
        return "unsafe_respawn";
    case ReachabilityFailure::unstable_respawn:
        return "unstable_respawn";
    case ReachabilityFailure::unreachable_required_chamber:
        return "unreachable_required_chamber";
    case ReachabilityFailure::protected_route_not_bidirectional:
        return "protected_route_not_bidirectional";
    case ReachabilityFailure::guaranteed_loop_invalid:
        return "guaranteed_loop_invalid";
    }
    return "unknown_reachability_failure";
}

std::string format_reachability_issue(const ReachabilityIssue& issue)
{
    std::ostringstream output;
    output << reachability_failure_name(issue.failure);
    if (issue.chamber_id.has_value()) {
        output << " chamber=" << issue.chamber_id->value;
    }
    if (issue.edge.has_value()) {
        output << " edge=" << issue.edge->first.value << '-' << issue.edge->second.value;
    }
    if (issue.direction.has_value() && issue.edge.has_value()) {
        const bool forward{
            *issue.direction == RouteDirection::first_to_second};
        output << " direction="
               << (forward ? issue.edge->first.value : issue.edge->second.value)
               << "->"
               << (forward ? issue.edge->second.value : issue.edge->first.value);
    }
    if (issue.stable_object_id != 0U) {
        output << " object=" << issue.stable_object_id;
    }
    return output.str();
}

}  // namespace crystalbound
