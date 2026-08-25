#include "crystalbound/Generation.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <queue>
#include <utility>
#include <vector>

namespace crystalbound {
namespace {

[[nodiscard]] const ChamberNode* find_node(
    const TopologyData& topology,
    const NodeId id)
{
    const auto found = std::find_if(
        topology.nodes.begin(), topology.nodes.end(), [id](const ChamberNode& node) {
            return node.id == id;
        });
    return found == topology.nodes.end() ? nullptr : &*found;
}

[[nodiscard]] bool contains_edge(const std::vector<Edge>& edges, const Edge edge)
{
    return std::find(edges.begin(), edges.end(), edge) != edges.end();
}

[[nodiscard]] bool cycle_contains_edge(
    const std::vector<NodeId>& cycle,
    const Edge edge)
{
    if (cycle.size() < 3U) {
        return false;
    }
    for (std::size_t index{}; index < cycle.size(); ++index) {
        const NodeId next{cycle[(index + 1U) % cycle.size()]};
        if (make_edge(cycle[index], next) == edge) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] std::vector<NodeId> neighbours(
    const TopologyData& topology,
    const NodeId id)
{
    std::vector<NodeId> result;
    for (const Edge edge : topology.edges) {
        if (edge.first == id) {
            result.push_back(edge.second);
        } else if (edge.second == id) {
            result.push_back(edge.first);
        }
    }
    return result;
}

[[nodiscard]] std::optional<std::uint32_t> graph_distance(
    const TopologyData& topology,
    const NodeId start,
    const NodeId goal)
{
    std::queue<std::pair<NodeId, std::uint32_t>> pending;
    std::vector<NodeId> visited;
    pending.push({start, 0U});
    visited.push_back(start);

    while (!pending.empty()) {
        const auto [current, distance] = pending.front();
        pending.pop();
        if (current == goal) {
            return distance;
        }
        for (const NodeId neighbour : neighbours(topology, current)) {
            if (std::find(visited.begin(), visited.end(), neighbour) == visited.end()) {
                visited.push_back(neighbour);
                pending.push({neighbour, distance + 1U});
            }
        }
    }
    return std::nullopt;
}

void validate_node_contract(
    const TopologyData& topology,
    std::vector<std::string>& errors,
    std::optional<NodeId>& start_id,
    std::optional<NodeId>& exit_id)
{
    if (topology.nodes.size() < 7U || topology.nodes.size() > 8U) {
        errors.push_back("topology must contain seven or eight nodes");
    }
    if (!std::is_sorted(
            topology.nodes.begin(), topology.nodes.end(),
            [](const ChamberNode& left, const ChamberNode& right) {
                return left.id < right.id;
            })) {
        errors.push_back("nodes must be sorted by stable ID");
    }

    std::size_t start_count{};
    std::size_t exit_count{};
    std::size_t neutral_count{};
    std::array<std::size_t, 5> element_counts{};
    std::vector<NodeId> ids;
    for (const ChamberNode& node : topology.nodes) {
        if (std::find(ids.begin(), ids.end(), node.id) != ids.end()) {
            errors.push_back("node IDs must be unique");
        }
        ids.push_back(node.id);
        if (node.anchor.x_millimetres < -topology_limits.horizontal_anchor_millimetres
            || node.anchor.x_millimetres > topology_limits.horizontal_anchor_millimetres
            || node.anchor.z_millimetres < -topology_limits.horizontal_anchor_millimetres
            || node.anchor.z_millimetres > topology_limits.horizontal_anchor_millimetres
            || node.anchor.elevation_millimetres
                < -topology_limits.elevation_anchor_millimetres
            || node.anchor.elevation_millimetres
                > topology_limits.elevation_anchor_millimetres
            || node.anchor.heading_millidegrees < 0
            || node.anchor.heading_millidegrees
                >= topology_limits.full_turn_millidegrees) {
            errors.push_back("node anchor is outside deterministic bounds");
        }
        switch (node.role) {
        case ChamberRole::start:
            ++start_count;
            start_id = node.id;
            if (node.element.has_value()) {
                errors.push_back("Start must not have an element");
            }
            break;
        case ChamberRole::exit:
            ++exit_count;
            exit_id = node.id;
            if (node.element.has_value()) {
                errors.push_back("Exit must not have an element");
            }
            break;
        case ChamberRole::neutral:
            ++neutral_count;
            if (node.element.has_value()) {
                errors.push_back("Neutral must not have an element");
            }
            break;
        case ChamberRole::elemental:
            if (!node.element.has_value()) {
                errors.push_back("elemental chamber must identify its element");
            } else {
                const std::size_t element_index{
                    static_cast<std::size_t>(*node.element)};
                if (element_index >= element_counts.size()) {
                    errors.push_back("elemental chamber has an unknown element");
                } else {
                    ++element_counts[element_index];
                }
            }
            break;
        }
    }
    if (start_count != 1U) {
        errors.push_back("topology must contain exactly one Start");
    }
    if (exit_count != 1U) {
        errors.push_back("topology must contain exactly one Exit");
    }
    if (neutral_count > 1U || topology.nodes.size() != 7U + neutral_count) {
        errors.push_back("Neutral count must match the seven/eight-node contract");
    }
    if (std::any_of(element_counts.begin(), element_counts.end(), [](const std::size_t count) {
            return count != 1U;
        })) {
        errors.push_back("topology must contain each of the five elements exactly once");
    }
}

void validate_edge_contract(
    const TopologyData& topology,
    std::vector<std::string>& errors)
{
    if (!std::is_sorted(topology.edges.begin(), topology.edges.end())) {
        errors.push_back("edges must use canonical sorted order");
    }
    for (std::size_t index{}; index < topology.edges.size(); ++index) {
        const Edge edge{topology.edges[index]};
        if (!(edge.first < edge.second)) {
            errors.push_back("edges must be normalized and contain distinct endpoints");
        }
        if (find_node(topology, edge.first) == nullptr
            || find_node(topology, edge.second) == nullptr) {
            errors.push_back("edge endpoint must reference an existing node");
        }
        if (index > 0U && topology.edges[index - 1U] == edge) {
            errors.push_back("duplicate edges are not allowed");
        }
    }
}

void validate_cycle_contract(
    const TopologyData& topology,
    std::vector<std::string>& errors)
{
    if (topology.guaranteed_cycle.size() < 3U) {
        errors.push_back("guaranteed cycle must contain at least three nodes");
        return;
    }

    std::vector<NodeId> cycle_ids;
    for (const NodeId id : topology.guaranteed_cycle) {
        if (find_node(topology, id) == nullptr) {
            errors.push_back("guaranteed cycle references an unknown node");
        }
        if (std::find(cycle_ids.begin(), cycle_ids.end(), id) != cycle_ids.end()) {
            errors.push_back("guaranteed cycle nodes must be distinct");
        }
        cycle_ids.push_back(id);
    }
    for (std::size_t index{}; index < topology.guaranteed_cycle.size(); ++index) {
        const Edge edge{make_edge(
            topology.guaranteed_cycle[index],
            topology.guaranteed_cycle[(index + 1U) % topology.guaranteed_cycle.size()])};
        if (!contains_edge(topology.edges, edge)) {
            errors.push_back("guaranteed cycle edge is missing");
        }
    }
}

void validate_route_contract(
    const TopologyData& topology,
    std::vector<std::string>& errors)
{
    if (topology.routes.size() != topology.edges.size()) {
        errors.push_back("every edge must have exactly one route descriptor");
        return;
    }
    for (std::size_t index{}; index < topology.routes.size(); ++index) {
        const RouteDescriptor& route = topology.routes[index];
        if (route.edge != topology.edges[index]) {
            errors.push_back("routes must follow canonical edge order");
        }
        if (route.lateral_offset_millimetres
                < -topology_limits.route_lateral_offset_millimetres
            || route.lateral_offset_millimetres
                > topology_limits.route_lateral_offset_millimetres
            || route.elevation_offset_millimetres
                < -topology_limits.route_elevation_offset_millimetres
            || route.elevation_offset_millimetres
                > topology_limits.route_elevation_offset_millimetres
            || route.heading_millidegrees < 0
            || route.heading_millidegrees >= topology_limits.full_turn_millidegrees) {
            errors.push_back("route descriptor is outside deterministic bounds");
        }
        if (route.on_guaranteed_cycle
            != cycle_contains_edge(topology.guaranteed_cycle, route.edge)) {
            errors.push_back("route cycle marker disagrees with guaranteed cycle");
        }
    }
}

}  // namespace

std::vector<std::string> validate_topology(const TopologyData& topology)
{
    std::vector<std::string> errors;
    std::optional<NodeId> start_id;
    std::optional<NodeId> exit_id;
    validate_node_contract(topology, errors, start_id, exit_id);
    validate_edge_contract(topology, errors);
    validate_cycle_contract(topology, errors);
    validate_route_contract(topology, errors);

    if (start_id.has_value()) {
        for (const ChamberNode& node : topology.nodes) {
            if (!graph_distance(topology, *start_id, node.id).has_value()) {
                errors.push_back("Start must reach every chamber");
                break;
            }
        }
    }
    if (start_id.has_value() && exit_id.has_value()) {
        const std::optional<std::uint32_t> distance{
            graph_distance(topology, *start_id, *exit_id)};
        if (!distance.has_value() || *distance < 1U) {
            errors.push_back("Exit graph distance from Start must be at least one");
        }
    }
    return errors;
}

}  // namespace crystalbound
