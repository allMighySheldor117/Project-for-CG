#include "crystalbound/CaveScene.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "crystalbound/PlayerController.hpp"
#include "crystalbound/Reachability.hpp"

namespace crystalbound {

CaveGenerationResult generate_cave(
    const Seed requested_seed,
    const CaveGenerationTestSeams& seams)
{
    std::optional<CaveSceneData> accepted_scene;
    std::optional<MechanicalReachabilityReport> accepted_reachability;
    std::vector<std::optional<ReachabilityIssue>> mechanical_failures(
        topology_limits.normal_attempt_count);
    GenerationTestSeams topology_seams;
    topology_seams.fallback_factory = seams.fallback_factory;
    topology_seams.reject_attempt = [&](
                                           const std::uint32_t attempt_index,
                                           const TopologyData& topology)
        -> std::optional<std::string> {
        try {
            const Seed attempt_seed{derive_attempt_seed(requested_seed, attempt_index)};
            CaveSceneData candidate{build_cave_scene(topology, attempt_seed)};
            if (seams.reject_attempt) {
                const std::optional<std::string> forced_rejection{
                    seams.reject_attempt(attempt_index, candidate)};
                if (forced_rejection.has_value()) {
                    return forced_rejection;
                }
            }
            const CollisionWorld collision_world{build_collision_world(candidate)};
            MechanicalReachabilityReport reachability{
                validate_mechanical_reachability(topology, candidate, collision_world)};
            if (seams.reject_mechanical) {
                const std::optional<ReachabilityIssue> forced_rejection{
                    seams.reject_mechanical(
                        attempt_index, false, reachability)};
                if (forced_rejection.has_value()) {
                    reachability.issues.push_back(*forced_rejection);
                    reachability.accepted = false;
                }
            }
            if (!reachability.accepted) {
                if (reachability.issues.empty()) {
                    return "mechanical reachability rejected without a typed issue";
                }
                mechanical_failures[attempt_index] = reachability.issues.front();
                return std::string{"mechanical reachability rejected: "}
                    + format_reachability_issue(reachability.issues.front());
            }
            accepted_scene = std::move(candidate);
            accepted_reachability = std::move(reachability);
            return std::nullopt;
        } catch (const GeometryError& error) {
            return std::string{"geometry rejected: "} + error.what();
        } catch (const ControllerError& error) {
            return std::string{"collision rejected: "} + error.what();
        }
    };

    GenerationResult generation{generate_topology(requested_seed, topology_seams)};
    for (GenerationDiagnostic& diagnostic : generation.diagnostics) {
        if (diagnostic.attempt_index < mechanical_failures.size()) {
            diagnostic.mechanical_failure =
                mechanical_failures[diagnostic.attempt_index];
        }
    }
    CaveSceneData scene;
    MechanicalReachabilityReport reachability;
    if (generation.used_fallback) {
        try {
            scene = build_cave_scene(generation.topology, generation.effective_seed);
            const CollisionWorld collision_world{build_collision_world(scene)};
            reachability = validate_mechanical_reachability(
                generation.topology, scene, collision_world);
            if (seams.reject_mechanical) {
                const std::optional<ReachabilityIssue> forced_rejection{
                    seams.reject_mechanical(
                        topology_limits.normal_attempt_count, true, reachability)};
                if (forced_rejection.has_value()) {
                    reachability.issues.push_back(*forced_rejection);
                    reachability.accepted = false;
                }
            }
            if (!reachability.accepted) {
                const std::string detail{reachability.issues.empty()
                        ? "unknown_reachability_failure"
                        : format_reachability_issue(reachability.issues.front())};
                throw GenerationError{
                    "Known-good fallback cave failed mechanical reachability validation: "
                    + detail};
            }
        } catch (const GeometryError& error) {
            throw GenerationError{
                std::string{"Known-good fallback cave failed geometry validation: "}
                + error.what()};
        } catch (const ControllerError& error) {
            throw GenerationError{
                std::string{"Known-good fallback cave failed collision validation: "}
                + error.what()};
        }
    } else {
        if (!accepted_scene.has_value() || !accepted_reachability.has_value()) {
            throw GenerationError{
                "Accepted topology did not retain its validated complete cave candidate."};
        }
        scene = std::move(*accepted_scene);
        reachability = std::move(*accepted_reachability);
    }

    if (!generation.diagnostics.empty()) {
        generation.diagnostics.back().message = generation.used_fallback
            ? "accepted validated fallback topology, geometry, colliders, and mechanical reachability"
            : "accepted validated topology, geometry, colliders, and mechanical reachability";
    }
    return {
        std::move(generation),
        std::move(scene),
        std::move(reachability),
    };
}

}  // namespace crystalbound
