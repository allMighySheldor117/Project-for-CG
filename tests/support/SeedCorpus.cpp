#include "SeedCorpus.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <queue>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "crystalbound/CaveScene.hpp"
#include "crystalbound/CrystalCollection.hpp"
#include "crystalbound/ElementalVisuals.hpp"
#include "crystalbound/ExitArch.hpp"
#include "crystalbound/PlayerController.hpp"
#include "crystalbound/Reachability.hpp"

namespace crystalbound::test {
namespace {

[[nodiscard]] std::string diagnostics_text(const GenerationResult& generation)
{
    std::ostringstream output;
    for (const GenerationDiagnostic& diagnostic : generation.diagnostics) {
        if (output.tellp() > 0) {
            output << " | ";
        }
        output << "attempt=" << diagnostic.attempt_index
               << ",seed=" << diagnostic.attempt_seed.value
               << ",message=" << diagnostic.message;
        if (diagnostic.mechanical_failure.has_value()) {
            output << ",mechanical="
                   << format_reachability_issue(*diagnostic.mechanical_failure);
        }
    }
    return output.str();
}

[[noreturn]] void fail(
    const CaveGenerationResult& result,
    const std::string_view contract,
    const std::string_view detail)
{
    std::ostringstream message;
    message << "requested_seed=" << result.generation.requested_seed.value
            << " attempt_seed=" << result.generation.attempt_seed.value
            << " effective_seed=" << result.generation.effective_seed.value
            << " fallback=" << (result.generation.used_fallback ? "true" : "false")
            << " contract=" << contract
            << " detail=" << detail
            << " diagnostics=[" << diagnostics_text(result.generation) << ']';
    throw SeedCorpusError{message.str()};
}

void require(
    const bool condition,
    const CaveGenerationResult& result,
    const std::string_view contract,
    const std::string_view detail)
{
    if (!condition) {
        fail(result, contract, detail);
    }
}

void require_no_errors(
    const std::vector<std::string>& errors,
    const CaveGenerationResult& result,
    const std::string_view contract)
{
    if (errors.empty()) {
        return;
    }
    std::ostringstream detail;
    for (std::size_t index{}; index < errors.size(); ++index) {
        if (index != 0U) {
            detail << " | ";
        }
        detail << errors[index];
    }
    fail(result, contract, detail.str());
}

[[nodiscard]] bool finite_vector(const GeometryVector3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] const ChamberNode* node_with_role(
    const TopologyData& topology,
    const ChamberRole role) noexcept
{
    const auto found{std::find_if(
        topology.nodes.begin(), topology.nodes.end(), [role](const ChamberNode& node) {
            return node.role == role;
        })};
    return found == topology.nodes.end() ? nullptr : &*found;
}

[[nodiscard]] std::vector<NodeId> neighbours_without_edge(
    const TopologyData& topology,
    const NodeId node,
    const std::optional<Edge> omitted)
{
    std::vector<NodeId> neighbours;
    for (const Edge edge : topology.edges) {
        if (omitted.has_value() && edge == *omitted) {
            continue;
        }
        if (edge.first == node) {
            neighbours.push_back(edge.second);
        } else if (edge.second == node) {
            neighbours.push_back(edge.first);
        }
    }
    return neighbours;
}

[[nodiscard]] std::optional<std::uint32_t> graph_distance(
    const TopologyData& topology,
    const NodeId start,
    const NodeId goal,
    const std::optional<Edge> omitted = std::nullopt)
{
    std::queue<std::pair<NodeId, std::uint32_t>> pending;
    std::vector<NodeId> visited{start};
    pending.push({start, 0U});
    while (!pending.empty()) {
        const auto [current, distance]{pending.front()};
        pending.pop();
        if (current == goal) {
            return distance;
        }
        for (const NodeId neighbour : neighbours_without_edge(topology, current, omitted)) {
            if (std::find(visited.begin(), visited.end(), neighbour) == visited.end()) {
                visited.push_back(neighbour);
                pending.push({neighbour, distance + 1U});
            }
        }
    }
    return std::nullopt;
}

[[nodiscard]] bool diagnostics_equal(
    const GenerationResult& left,
    const GenerationResult& right) noexcept
{
    if (left.diagnostics.size() != right.diagnostics.size()) {
        return false;
    }
    for (std::size_t index{}; index < left.diagnostics.size(); ++index) {
        const GenerationDiagnostic& first{left.diagnostics[index]};
        const GenerationDiagnostic& second{right.diagnostics[index]};
        if (first.attempt_index != second.attempt_index
            || first.attempt_seed != second.attempt_seed
            || first.outcome != second.outcome
            || first.message != second.message
            || !(first.mechanical_failure == second.mechanical_failure)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] SeedCorpusResult validate_complete_result(
    const Seed requested_seed,
    const CaveGenerationResult& result)
{
    const GenerationResult& generation{result.generation};
    const TopologyData& topology{generation.topology};
    const CaveSceneData& scene{result.scene};
    require(
        generation.requested_seed == requested_seed,
        result,
        "requested seed",
        "generator returned a different requested seed");
    require_no_errors(validate_topology(topology), result, "topology validation");
    require_no_errors(validate_cave_scene(topology, scene), result, "cave scene validation");
    require(
        generation.fingerprint == topology_fingerprint(
            generation.requested_seed,
            generation.attempt_seed,
            generation.effective_seed,
            topology),
        result,
        "topology fingerprint",
        "stored topology fingerprint is not canonical");
    require(
        scene.fingerprint == cave_scene_fingerprint(generation.effective_seed, scene),
        result,
        "scene fingerprint",
        "stored scene fingerprint is not canonical");
    require(
        scene.elemental_visuals.fingerprint
            == elemental_scene_fingerprint(generation.effective_seed, scene.elemental_visuals),
        result,
        "elemental fingerprint",
        "stored elemental fingerprint is not canonical");

    const CollisionWorld collision{build_collision_world(scene)};
    require_no_errors(validate_collision_world(collision), result, "collision validation");
    const MechanicalReachabilityReport reachability{
        validate_mechanical_reachability(topology, scene, collision)};
    require(reachability.accepted, result, "mechanical reachability", "fresh validation rejected cave");
    require(
        reachability == result.reachability,
        result,
        "mechanical reachability",
        "stored reachability report differs from fresh validation");
    require(
        reachability.reachable_chambers.size() == topology.nodes.size(),
        result,
        "required chamber reachability",
        "not every generated chamber is mechanically reachable");
    require(
        std::all_of(reachability.respawns.begin(), reachability.respawns.end(),
            [](const ChamberRespawnVerdict& respawn) { return respawn.safe; }),
        result,
        "safe respawns",
        "at least one chamber respawn is unsafe");

    require(topology.guaranteed_cycle.empty(), result,
        "fixed linear route", "topology unexpectedly retained a guaranteed loop");
    require(scene.bridge_routes.empty(), result,
        "tunnel-only route", "fixed authored layout contains a bridge route");
    require(std::all_of(scene.routes.begin(), scene.routes.end(),
                [](const RouteGeometryContract& route) { return !route.bridge; }),
        result,
        "tunnel-only route",
        "fixed authored layout contains bridge geometry");

    const std::vector<CrystalInteractionTarget> targets{
        build_crystal_interaction_targets(scene.elemental_visuals)};
    require(targets.size() == 5U, result, "crystal targets", "cave does not have five crystals");
    std::set<Element> target_elements;
    std::set<std::uint64_t> target_ids;
    CrystalCollectionState collection;
    for (const CrystalInteractionTarget& target : targets) {
        target_elements.insert(target.element);
        target_ids.insert(target.stable_object_id);
        require(
            target.stable_object_id != 0U && finite_vector(target.position_metres),
            result,
            "crystal targets",
            "crystal target identity or position is invalid");
        require(
            collection.collect(target.element),
            result,
            "crystal collection",
            "distinct crystal could not be collected");
    }
    require(
        target_elements.size() == 5U && target_ids.size() == 5U && collection.all_collected(),
        result,
        "crystal collection",
        "five distinct elemental crystals were not collectable");
    const VisibilityWorld visibility{build_crystal_visibility_world(scene)};
    require(
        !visibility.chambers.empty() && !visibility.routes.empty(),
        result,
        "crystal visibility",
        "visibility world is partial");

    const ExitArchData arch{build_exit_arch(result)};
    require_no_errors(validate_exit_arch(result, arch), result, "exit arch validation");
    const ExitArchDisplayState display{exit_arch_display_state(arch, collection)};
    require(
        display.active
            && std::all_of(display.filled.displayed.begin(), display.filled.displayed.end(),
                [](const bool filled) { return filled; }),
        result,
        "exit arch activation",
        "fully collected crystal state did not activate all exit sockets");

    const ChamberNode* start{node_with_role(topology, ChamberRole::start)};
    const ChamberNode* exit{node_with_role(topology, ChamberRole::exit)};
    require(start != nullptr && exit != nullptr, result, "chamber roles", "Start or Exit is missing");
    const std::optional<std::uint32_t> exit_distance{graph_distance(topology, start->id, exit->id)};
    require(exit_distance.has_value(), result, "exit placement", "Exit is disconnected from Start");
    std::uint32_t farthest_distance{};
    for (const ChamberNode& node : topology.nodes) {
        const std::optional<std::uint32_t> distance{graph_distance(topology, start->id, node.id)};
        require(distance.has_value(), result, "graph connectivity", "generated chamber is disconnected");
        farthest_distance = std::max(farthest_distance, *distance);
    }
    const auto [minimum_elevation, maximum_elevation]{std::minmax_element(
        topology.nodes.begin(), topology.nodes.end(), [](const ChamberNode& left, const ChamberNode& right) {
            return left.anchor.elevation_millimetres < right.anchor.elevation_millimetres;
        })};

    return {
        generation.requested_seed,
        generation.attempt_seed,
        generation.effective_seed,
        generation.used_fallback,
        generation.diagnostics.size(),
        generation.fingerprint,
        scene.fingerprint,
        scene.elemental_visuals.fingerprint,
        arch.fingerprint,
        static_cast<std::uint32_t>(topology.nodes.size()),
        static_cast<std::uint32_t>(topology.edges.size()),
        *exit_distance,
        farthest_distance,
        maximum_elevation->anchor.elevation_millimetres
            - minimum_elevation->anchor.elevation_millimetres,
        static_cast<std::uint32_t>(scene.bridge_routes.size()),
        scene.static_vertex_count + scene.elemental_visuals.generated_vertex_count,
        scene.opaque_draw_call_count + scene.elemental_visuals.opaque_draw_call_count,
        scene.elemental_visuals.transparent_effect_draw_count,
        scene.elemental_visuals.particle_count,
        generation.generator_version.value,
        normalized_layout_fingerprint(result),
        structural_component_fingerprint(result),
        generation.diagnostics,
    };
}

void require_repeatable(
    const CaveGenerationResult& first,
    const CaveGenerationResult& second,
    const SeedCorpusResult& first_summary,
    const SeedCorpusResult& second_summary)
{
    const bool stable{
        first.generation.requested_seed == second.generation.requested_seed
        && first.generation.attempt_seed == second.generation.attempt_seed
        && first.generation.effective_seed == second.generation.effective_seed
        && first.generation.used_fallback == second.generation.used_fallback
        && first.generation.generator_version.value == second.generation.generator_version.value
        && first.generation.topology == second.generation.topology
        && first.generation.fingerprint == second.generation.fingerprint
        && first.scene.fingerprint == second.scene.fingerprint
        && first.scene.elemental_visuals.fingerprint == second.scene.elemental_visuals.fingerprint
        && first.reachability == second.reachability
        && diagnostics_equal(first.generation, second.generation)
        && first_summary.exit_arch_fingerprint == second_summary.exit_arch_fingerprint};
    require(stable, first, "stable repeat execution", "repeated complete generation changed");
}

}  // namespace

std::vector<Seed> stable_seed_corpus(const std::size_t count)
{
    constexpr std::size_t minimum_count_for_golden_seeds{44U};
    if (count < minimum_count_for_golden_seeds) {
        throw std::invalid_argument(
            "Stable corpus count must be at least 44 to include seeds 42 and 123456789.");
    }
    std::vector<Seed> seeds;
    seeds.reserve(count);
    for (std::size_t value{}; value + 1U < count; ++value) {
        seeds.push_back({static_cast<std::uint64_t>(value)});
    }
    seeds.push_back(corpus_reference_seed);
    return seeds;
}

SeedCorpusReport run_seed_corpus(const std::vector<Seed>& seeds)
{
    if (seeds.empty()) {
        throw std::invalid_argument("Seed corpus must not be empty.");
    }
    SeedCorpusReport report;
    std::set<std::uint64_t> unique_normal_layouts;
    report.requested_seed_count = seeds.size();
    report.results.reserve(seeds.size());
    for (const Seed requested_seed : seeds) {
        try {
            const CaveGenerationResult first{generate_cave(requested_seed)};
            const SeedCorpusResult first_summary{
                validate_complete_result(requested_seed, first)};
            const CaveGenerationResult second{generate_cave(requested_seed)};
            const SeedCorpusResult second_summary{
                validate_complete_result(requested_seed, second)};
            require_repeatable(first, second, first_summary, second_summary);
            if (first.generation.used_fallback) {
                ++report.fallback_acceptance_count;
            } else {
                ++report.normal_acceptance_count;
                unique_normal_layouts.insert(
                    first_summary.normalized_layout_fingerprint);
            }
            report.results.push_back(first_summary);
        } catch (const SeedCorpusError&) {
            throw;
        } catch (const std::exception& error) {
            throw SeedCorpusError{
                "requested_seed=" + std::to_string(requested_seed.value)
                + " attempt_seed=unavailable effective_seed=unavailable fallback=unavailable"
                  " contract=complete generation detail="
                + error.what()};
        }
    }
    report.unique_normal_layout_count = unique_normal_layouts.size();
    return report;
}

}  // namespace crystalbound::test
