#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "crystalbound/CaveScene.hpp"

namespace crystalbound {

enum class GroundContactKind : std::uint8_t {
    none,
    chamber,
    tunnel,
    bridge,
};

struct PlayerCapsule {
    double radius_metres{};
    double height_metres{};
};

struct GroundedMovementInput {
    double forward{};
    double right{};
    double view_yaw_degrees{};
    bool sprint{};
    bool jump{};
};

struct ChamberCollisionRegion {
    NodeId chamber_id{};
    std::uint64_t stable_object_id{};
    GeometryVector3 center_metres{};
    double floor_height_metres{};
    double ceiling_height_metres{};
    double usable_radius_metres{};
};

struct RouteCollisionRegion {
    Edge edge{};
    std::uint64_t stable_object_id{};
    GroundContactKind kind{GroundContactKind::tunnel};
    std::vector<SplineSample> samples{};
    double usable_half_width_metres{};
    double tunnel_radius_metres{};
    double rail_height_metres{};
};

struct FallCollisionRegion {
    std::uint64_t stable_object_id{};
    AxisAlignedBounds bounds{};
};

struct CollisionWorld {
    std::vector<ChamberCollisionRegion> chambers{};
    std::vector<RouteCollisionRegion> routes{};
    std::vector<FallCollisionRegion> fall_regions{};
    double kill_plane_metres{};
};

struct PlayerSpawn {
    GeometryVector3 feet_position_metres{};
    NodeId chamber_id{};
};

struct CollisionProbe {
    bool supported{};
    double floor_height_metres{};
    double ceiling_height_metres{};
    double slope_radians{};
    GroundContactKind contact_kind{GroundContactKind::none};
    std::optional<NodeId> chamber_id{};
    std::uint64_t stable_object_id{};
};

struct PlayerState {
    GeometryVector3 feet_position_metres{};
    double vertical_velocity_metres_per_second{};
    bool grounded{};
    GeometryVector3 safe_feet_position_metres{};
    NodeId safe_chamber_id{};
};

struct ControllerAdvanceResult {
    std::uint32_t fixed_ticks{};
    bool backlog_discarded{};
    bool collided{};
    bool jumped{};
    bool landed{};
    bool respawned{};
};

class ControllerError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

[[nodiscard]] PlayerCapsule locked_player_capsule() noexcept;
[[nodiscard]] CollisionWorld build_collision_world(const CaveSceneData& scene);
[[nodiscard]] PlayerSpawn find_start_spawn(const CaveGenerationResult& generation);
[[nodiscard]] std::vector<std::string> validate_collision_world(
    const CollisionWorld& world);
[[nodiscard]] CollisionProbe probe_collision_world(
    const CollisionWorld& world,
    const PlayerCapsule& capsule,
    const GeometryVector3& feet_position_metres,
    double maximum_step_up_metres);
[[nodiscard]] bool intersects_fall_region(
    const CollisionWorld& world,
    const GeometryVector3& feet_position_metres) noexcept;

class GroundedController final {
public:
    GroundedController(CollisionWorld world, PlayerSpawn spawn);

    [[nodiscard]] ControllerAdvanceResult advance(
        const GroundedMovementInput& input,
        double frame_delta_seconds);
    [[nodiscard]] const PlayerState& state() const noexcept;
    [[nodiscard]] GeometryVector3 camera_position_metres() const noexcept;
    [[nodiscard]] double fixed_step_seconds() const noexcept;

private:
    struct TickResult {
        bool collided{};
        bool jumped{};
        bool landed{};
        bool respawned{};
    };

    [[nodiscard]] TickResult simulate_tick(const GroundedMovementInput& input);
    [[nodiscard]] bool try_horizontal_move(
        GeometryVector3 candidate,
        double maximum_step_up_metres,
        bool allow_ground_snap,
        CollisionProbe& accepted_probe);
    void update_safe_chamber(const CollisionProbe& probe);
    void respawn() noexcept;

    CollisionWorld world_{};
    PlayerCapsule capsule_{locked_player_capsule()};
    PlayerState state_{};
    double accumulator_seconds_{};
    bool previous_jump_down_{};
    bool pending_jump_{};
};

}  // namespace crystalbound
