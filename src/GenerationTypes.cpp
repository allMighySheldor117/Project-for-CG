#include "crystalbound/Generation.hpp"

namespace crystalbound {

bool operator==(const Anchor& left, const Anchor& right) noexcept
{
    return left.x_millimetres == right.x_millimetres
        && left.elevation_millimetres == right.elevation_millimetres
        && left.z_millimetres == right.z_millimetres
        && left.heading_millidegrees == right.heading_millidegrees;
}

bool operator==(const ChamberNode& left, const ChamberNode& right) noexcept
{
    return left.id == right.id && left.role == right.role && left.element == right.element
        && left.anchor == right.anchor;
}

bool operator==(const Edge& left, const Edge& right) noexcept
{
    return left.first == right.first && left.second == right.second;
}

bool operator!=(const Edge& left, const Edge& right) noexcept
{
    return !(left == right);
}

bool operator<(const Edge& left, const Edge& right) noexcept
{
    return left.first < right.first
        || (left.first == right.first && left.second < right.second);
}

bool operator==(const ReachabilityIssue& left, const ReachabilityIssue& right) noexcept
{
    return left.failure == right.failure && left.chamber_id == right.chamber_id
        && left.edge == right.edge && left.direction == right.direction
        && left.stable_object_id == right.stable_object_id;
}

bool operator==(
    const DirectedRouteTraversal& left,
    const DirectedRouteTraversal& right) noexcept
{
    return left.edge == right.edge && left.from == right.from && left.to == right.to
        && left.direction == right.direction
        && left.stable_object_id == right.stable_object_id && left.bridge == right.bridge
        && left.traversable == right.traversable && left.failures == right.failures;
}

bool operator==(
    const ChamberRespawnVerdict& left,
    const ChamberRespawnVerdict& right) noexcept
{
    return left.chamber_id == right.chamber_id
        && left.stable_object_id == right.stable_object_id && left.safe == right.safe
        && left.failures == right.failures;
}

bool operator==(
    const MechanicalReachabilityReport& left,
    const MechanicalReachabilityReport& right) noexcept
{
    return left.accepted == right.accepted
        && left.start_chamber == right.start_chamber
        && left.reachable_chambers == right.reachable_chambers
        && left.required_unreachable_chambers == right.required_unreachable_chambers
        && left.directed_routes == right.directed_routes
        && left.respawns == right.respawns && left.issues == right.issues
        && left.diagnostics == right.diagnostics;
}

Edge make_edge(const NodeId left, const NodeId right) noexcept
{
    return right < left ? Edge{right, left} : Edge{left, right};
}

std::uint64_t stable_edge_id(const Edge& edge) noexcept
{
    const Edge normalized{make_edge(edge.first, edge.second)};
    return (static_cast<std::uint64_t>(normalized.first.value) << 32U)
        | static_cast<std::uint64_t>(normalized.second.value);
}

bool operator==(const RouteDescriptor& left, const RouteDescriptor& right) noexcept
{
    return left.edge == right.edge
        && left.lateral_offset_millimetres == right.lateral_offset_millimetres
        && left.elevation_offset_millimetres == right.elevation_offset_millimetres
        && left.heading_millidegrees == right.heading_millidegrees
        && left.on_guaranteed_cycle == right.on_guaranteed_cycle;
}

bool operator==(const TopologyData& left, const TopologyData& right) noexcept
{
    return left.nodes == right.nodes && left.edges == right.edges
        && left.routes == right.routes && left.guaranteed_cycle == right.guaranteed_cycle;
}

}  // namespace crystalbound
