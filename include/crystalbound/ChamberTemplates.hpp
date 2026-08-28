#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "crystalbound/Generation.hpp"
#include "crystalbound/Geometry.hpp"

namespace crystalbound {

inline constexpr std::int64_t template_rotation_one_q30{1'073'741'824LL};
inline constexpr std::int64_t template_rotation_diagonal_q30{759'250'125LL};
inline constexpr std::int32_t template_socket_direction_scale{1'000'000};
inline constexpr std::int32_t template_socket_diagonal_component{707'107};
inline constexpr std::int64_t template_socket_tangent_limit{414'235LL};
inline constexpr std::int64_t authored_fire_socket_tangent_limit{1'000'000LL};
inline constexpr std::int64_t authored_water_socket_tangent_limit{1'000'000LL};
inline constexpr std::int64_t authored_earth_socket_tangent_limit{1'000'000LL};
inline constexpr std::int64_t authored_air_socket_tangent_limit{2'500'000LL};

enum class ChamberTemplateRole : std::uint8_t {
    start,
    fire,
    water,
    earth,
    air,
    aether,
    exit,
    neutral,
};

enum class TemplateSurfaceKind : std::uint8_t {
    stone,
    basalt,
    shallow_water,
    earth,
    wood,
    aether_stone,
};

enum class TemplateHazardKind : std::uint8_t {
    lava,
    aether_void,
};

enum class TemplateRespawnPolicy : std::uint8_t {
    last_safe_checkpoint,
};

enum class TemplateClearPurpose : std::uint8_t {
    socket_landing,
    checkpoint,
    crystal,
    exit_arch,
    required_corridor,
};

enum class TemplateAnchorKind : std::uint8_t {
    structural_landmark,
    crystal,
    pedestal,
    exit_arch,
    start_camera_target,
    cosmetic,
};

struct TemplatePoint2 {
    std::int32_t x_millimetres{};
    std::int32_t z_millimetres{};
};

constexpr bool operator==(
    const TemplatePoint2 left,
    const TemplatePoint2 right) noexcept
{
    return left.x_millimetres == right.x_millimetres
        && left.z_millimetres == right.z_millimetres;
}

struct TemplateFloorPatch {
    std::uint32_t stable_id{};
    std::vector<TemplatePoint2> clockwise_polygon{};
    std::int32_t support_height_millimetres{};
    TemplateSurfaceKind surface{TemplateSurfaceKind::stone};
    std::uint32_t support_priority{};
    bool walkable{};
};

struct TemplateBoundaryPatch {
    std::uint32_t stable_id{};
    std::vector<TemplatePoint2> clockwise_polygon{};
    std::int32_t minimum_y_millimetres{};
    std::int32_t maximum_y_millimetres{};
    bool capsule_blocking{};
    bool structural_line_of_sight{};
};

struct TemplateHazardVolume {
    std::uint32_t stable_id{};
    std::vector<TemplatePoint2> clockwise_polygon{};
    std::int32_t minimum_y_millimetres{};
    std::int32_t maximum_y_millimetres{};
    TemplateHazardKind kind{TemplateHazardKind::lava};
    std::uint32_t priority{};
    TemplateRespawnPolicy respawn{TemplateRespawnPolicy::last_safe_checkpoint};
};

struct TemplateClearZone {
    std::uint32_t stable_id{};
    std::vector<TemplatePoint2> clockwise_polygon{};
    std::int32_t minimum_y_millimetres{};
    std::int32_t maximum_y_millimetres{};
    TemplateClearPurpose purpose{TemplateClearPurpose::required_corridor};
};

struct TemplateSocket {
    std::uint8_t index{};
    TemplatePoint2 origin_millimetres{};
    TemplatePoint2 outward_direction_million{};
    std::int32_t arch_width_millimetres{};
    std::int32_t arch_height_millimetres{};
    TemplatePoint2 vestibule_inner_millimetres{};
    TemplatePoint2 vestibule_outer_millimetres{};
    std::uint32_t landing_patch_id{};
    std::uint32_t landing_clear_zone_id{};
};

struct TemplateNavigationEdge {
    std::uint32_t stable_id{};
    std::uint32_t from_patch_or_zone_id{};
    std::uint32_t to_patch_or_zone_id{};
    std::int32_t minimum_width_millimetres{};
    std::int32_t maximum_step_millimetres{};
    std::int32_t maximum_slope_millidegrees{};
    std::int32_t maximum_gap_millimetres{};
    bool bidirectional{};
};

struct TemplateAnchor {
    std::uint32_t stable_id{};
    TemplateAnchorKind kind{TemplateAnchorKind::cosmetic};
    IntegerPoint3 position_millimetres{};
    std::int32_t heading_millidegrees{};
};

struct ChamberTemplate {
    ChamberTemplateRole role{ChamberTemplateRole::neutral};
    std::int32_t outer_width_millimetres{};
    std::int32_t outer_depth_millimetres{};
    std::int32_t usable_diameter_millimetres{};
    std::int32_t usable_height_millimetres{};
    std::vector<TemplateFloorPatch> floor_patches{};
    std::vector<TemplateBoundaryPatch> boundary_patches{};
    std::vector<TemplateHazardVolume> hazards{};
    std::vector<TemplateClearZone> clear_zones{};
    std::vector<TemplateSocket> sockets{};
    std::vector<TemplateNavigationEdge> navigation_edges{};
    std::vector<TemplateAnchor> anchors{};
};

struct CrystalScaleInput {
    std::int32_t radius_millimetres{};
    std::int32_t height_millimetres{};
    std::vector<std::int32_t> radial_offsets_millimetres{};
    std::uint32_t base_scale_milli{1'000U};
    std::uint32_t minimum_animation_scale_milli{940U};
    std::uint32_t maximum_animation_scale_milli{1'080U};
};

struct CrystalScaleExtrema {
    std::int32_t minimum_diameter_millimetres{};
    std::int32_t maximum_diameter_millimetres{};
    std::int32_t minimum_height_millimetres{};
    std::int32_t maximum_height_millimetres{};
};

struct AssignedTemplateSocket {
    Edge edge{};
    std::uint8_t socket_index{};
};

struct ChamberSocketAssignment {
    NodeId chamber_id{};
    std::uint8_t orientation_octant{};
    std::vector<AssignedTemplateSocket> incident_edges{};
};

struct CompiledTemplateFloorPatch {
    std::uint64_t stable_object_id{};
    std::vector<TemplatePoint2> world_polygon_millimetres{};
    std::int32_t support_height_millimetres{};
    TemplateSurfaceKind surface{TemplateSurfaceKind::stone};
    std::uint32_t support_priority{};
    bool walkable{};
};

struct CompiledTemplateBoundaryPatch {
    std::uint64_t stable_object_id{};
    std::vector<TemplatePoint2> world_polygon_millimetres{};
    std::int32_t minimum_y_millimetres{};
    std::int32_t maximum_y_millimetres{};
    bool capsule_blocking{};
    bool structural_line_of_sight{};
};

struct CompiledTemplateHazardVolume {
    std::uint64_t stable_object_id{};
    std::vector<TemplatePoint2> world_polygon_millimetres{};
    std::int32_t minimum_y_millimetres{};
    std::int32_t maximum_y_millimetres{};
    TemplateHazardKind kind{TemplateHazardKind::lava};
    std::uint32_t priority{};
    TemplateRespawnPolicy respawn{TemplateRespawnPolicy::last_safe_checkpoint};
};

struct CompiledTemplateClearZone {
    std::uint64_t stable_object_id{};
    std::vector<TemplatePoint2> world_polygon_millimetres{};
    std::int32_t minimum_y_millimetres{};
    std::int32_t maximum_y_millimetres{};
    TemplateClearPurpose purpose{TemplateClearPurpose::required_corridor};
};

struct CompiledTemplateSocket {
    std::uint8_t local_index{};
    std::uint8_t world_index{};
    TemplatePoint2 world_origin_millimetres{};
    TemplatePoint2 world_outward_direction_million{};
    TemplatePoint2 world_vestibule_inner_millimetres{};
    TemplatePoint2 world_vestibule_outer_millimetres{};
    bool active{};
    std::optional<Edge> route{};
};

struct CompiledTemplateAnchor {
    std::uint64_t stable_object_id{};
    TemplateAnchorKind kind{TemplateAnchorKind::cosmetic};
    IntegerPoint3 world_position_millimetres{};
    std::int32_t heading_millidegrees{};
};

struct CompiledChamberTemplate {
    NodeId chamber_id{};
    ChamberTemplateRole role{ChamberTemplateRole::neutral};
    std::uint8_t orientation_octant{};
    std::int32_t usable_height_millimetres{};
    std::vector<CompiledTemplateFloorPatch> floor_patches{};
    std::vector<CompiledTemplateBoundaryPatch> boundary_patches{};
    std::vector<CompiledTemplateHazardVolume> hazards{};
    std::vector<CompiledTemplateClearZone> clear_zones{};
    std::vector<CompiledTemplateSocket> sockets{};
    std::vector<TemplateNavigationEdge> navigation_edges{};
    std::vector<CompiledTemplateAnchor> anchors{};
    std::uint64_t fingerprint{};
};

[[nodiscard]] ChamberTemplateRole chamber_template_role(const ChamberNode& node);
[[nodiscard]] const ChamberTemplate& chamber_template(ChamberTemplateRole role);
[[nodiscard]] const ChamberTemplate& chamber_template(const ChamberNode& node);
[[nodiscard]] const std::vector<ChamberTemplate>& chamber_templates();

[[nodiscard]] CrystalScaleExtrema crystal_scale_extrema(
    const CrystalScaleInput& input);
[[nodiscard]] std::vector<std::string> validate_chamber_template(
    const ChamberTemplate& chamber);
[[nodiscard]] std::vector<std::string> validate_all_chamber_templates();

[[nodiscard]] std::vector<ChamberSocketAssignment> assign_template_sockets(
    const TopologyData& topology);
[[nodiscard]] std::vector<std::string> validate_template_socket_assignments(
    const TopologyData& topology,
    const std::vector<ChamberSocketAssignment>& assignments);
[[nodiscard]] std::vector<CompiledChamberTemplate> compile_chamber_templates(
    const TopologyData& topology,
    const std::vector<ChamberSocketAssignment>& assignments);
[[nodiscard]] std::vector<std::string> validate_compiled_chamber_templates(
    const TopologyData& topology,
    const std::vector<CompiledChamberTemplate>& compiled);

[[nodiscard]] TemplatePoint2 rotate_template_point(
    TemplatePoint2 point,
    std::uint8_t octant);
[[nodiscard]] bool template_socket_accepts_vector(
    TemplatePoint2 socket_direction_million,
    TemplatePoint2 vector_millimetres) noexcept;

[[nodiscard]] std::uint64_t template_gameplay_fingerprint(
    const TopologyData& topology,
    const std::vector<ChamberSocketAssignment>& assignments);
[[nodiscard]] std::uint64_t chamber_template_structural_signature(
    const ChamberTemplate& chamber);

}  // namespace crystalbound
