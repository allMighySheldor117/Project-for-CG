#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "crystalbound/Generation.hpp"
#include "crystalbound/Geometry.hpp"
#include "crystalbound/Rendering.hpp"

namespace crystalbound {

enum class ScenePieceKind : std::uint8_t {
    chamber_shell,
    chamber_floor,
    tunnel,
    junction,
    bridge,
};

enum class ColliderKind : std::uint8_t {
    chamber_floor,
    chamber_boundary,
    tunnel,
    bridge_deck,
    bridge_rail,
    fall_region,
};

struct ChamberGeometryContract {
    NodeId node_id{};
    IntegerPoint3 center_millimetres{};
    std::int32_t base_radius_millimetres{};
    std::int32_t wall_height_millimetres{};
    std::uint32_t side_count{};
    std::vector<std::int32_t> radial_offsets_millimetres{};
};

struct PortalContract {
    NodeId chamber_id{};
    Edge route{};
    IntegerPoint3 center_millimetres{};
    IntegerPoint3 inward_direction_millimetres{};
    std::uint32_t opening_side_index{};
};

struct RouteGeometryContract {
    Edge edge{};
    SplineRouteInput spline{};
    std::vector<std::int32_t> ring_offsets_millimetres{};
    bool bridge{};
    std::int32_t bridge_width_millimetres{};
    std::int32_t bridge_rail_height_millimetres{};
};

struct SceneMeshPiece {
    ScenePieceKind kind{ScenePieceKind::chamber_shell};
    std::uint64_t stable_object_id{};
    MeshData mesh{};
    AxisAlignedBounds bounds{};
    MaterialKind material{MaterialKind::rock};
    std::array<float, 3> albedo{};
};

struct SceneCollider {
    ColliderKind kind{ColliderKind::chamber_floor};
    std::uint64_t stable_object_id{};
    AxisAlignedBounds bounds{};
};

struct CaveSceneData {
    std::vector<ChamberGeometryContract> chambers{};
    std::vector<PortalContract> portals{};
    std::vector<RouteGeometryContract> routes{};
    std::vector<SceneMeshPiece> mesh_pieces{};
    std::vector<SceneCollider> colliders{};
    std::vector<Edge> bridge_routes{};
    GeometryVector3 start_camera_position_metres{};
    GeometryVector3 start_camera_forward{};
    std::uint32_t static_vertex_count{};
    std::uint32_t opaque_draw_call_count{};
    std::uint64_t fingerprint{};
};

struct CaveGenerationResult {
    GenerationResult generation{};
    CaveSceneData scene{};
    MechanicalReachabilityReport reachability{};
};

using CaveSceneAttemptRejection = std::function<std::optional<std::string>(
    std::uint32_t attempt_index,
    const CaveSceneData& scene)>;

using MechanicalAttemptRejection = std::function<std::optional<ReachabilityIssue>(
    std::uint32_t attempt_index,
    bool fallback,
    const MechanicalReachabilityReport& report)>;

struct CaveGenerationTestSeams {
    CaveSceneAttemptRejection reject_attempt{};
    MechanicalAttemptRejection reject_mechanical{};
    FallbackFactory fallback_factory{};
};

[[nodiscard]] CaveSceneData build_cave_scene(
    const TopologyData& topology,
    Seed effective_seed);
[[nodiscard]] std::vector<std::string> validate_cave_scene(
    const TopologyData& topology,
    const CaveSceneData& scene);
[[nodiscard]] std::uint64_t cave_scene_fingerprint(
    Seed effective_seed,
    const CaveSceneData& scene);
[[nodiscard]] CaveGenerationResult generate_cave(
    Seed requested_seed,
    const CaveGenerationTestSeams& seams = {});

}  // namespace crystalbound
