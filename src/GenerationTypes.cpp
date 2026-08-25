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
