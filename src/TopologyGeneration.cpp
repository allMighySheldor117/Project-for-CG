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

struct PlanarAnchor {
    std::int32_t x_millimetres{};
    std::int32_t z_millimetres{};
};

constexpr std::array<PlanarAnchor, 8> spacious_anchor_ring{
    PlanarAnchor{0, -48'000},
    PlanarAnchor{33'941, -33'941},
    PlanarAnchor{48'000, 0},
    PlanarAnchor{33'941, 33'941},
    PlanarAnchor{0, 48'000},
    PlanarAnchor{-33'941, 33'941},
    PlanarAnchor{-48'000, 0},
    PlanarAnchor{-33'941, -33'941},
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
    const Seed,
    const Edge edge,
    const std::vector<NodeId>&)
{
    return {edge, 0, 0, 0, false};
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

ChamberDimensionBand planned_chamber_dimension_band(
    const ChamberNode& node) noexcept
{
    if (node.role == ChamberRole::elemental && node.element.has_value()) {
        switch (*node.element) {
        case Element::fire:
            return {32'000, 33'000, 23'000, 24'000};
        case Element::water:
            return {9'300, 10'300, 5'600, 6'500};
        case Element::earth:
            return {24'000, 25'000, 13'000, 14'000};
        case Element::air:
            return {25'000, 26'000, 22'000, 23'000};
        case Element::aether:
            return {8'900, 9'900, 6'000, 7'000};
        }
    }
    switch (node.role) {
    case ChamberRole::start:
        return {9'000, 10'000, 5'000, 6'000};
    case ChamberRole::exit:
        return {9'500, 10'500, 5'500, 6'500};
    case ChamberRole::neutral:
        return {8'800, 9'800, 5'000, 6'000};
    case ChamberRole::elemental:
        break;
    }
    return {};
}

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
    std::vector<RoleAssignment> assignments{
        {ChamberRole::start, std::nullopt},
        {ChamberRole::elemental, Element::fire},
        {ChamberRole::elemental, Element::air},
        {ChamberRole::elemental, Element::earth},
        {ChamberRole::elemental, Element::water},
        {ChamberRole::elemental, Element::aether},
        {ChamberRole::exit, std::nullopt},
    };

    TopologyData topology;
    topology.nodes.reserve(assignments.size());
    for (std::size_t index{}; index < assignments.size(); ++index) {
        const NodeId id{static_cast<std::uint32_t>(index)};
        topology.nodes.push_back(ChamberNode{
            id,
            assignments[index].role,
            assignments[index].element,
            {},
        });
    }

    std::vector<NodeId> traversal_order;
    traversal_order.reserve(topology.nodes.size());
    for (const ChamberNode& node : topology.nodes) {
        traversal_order.push_back(node.id);
    }

    const std::array<Anchor, 7U> fixed_anchors{{
        {0, 0, 0, 90'000},
        {0, -authored_fire_landing_height_millimetres, 110'000, 270'000},
        {110'000, 0, 110'000, 90'000},
        {215'000, 0, 110'000, 180'000},
        {215'000, -authored_water_landing_height_millimetres,
            8'000, 270'000},
        {325'000, 0, 8'000, 0},
        {425'000, 0, 8'000, 0},
    }};
    for (std::size_t index{}; index < topology.nodes.size(); ++index) {
        topology.nodes[index].anchor = fixed_anchors[index];
    }

    for (std::size_t index{1U}; index < traversal_order.size(); ++index) {
        add_edge(topology.edges, traversal_order[index - 1U], traversal_order[index]);
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
        RoleAssignment{ChamberRole::elemental, Element::air},
        RoleAssignment{ChamberRole::elemental, Element::earth},
        RoleAssignment{ChamberRole::elemental, Element::water},
        RoleAssignment{ChamberRole::elemental, Element::aether},
        RoleAssignment{ChamberRole::exit, std::nullopt},
    };
    const std::array<Anchor, 7> anchors{
        Anchor{0, 0, 0, 90'000},
        Anchor{0, -authored_fire_landing_height_millimetres,
            110'000, 270'000},
        Anchor{110'000, 0, 110'000, 90'000},
        Anchor{215'000, 0, 110'000, 180'000},
        Anchor{215'000, -authored_water_landing_height_millimetres,
            8'000, 270'000},
        Anchor{325'000, 0, 8'000, 0},
        Anchor{425'000, 0, 8'000, 0},
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
        make_edge({1U}, {2U}),
        make_edge({2U}, {3U}),
        make_edge({3U}, {4U}),
        make_edge({4U}, {5U}),
        make_edge({5U}, {6U}),
    };
    std::sort(topology.edges.begin(), topology.edges.end());
    topology.routes = make_routes(
        fallback_effective_seed,
        topology.edges,
        topology.guaranteed_cycle);
    return topology;
}

}  // namespace crystalbound
