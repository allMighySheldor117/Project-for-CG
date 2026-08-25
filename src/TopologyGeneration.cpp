#include "crystalbound/Generation.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

#include "crystalbound/DeterministicRandom.hpp"

namespace crystalbound {
namespace {

struct RoleAssignment {
    ChamberRole role{ChamberRole::neutral};
    std::optional<Element> element{};
};

template <typename Value>
void deterministic_shuffle(std::vector<Value>& values, SplitMix64& random)
{
    for (std::size_t remaining{values.size()}; remaining > 1U; --remaining) {
        const std::size_t selected{
            static_cast<std::size_t>(random.bounded(remaining))};
        std::swap(values[remaining - 1U], values[selected]);
    }
}

[[nodiscard]] std::int32_t signed_sample(
    SplitMix64& random,
    const std::int32_t inclusive_limit)
{
    const std::uint64_t range{
        static_cast<std::uint64_t>(inclusive_limit) * 2U + 1U};
    return static_cast<std::int32_t>(random.bounded(range)) - inclusive_limit;
}

[[nodiscard]] bool contains_edge(const std::vector<Edge>& edges, const Edge edge)
{
    return std::find(edges.begin(), edges.end(), edge) != edges.end();
}

void add_edge(std::vector<Edge>& edges, const NodeId left, const NodeId right)
{
    const Edge edge{make_edge(left, right)};
    if (edge.first != edge.second && !contains_edge(edges, edge)) {
        edges.push_back(edge);
    }
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

[[nodiscard]] RouteDescriptor make_route(
    const Seed seed,
    const Edge edge,
    const std::vector<NodeId>& guaranteed_cycle)
{
    SplitMix64 random{make_substream(seed.value, random_domain::routes, stable_edge_id(edge))};
    return RouteDescriptor{
        edge,
        signed_sample(random, topology_limits.route_lateral_offset_millimetres),
        signed_sample(random, topology_limits.route_elevation_offset_millimetres),
        static_cast<std::int32_t>(
            random.bounded(static_cast<std::uint64_t>(
                topology_limits.full_turn_millidegrees))),
        cycle_contains_edge(guaranteed_cycle, edge),
    };
}

[[nodiscard]] std::vector<RouteDescriptor> make_routes(
    const Seed seed,
    const std::vector<Edge>& edges,
    const std::vector<NodeId>& guaranteed_cycle)
{
    std::vector<RouteDescriptor> routes;
    routes.reserve(edges.size());
    for (const Edge edge : edges) {
        routes.push_back(make_route(seed, edge, guaranteed_cycle));
    }
    return routes;
}

}  // namespace

Seed derive_attempt_seed(const Seed requested_seed, const std::uint32_t attempt_index)
{
    if (attempt_index >= topology_limits.normal_attempt_count) {
        throw std::out_of_range("Topology attempt index must be in the range 0..7.");
    }
    if (attempt_index == 0U) {
        return requested_seed;
    }
    SplitMix64 random{
        make_substream(requested_seed.value, random_domain::retry, attempt_index)};
    return Seed{random.next()};
}

TopologyData generate_topology_attempt(const Seed attempt_seed)
{
    SplitMix64 topology_random{
        make_substream(attempt_seed.value, random_domain::topology, 0U)};

    std::vector<RoleAssignment> assignments{
        {ChamberRole::start, std::nullopt},
        {ChamberRole::elemental, Element::fire},
        {ChamberRole::elemental, Element::water},
        {ChamberRole::elemental, Element::earth},
        {ChamberRole::elemental, Element::air},
        {ChamberRole::elemental, Element::aether},
        {ChamberRole::exit, std::nullopt},
    };
    if (topology_random.boolean()) {
        assignments.push_back({ChamberRole::neutral, std::nullopt});
    }
    deterministic_shuffle(assignments, topology_random);

    TopologyData topology;
    topology.nodes.reserve(assignments.size());
    for (std::size_t index{}; index < assignments.size(); ++index) {
        const NodeId id{static_cast<std::uint32_t>(index)};
        SplitMix64 anchor_random{
            make_substream(attempt_seed.value, random_domain::anchors, id.value)};
        topology.nodes.push_back(ChamberNode{
            id,
            assignments[index].role,
            assignments[index].element,
            Anchor{
                signed_sample(
                    anchor_random,
                    topology_limits.horizontal_anchor_millimetres),
                signed_sample(
                    anchor_random,
                    topology_limits.elevation_anchor_millimetres),
                signed_sample(
                    anchor_random,
                    topology_limits.horizontal_anchor_millimetres),
                static_cast<std::int32_t>(anchor_random.bounded(
                    static_cast<std::uint64_t>(
                        topology_limits.full_turn_millidegrees))),
            },
        });
    }

    std::vector<NodeId> traversal_order;
    traversal_order.reserve(topology.nodes.size());
    for (const ChamberNode& node : topology.nodes) {
        traversal_order.push_back(node.id);
    }
    deterministic_shuffle(traversal_order, topology_random);

    for (std::size_t index{1U}; index < traversal_order.size(); ++index) {
        add_edge(topology.edges, traversal_order[index - 1U], traversal_order[index]);
    }
    const std::size_t cycle_end{2U + static_cast<std::size_t>(
        topology_random.bounded(traversal_order.size() - 2U))};
    topology.guaranteed_cycle.assign(
        traversal_order.begin(), traversal_order.begin() + cycle_end + 1U);
    add_edge(topology.edges, traversal_order.front(), traversal_order[cycle_end]);

    if (topology_random.boolean()) {
        for (std::size_t attempt{}; attempt < topology.nodes.size(); ++attempt) {
            const NodeId left{traversal_order[static_cast<std::size_t>(
                topology_random.bounded(traversal_order.size()))]};
            const NodeId right{traversal_order[static_cast<std::size_t>(
                topology_random.bounded(traversal_order.size()))]};
            const std::size_t previous_size{topology.edges.size()};
            add_edge(topology.edges, left, right);
            if (topology.edges.size() != previous_size) {
                break;
            }
        }
    }

    std::sort(topology.edges.begin(), topology.edges.end());
    topology.routes = make_routes(
        attempt_seed, topology.edges, topology.guaranteed_cycle);
    return topology;
}

TopologyData known_good_fallback_topology()
{
    const std::array<RoleAssignment, 7> assignments{
        RoleAssignment{ChamberRole::start, std::nullopt},
        RoleAssignment{ChamberRole::elemental, Element::fire},
        RoleAssignment{ChamberRole::elemental, Element::water},
        RoleAssignment{ChamberRole::elemental, Element::earth},
        RoleAssignment{ChamberRole::elemental, Element::air},
        RoleAssignment{ChamberRole::elemental, Element::aether},
        RoleAssignment{ChamberRole::exit, std::nullopt},
    };
    const std::array<Anchor, 7> anchors{
        Anchor{0, 0, 0, 0},
        Anchor{10'000, 0, 0, 0},
        Anchor{5'000, 1'000, 8'000, 120'000},
        Anchor{15'000, -1'000, 12'000, 180'000},
        Anchor{22'000, 2'000, 8'000, 220'000},
        Anchor{27'000, 0, 15'000, 270'000},
        Anchor{20'000, 500, 23'000, 320'000},
    };

    TopologyData topology;
    for (std::size_t index{}; index < assignments.size(); ++index) {
        topology.nodes.push_back(ChamberNode{
            NodeId{static_cast<std::uint32_t>(index)},
            assignments[index].role,
            assignments[index].element,
            anchors[index],
        });
    }
    topology.edges = {
        make_edge({0U}, {1U}),
        make_edge({0U}, {2U}),
        make_edge({1U}, {2U}),
        make_edge({2U}, {3U}),
        make_edge({3U}, {4U}),
        make_edge({4U}, {5U}),
        make_edge({5U}, {6U}),
    };
    std::sort(topology.edges.begin(), topology.edges.end());
    topology.guaranteed_cycle = {{0U}, {1U}, {2U}};
    topology.routes = make_routes(
        fallback_effective_seed,
        topology.edges,
        topology.guaranteed_cycle);
    return topology;
}

}  // namespace crystalbound
