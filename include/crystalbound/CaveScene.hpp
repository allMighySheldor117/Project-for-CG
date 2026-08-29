#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "crystalbound/Generation.hpp"
#include "crystalbound/ChamberTemplates.hpp"
#include "crystalbound/Geometry.hpp"
#include "crystalbound/ElementalVisuals.hpp"
#include "crystalbound/Rendering.hpp"

namespace crystalbound {

inline constexpr std::int32_t route_junction_depth_millimetres{1'500};
inline constexpr std::int32_t authored_terminal_junction_depth_millimetres{150};
inline constexpr std::int32_t route_join_overlap_millimetres{100};
inline constexpr std::int32_t tunnel_clear_width_millimetres{3'200};
inline constexpr std::int32_t tunnel_side_height_millimetres{2'200};
inline constexpr std::int32_t tunnel_crown_height_millimetres{4'000};
inline constexpr std::int32_t tunnel_protected_corridor_millimetres{2'400};
inline constexpr std::uint32_t fixed_maze_room_count{5U};
inline constexpr std::uint32_t maze_grid_columns{7U};
inline constexpr std::uint32_t maze_grid_rows{9U};
inline constexpr std::int32_t maze_room_half_width_millimetres{12'000};
inline constexpr std::int32_t maze_room_core_half_length_millimetres{18'000};
inline constexpr std::int32_t maze_room_connector_half_length_millimetres{19'500};
inline constexpr std::int32_t maze_room_door_half_width_millimetres{1'600};
inline constexpr std::int32_t maze_minimum_tunnel_run_millimetres{6'000};
inline constexpr std::int32_t maze_wall_thickness_millimetres{300};
inline constexpr std::int32_t maze_wall_height_millimetres{4'000};
inline constexpr std::int32_t maze_tunnel_overlap_millimetres{100};
inline constexpr std::uint64_t second_bridge_decision_domain{
    0x4252494447450002ULL};
inline constexpr std::uint64_t second_bridge_score_domain{
    0x4252494447451002ULL};

constexpr std::uint64_t rotate_left_64(const std::uint64_t value) noexcept
{
    return (value << 1U) | (value >> 63U);
}

enum class ScenePieceKind : std::uint8_t {
    chamber_shell,
    chamber_floor,
    chamber_hazard,
    tunnel,
    junction,
    bridge,
    natural_formation,
    maze_wall,
};

enum class ColliderKind : std::uint8_t {
    chamber_floor,
    chamber_boundary,
    tunnel,
    bridge_deck,
    bridge_rail,
    fall_region,
};

enum class ChamberFloorMorphology : std::uint8_t {
    level,
    fractured_terraces,
    eroded_banks,
    grounded_shelves,
    open_spans,
    asymmetric_dais,
};

enum class ChamberShellSilhouette : std::uint8_t {
    balanced,
    jagged,
    flowing,
    massive,
    soaring,
    vaulted,
};

enum class ChamberEntranceFraming : std::uint8_t {
    natural,
    fractured,
    rounded,
    pillars,
    timbered,
    arched,
};

enum class ChamberLandmarkAnchor : std::uint8_t {
    center,
    lava_terrace,
    mist_basin,
    stone_columns,
    suspended_bridge,
    crystal_arch,
};

struct ChamberStructuralIdentity {
    ChamberFloorMorphology floor{ChamberFloorMorphology::level};
    ChamberShellSilhouette shell{ChamberShellSilhouette::balanced};
    ChamberEntranceFraming entrance{ChamberEntranceFraming::natural};
    ChamberLandmarkAnchor landmark{ChamberLandmarkAnchor::center};
    std::uint8_t vertical_profile{};
};

struct ChamberRingContract {
    std::int32_t height_millimetres{};
    std::vector<std::int32_t> radii_millimetres{};
};

struct ChamberGeometryContract {
    NodeId node_id{};
    IntegerPoint3 center_millimetres{};
    std::int32_t base_radius_millimetres{};
    std::int32_t wall_height_millimetres{};
    std::uint32_t side_count{};
    std::vector<std::int32_t> radial_offsets_millimetres{};
    std::int32_t minimum_safe_ring_radius_millimetres{};
    ChamberStructuralIdentity identity{};
    std::vector<ChamberRingContract> rings{};
};

struct ChamberStructuralTriangle {
    std::uint64_t stable_object_id{};
    GeometryVector3 first{};
    GeometryVector3 second{};
    GeometryVector3 third{};
};

struct PortalContract {
    NodeId chamber_id{};
    Edge route{};
    IntegerPoint3 center_millimetres{};
    IntegerPoint3 inward_direction_millimetres{};
    std::uint32_t opening_side_index{};
    std::int32_t approach_depth_millimetres{route_junction_depth_millimetres};
};

struct RouteGeometryContract {
    Edge edge{};
    SplineRouteInput spline{};
    std::vector<std::int32_t> ring_offsets_millimetres{};
    bool bridge{};
    std::int32_t tunnel_clear_width_millimetres{};
    std::int32_t tunnel_side_height_millimetres{};
    std::int32_t tunnel_crown_height_millimetres{};
    std::int32_t vestibule_length_millimetres{};
    std::int32_t join_overlap_millimetres{};
    std::int32_t bridge_width_millimetres{};
    std::int32_t bridge_rail_height_millimetres{};
};

enum class MazeWallDirection : std::uint8_t {
    local_x,
    local_z,
};

struct MazeCellCoordinate {
    std::uint32_t column{};
    std::uint32_t row{};
};

constexpr bool operator==(
    const MazeCellCoordinate left,
    const MazeCellCoordinate right) noexcept
{
    return left.column == right.column && left.row == right.row;
}

struct MazeWallContract {
    std::uint64_t stable_object_id{};
    MazeWallDirection direction{MazeWallDirection::local_x};
    std::int32_t center_x_millimetres{};
    std::int32_t center_z_millimetres{};
    std::int32_t length_millimetres{};
};

struct MazeRoomContract {
    Edge route{};
    std::uint32_t ordinal{};
    IntegerPoint3 floor_center_millimetres{};
    std::int32_t forward_x_milli{};
    std::int32_t forward_z_milli{};
    std::uint32_t columns{};
    std::uint32_t rows{};
    std::vector<MazeWallContract> walls{};
    std::vector<MazeCellCoordinate> solution_cells{};
    std::uint64_t fingerprint{};
};

struct SceneMeshPiece {
    ScenePieceKind kind{ScenePieceKind::chamber_shell};
    std::uint64_t stable_object_id{};
    MeshData mesh{};
    AxisAlignedBounds bounds{};
    MaterialKind material{MaterialKind::rock};
    std::array<float, 3> albedo{};
    std::optional<NodeId> owner_chamber_id{};
};

struct SceneCollider {
    ColliderKind kind{ColliderKind::chamber_floor};
    std::uint64_t stable_object_id{};
    AxisAlignedBounds bounds{};
};

struct CaveSceneData {
    std::vector<ChamberSocketAssignment> template_socket_assignments{};
    std::vector<CompiledChamberTemplate> compiled_chambers{};
    std::uint64_t template_gameplay_fingerprint{};
    std::vector<ChamberGeometryContract> chambers{};
    std::vector<PortalContract> portals{};
    std::vector<RouteGeometryContract> routes{};
    std::vector<MazeRoomContract> maze_rooms{};
    std::vector<SceneMeshPiece> mesh_pieces{};
    std::vector<SceneCollider> colliders{};
    std::vector<Edge> bridge_routes{};
    GeometryVector3 start_camera_position_metres{};
    GeometryVector3 start_camera_forward{};
    std::uint32_t static_vertex_count{};
    std::uint32_t opaque_draw_call_count{};
    std::uint64_t fingerprint{};
    ElementalSceneData elemental_visuals{};
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
[[nodiscard]] GeometryVector3 chamber_ring_position(
    const ChamberGeometryContract& chamber,
    std::size_t ring_index,
    std::uint32_t side);
[[nodiscard]] bool chamber_portal_opens_side(
    const std::vector<PortalContract>& portals,
    NodeId chamber_id,
    std::uint32_t side,
    std::uint32_t side_count) noexcept;
[[nodiscard]] std::vector<ChamberStructuralTriangle> chamber_structure_triangles(
    const ChamberGeometryContract& chamber,
    const std::vector<PortalContract>& portals);
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
