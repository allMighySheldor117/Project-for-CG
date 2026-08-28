#include "crystalbound/ProfileTraversal.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <vector>

namespace crystalbound {
namespace {

constexpr std::uint64_t fnv_offset_basis{14'695'981'039'346'656'037ULL};
constexpr std::uint64_t fnv_prime{1'099'511'628'211ULL};

void append_u8(std::uint64_t& hash, const std::uint8_t value) noexcept
{
    hash ^= value;
    hash *= fnv_prime;
}

void append_u32(std::uint64_t& hash, const std::uint32_t value) noexcept
{
    for (unsigned int shift{}; shift < 32U; shift += 8U) {
        append_u8(hash, static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

void append_i32(std::uint64_t& hash, const std::int32_t value) noexcept
{
    append_u32(hash, static_cast<std::uint32_t>(value));
}

[[nodiscard]] const ChamberNode& start_node(const TopologyData& topology)
{
    const auto found{std::find_if(
        topology.nodes.begin(), topology.nodes.end(), [](const ChamberNode& node) {
            return node.role == ChamberRole::start;
        })};
    if (found == topology.nodes.end()) {
        throw std::invalid_argument{"profile traversal requires a Start chamber"};
    }
    return *found;
}

[[nodiscard]] const RouteGeometryContract& route_for(
    const CaveSceneData& scene,
    const Edge edge)
{
    const auto found{std::find_if(
        scene.routes.begin(), scene.routes.end(), [edge](const RouteGeometryContract& route) {
            return route.edge == edge;
        })};
    if (found == scene.routes.end()) {
        throw std::invalid_argument{"profile traversal route is missing"};
    }
    return *found;
}

[[nodiscard]] const ChamberGeometryContract& chamber_for(
    const CaveSceneData& scene,
    const NodeId id)
{
    const auto found{std::find_if(
        scene.chambers.begin(), scene.chambers.end(), [id](const ChamberGeometryContract& chamber) {
            return chamber.node_id == id;
        })};
    if (found == scene.chambers.end()) {
        throw std::invalid_argument{"profile traversal chamber geometry is missing"};
    }
    return *found;
}

}  // namespace

ProfileTraversalWorkload build_profile_traversal_workload(
    const CaveGenerationResult& generation)
{
    const TopologyData& topology{generation.generation.topology};
    const ChamberNode& start{start_node(topology)};
    if (topology.nodes.size() < 2U
        || topology.edges.size() + 1U != topology.nodes.size()
        || !topology.guaranteed_cycle.empty()) {
        throw std::invalid_argument{
            "profile traversal requires the fixed linear chamber route"};
    }

    ProfileTraversalWorkload workload;
    std::vector<NodeId> outward_order{start.id};
    outward_order.reserve(topology.nodes.size());
    std::optional<NodeId> previous;
    while (outward_order.size() < topology.nodes.size()) {
        const NodeId current{outward_order.back()};
        std::vector<NodeId> next_nodes;
        for (const Edge edge : topology.edges) {
            if (edge.first == current) {
                next_nodes.push_back(edge.second);
            } else if (edge.second == current) {
                next_nodes.push_back(edge.first);
            }
        }
        next_nodes.erase(std::remove_if(next_nodes.begin(), next_nodes.end(),
            [&](const NodeId candidate) {
                return previous.has_value() && candidate == *previous;
            }), next_nodes.end());
        std::sort(next_nodes.begin(), next_nodes.end());
        if (next_nodes.size() != 1U
            || std::find(outward_order.begin(), outward_order.end(), next_nodes.front())
                != outward_order.end()) {
            throw std::invalid_argument{
                "profile traversal route is branched or cyclic"};
        }
        previous = current;
        outward_order.push_back(next_nodes.front());
    }
    const auto final_node{std::find_if(topology.nodes.begin(), topology.nodes.end(),
        [&](const ChamberNode& node) {
            return node.id == outward_order.back();
        })};
    if (final_node == topology.nodes.end()
        || final_node->role != ChamberRole::exit) {
        throw std::invalid_argument{
            "profile traversal linear route does not end at Exit"};
    }
    workload.chamber_visit_order.reserve(topology.nodes.size() * 2U - 1U);
    workload.chamber_visit_order = outward_order;
    for (std::size_t index{outward_order.size() - 1U}; index > 0U; --index) {
        workload.chamber_visit_order.push_back(outward_order[index - 1U]);
    }

    std::uint64_t hash{fnv_offset_basis};
    append_u32(hash, current_generator_version.value);
    for (const NodeId id : workload.chamber_visit_order) {
        append_u32(hash, id.value);
    }
    const ChamberGeometryContract& start_chamber{
        chamber_for(generation.scene, start.id)};
    workload.waypoints_metres.push_back({
        static_cast<double>(start_chamber.center_millimetres.x_millimetres) / 1'000.0,
        static_cast<double>(start_chamber.center_millimetres.y_millimetres) / 1'000.0,
        static_cast<double>(start_chamber.center_millimetres.z_millimetres) / 1'000.0,
    });

    for (std::size_t leg{}; leg + 1U < workload.chamber_visit_order.size(); ++leg) {
        const NodeId from{workload.chamber_visit_order[leg]};
        const NodeId to{workload.chamber_visit_order[leg + 1U]};
        const RouteGeometryContract& route{route_for(
            generation.scene, make_edge(from, to))};
        const bool reverse{route.edge.first != from};
        append_u32(hash, from.value);
        append_u32(hash, to.value);
        append_u32(hash, static_cast<std::uint32_t>(route.spline.control_points.size()));
        for (std::size_t step{}; step < route.spline.control_points.size(); ++step) {
            const std::size_t index{reverse
                    ? route.spline.control_points.size() - 1U - step
                    : step};
            const IntegerPoint3& control{route.spline.control_points[index]};
            append_i32(hash, control.x_millimetres);
            append_i32(hash, control.y_millimetres);
            append_i32(hash, control.z_millimetres);
        }

        std::vector<SplineSample> samples{sample_centripetal_catmull_rom(route.spline)};
        if (reverse) {
            std::reverse(samples.begin(), samples.end());
        }
        for (const SplineSample& sample : samples) {
            workload.waypoints_metres.push_back({
                sample.position_metres.x,
                sample.position_metres.y
                    - static_cast<double>(route.spline.radius_millimetres) / 1'000.0,
                sample.position_metres.z,
            });
        }
        const ChamberGeometryContract& destination{chamber_for(generation.scene, to)};
        workload.waypoints_metres.push_back({
            static_cast<double>(destination.center_millimetres.x_millimetres) / 1'000.0,
            static_cast<double>(destination.center_millimetres.y_millimetres) / 1'000.0,
            static_cast<double>(destination.center_millimetres.z_millimetres) / 1'000.0,
        });
    }
    workload.fingerprint = hash;
    return workload;
}

}  // namespace crystalbound
