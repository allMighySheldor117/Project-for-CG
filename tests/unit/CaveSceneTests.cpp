#include "CaveSceneTests.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "crystalbound/CaveScene.hpp"

namespace crystalbound::test {
namespace {

class CaveSceneTestFailure final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

void require(const bool condition, const std::string_view message)
{
    if (!condition) {
        throw CaveSceneTestFailure{std::string{message}};
    }
}

template <typename Exception, typename Function>
void require_throws(
    Function&& function,
    const std::string_view expected_message,
    const std::string_view failure_message)
{
    try {
        function();
    } catch (const Exception& error) {
        if (std::string_view{error.what()}.find(expected_message) == std::string_view::npos) {
            throw CaveSceneTestFailure{
                std::string{failure_message} + ": unexpected message: " + error.what()};
        }
        return;
    } catch (const std::exception& error) {
        throw CaveSceneTestFailure{
            std::string{failure_message} + ": wrong exception type: " + error.what()};
    }
    throw CaveSceneTestFailure{std::string{failure_message} + ": no exception was thrown"};
}

[[nodiscard]] bool has_collider(
    const CaveSceneData& scene,
    const ColliderKind kind)
{
    return std::any_of(
        scene.colliders.begin(), scene.colliders.end(), [kind](const SceneCollider& collider) {
            return collider.kind == kind;
        });
}

[[nodiscard]] bool has_piece(
    const CaveSceneData& scene,
    const ScenePieceKind kind)
{
    return std::any_of(
        scene.mesh_pieces.begin(), scene.mesh_pieces.end(), [kind](const SceneMeshPiece& piece) {
            return piece.kind == kind;
        });
}

void fixed_seed_builds_complete_scene(const std::filesystem::path&)
{
    const CaveGenerationResult result{generate_cave({42U})};
    require(!result.generation.used_fallback, "canonical scene seed unexpectedly used fallback");
    require(
        validate_cave_scene(result.generation.topology, result.scene).empty(),
        "fixed seed scene did not validate");
    require(
        result.scene.chambers.size() == result.generation.topology.nodes.size(),
        "fixed seed lost a chamber");
    require(
        result.scene.routes.size() == result.generation.topology.routes.size(),
        "fixed seed lost a route");
    require(
        result.scene.portals.size() == result.scene.routes.size() * 2U,
        "fixed seed lost route portals");
    require(!result.scene.bridge_routes.empty(), "fixed seed did not generate a bridge");
    require(result.scene.static_vertex_count > 0U, "fixed seed generated no vertices");
    require(
        format_fingerprint(result.scene.fingerprint) == "9fb15c446b74730d",
        "canonical Step 5B scene fingerprint changed");
}

void same_seed_repeats_scene_contract(const std::filesystem::path&)
{
    const CaveGenerationResult first{generate_cave({123'456'789U})};
    const CaveGenerationResult second{generate_cave({123'456'789U})};
    require(
        first.generation.effective_seed == second.generation.effective_seed,
        "same seed changed effective seed");
    require(
        first.generation.fingerprint == second.generation.fingerprint,
        "same seed changed topology fingerprint");
    require(
        first.scene.fingerprint == second.scene.fingerprint,
        "same seed changed scene fingerprint");
    require(first.scene.bridge_routes == second.scene.bridge_routes, "same seed changed bridge route");
    require(
        first.scene.static_vertex_count == second.scene.static_vertex_count,
        "same seed changed vertex count");
}

void representative_seeds_vary(const std::filesystem::path&)
{
    std::vector<std::uint64_t> fingerprints;
    for (const std::uint64_t seed : {0U, 1U, 42U, 123'456'789U, 987'654'321U}) {
        const CaveGenerationResult result{generate_cave({seed})};
        fingerprints.push_back(result.scene.fingerprint);
    }
    std::sort(fingerprints.begin(), fingerprints.end());
    fingerprints.erase(std::unique(fingerprints.begin(), fingerprints.end()), fingerprints.end());
    require(fingerprints.size() >= 2U, "representative seeds did not vary the scene contract");
}

void geometry_pieces_are_valid_and_budgeted(const std::filesystem::path&)
{
    const CaveGenerationResult result{generate_cave({123'456'789U})};
    std::uint64_t vertices{};
    for (const SceneMeshPiece& piece : result.scene.mesh_pieces) {
        validate_procedural_mesh(piece.mesh);
        vertices += piece.mesh.vertices.size();
    }
    require(vertices == result.scene.static_vertex_count, "scene vertex total drifted");
    require(vertices <= geometry_budgets.maximum_static_vertices, "scene exceeded vertex budget");
    require(
        result.scene.opaque_draw_call_count <= geometry_budgets.maximum_opaque_draw_calls,
        "scene exceeded draw-call budget");
    require(has_piece(result.scene, ScenePieceKind::chamber_shell), "scene has no chamber shell");
    require(has_piece(result.scene, ScenePieceKind::chamber_floor), "scene has no chamber floor");
    require(has_piece(result.scene, ScenePieceKind::tunnel), "scene has no tunnel");
    require(has_piece(result.scene, ScenePieceKind::junction), "scene has no junction");
    require(has_piece(result.scene, ScenePieceKind::bridge), "scene has no bridge mesh");
}

void bridge_and_collider_guarantees_hold(const std::filesystem::path&)
{
    for (const std::uint64_t seed : {7U, 11U, 123'456'789U}) {
        const CaveGenerationResult result{generate_cave({seed})};
        require(result.scene.bridge_routes.size() == 1U, "scene must guarantee one bridge route");
        const auto bridge = std::find_if(
            result.scene.routes.begin(), result.scene.routes.end(), [](const RouteGeometryContract& route) {
                return route.bridge;
            });
        require(bridge != result.scene.routes.end(), "bridge marker has no route contract");
        require(
            bridge->bridge_width_millimetres
                >= movement_envelope.minimum_clearance_width_millimetres
                    + movement_envelope.safety_margin_millimetres,
            "bridge is too narrow");
        require(has_collider(result.scene, ColliderKind::chamber_floor), "missing floor collider");
        require(has_collider(result.scene, ColliderKind::chamber_boundary), "missing boundary collider");
        require(has_collider(result.scene, ColliderKind::tunnel), "missing tunnel collider");
        require(has_collider(result.scene, ColliderKind::bridge_deck), "missing deck collider");
        require(has_collider(result.scene, ColliderKind::bridge_rail), "missing rail collider");
        require(has_collider(result.scene, ColliderKind::fall_region), "missing fall collider");
    }
}

void portal_and_route_endpoints_agree(const std::filesystem::path&)
{
    const CaveGenerationResult result{generate_cave({123'456'789U})};
    for (const RouteGeometryContract& route : result.scene.routes) {
        const auto first = std::find_if(
            result.scene.portals.begin(), result.scene.portals.end(), [&](const PortalContract& portal) {
                return portal.chamber_id == route.edge.first && portal.route == route.edge;
            });
        const auto second = std::find_if(
            result.scene.portals.begin(), result.scene.portals.end(), [&](const PortalContract& portal) {
                return portal.chamber_id == route.edge.second && portal.route == route.edge;
            });
        require(first != result.scene.portals.end(), "route has no first portal");
        require(second != result.scene.portals.end(), "route has no second portal");
        require(route.spline.control_points.front() == first->center_millimetres, "first seam drifted");
        require(route.spline.control_points.back() == second->center_millimetres, "second seam drifted");
    }
}

void scene_fingerprint_is_sensitive_to_integer_contract(const std::filesystem::path&)
{
    CaveGenerationResult result{generate_cave({123'456'789U})};
    const std::uint64_t fingerprint{result.scene.fingerprint};
    result.scene.chambers.front().base_radius_millimetres += 1;
    require(
        cave_scene_fingerprint(result.generation.effective_seed, result.scene) != fingerprint,
        "integer chamber change did not change scene fingerprint");
}

void geometry_rejections_reach_checked_fallback(const std::filesystem::path&)
{
    CaveGenerationTestSeams seams;
    seams.reject_attempt = [](const std::uint32_t, const CaveSceneData&) {
        return std::optional<std::string>{"forced Step 5B geometry rejection"};
    };
    const CaveGenerationResult result{generate_cave({123'456'789U}, seams)};
    require(result.generation.used_fallback, "eight geometry rejections did not use fallback");
    require(
        result.generation.diagnostics.size() == topology_limits.normal_attempt_count + 1U,
        "geometry retry diagnostic count changed");
    require(
        validate_cave_scene(result.generation.topology, result.scene).empty(),
        "fallback cave did not validate");
}

void invalid_geometry_fallback_fails_atomically(const std::filesystem::path&)
{
    CaveGenerationTestSeams seams;
    seams.reject_attempt = [](const std::uint32_t, const CaveSceneData&) {
        return std::optional<std::string>{"force fallback"};
    };
    seams.fallback_factory = [] {
        TopologyData fallback{known_good_fallback_topology()};
        fallback.nodes[1].anchor.x_millimetres = fallback.nodes[0].anchor.x_millimetres;
        fallback.nodes[1].anchor.z_millimetres = fallback.nodes[0].anchor.z_millimetres;
        return fallback;
    };
    require_throws<GenerationError>(
        [&] { static_cast<void>(generate_cave({99U}, seams)); },
        "fallback cave failed geometry validation",
        "invalid geometry fallback must fail atomically");
}

void start_camera_pose_is_finite(const std::filesystem::path&)
{
    const CaveGenerationResult result{generate_cave({123'456'789U})};
    const GeometryVector3& position{result.scene.start_camera_position_metres};
    const GeometryVector3& forward{result.scene.start_camera_forward};
    require(
        std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z),
        "start camera position is non-finite");
    const double length{std::sqrt(
        forward.x * forward.x + forward.y * forward.y + forward.z * forward.z)};
    require(std::isfinite(length) && std::abs(length - 1.0) < 1.0e-6, "start forward is not unit");
}

}  // namespace

std::vector<TestCase> cave_scene_test_cases()
{
    return {
        {"fixed seed builds complete cave scene", fixed_seed_builds_complete_scene},
        {"same seed repeats cave scene contract", same_seed_repeats_scene_contract},
        {"representative seeds vary cave scene", representative_seeds_vary},
        {"scene meshes are valid and budgeted", geometry_pieces_are_valid_and_budgeted},
        {"bridge and collider guarantees hold", bridge_and_collider_guarantees_hold},
        {"portal and route endpoints agree", portal_and_route_endpoints_agree},
        {"scene fingerprint is contract-sensitive", scene_fingerprint_is_sensitive_to_integer_contract},
        {"geometry rejections use checked fallback", geometry_rejections_reach_checked_fallback},
        {"invalid geometry fallback fails atomically", invalid_geometry_fallback_fails_atomically},
        {"start camera pose is finite", start_camera_pose_is_finite},
    };
}

}  // namespace crystalbound::test
