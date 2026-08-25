#include "crystalbound/CaveScene.hpp"

#include <optional>
#include <string>
#include <utility>

namespace crystalbound {

CaveGenerationResult generate_cave(
    const Seed requested_seed,
    const CaveGenerationTestSeams& seams)
{
    std::optional<CaveSceneData> accepted_scene;
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
            accepted_scene = std::move(candidate);
            return std::nullopt;
        } catch (const GeometryError& error) {
            return std::string{"geometry rejected: "} + error.what();
        }
    };

    GenerationResult generation{generate_topology(requested_seed, topology_seams)};
    CaveSceneData scene;
    if (generation.used_fallback) {
        try {
            scene = build_cave_scene(generation.topology, generation.effective_seed);
        } catch (const GeometryError& error) {
            throw GenerationError{
                std::string{"Known-good fallback cave failed geometry validation: "}
                + error.what()};
        }
    } else {
        if (!accepted_scene.has_value()) {
            throw GenerationError{
                "Accepted topology did not retain its validated cave scene."};
        }
        scene = std::move(*accepted_scene);
    }

    if (!generation.diagnostics.empty()) {
        generation.diagnostics.back().message = generation.used_fallback
            ? "accepted validated fallback topology, geometry, and colliders"
            : "accepted validated topology, geometry, and colliders";
    }
    return {std::move(generation), std::move(scene)};
}

}  // namespace crystalbound
