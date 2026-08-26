#include "crystalbound/PlayerController.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace crystalbound {
namespace {

constexpr double millimetres_per_metre{1'000.0};
constexpr double pi{3.14159265358979323846};
constexpr double ground_tolerance_metres{0.05};
constexpr double maximum_horizontal_substep_metres{0.10};
constexpr double comparison_tolerance{1.0e-9};

[[nodiscard]] std::int32_t maximum_sample_slope_millidegrees(
    const std::vector<SplineSample>& samples)
{
    double maximum_radians{};
    for (std::size_t index{}; index + 1U < samples.size(); ++index) {
        const GeometryVector3& first{samples[index].position_metres};
        const GeometryVector3& second{samples[index + 1U].position_metres};
        const double horizontal{std::hypot(second.x - first.x, second.z - first.z)};
        const double radians{std::atan2(std::abs(second.y - first.y), horizontal)};
        if (!std::isfinite(radians)) {
            throw ControllerError{"Collision route has a non-finite slope."};
        }
        maximum_radians = std::max(maximum_radians, radians);
    }
    const double millidegrees{maximum_radians * 180'000.0 / pi};
    if (millidegrees > static_cast<double>(std::numeric_limits<std::int32_t>::max())) {
        throw ControllerError{"Collision route slope cannot be represented."};
    }
    return static_cast<std::int32_t>(std::lround(millidegrees));
}

[[nodiscard]] const ChamberGeometryContract& scene_chamber(
    const CaveSceneData& scene,
    const NodeId chamber_id)
{
    const auto found = std::find_if(
        scene.chambers.begin(), scene.chambers.end(),
        [chamber_id](const ChamberGeometryContract& chamber) {
            return chamber.node_id == chamber_id;
        });
    if (found == scene.chambers.end()) {
        throw ControllerError{"Collision route references a missing chamber contract."};
    }
    return *found;
}

[[nodiscard]] bool has_route_portal(
    const CaveSceneData& scene,
    const NodeId chamber_id,
    const Edge edge,
    const IntegerPoint3& endpoint)
{
    return std::any_of(
        scene.portals.begin(), scene.portals.end(),
        [&](const PortalContract& portal) {
            return portal.chamber_id == chamber_id && portal.route == edge
                && portal.center_millimetres == endpoint;
        });
}

[[nodiscard]] RouteCollisionRegion::DirectedMeasurements directed_measurements(
    const CaveSceneData& scene,
    const RouteGeometryContract& route,
    const RouteDirection direction,
    const std::int32_t landing_width_millimetres)
{
    const bool forward{direction == RouteDirection::first_to_second};
    const NodeId from_id{forward ? route.edge.first : route.edge.second};
    const NodeId to_id{forward ? route.edge.second : route.edge.first};
    const IntegerPoint3& from_endpoint{
        forward ? route.spline.control_points.front() : route.spline.control_points.back()};
    const IntegerPoint3& to_endpoint{
        forward ? route.spline.control_points.back() : route.spline.control_points.front()};
    const ChamberGeometryContract& from{scene_chamber(scene, from_id)};
    const ChamberGeometryContract& to{scene_chamber(scene, to_id)};
    const std::int32_t from_route_floor{
        from_endpoint.y_millimetres - route.spline.radius_millimetres};
    const std::int32_t to_route_floor{
        to_endpoint.y_millimetres - route.spline.radius_millimetres};
    const std::int32_t entry_step{
        std::max(0, from_route_floor - from.center_millimetres.y_millimetres)};
    const std::int32_t exit_step{
        std::max(0, to.center_millimetres.y_millimetres - to_route_floor)};
    const bool from_portal{has_route_portal(scene, from_id, route.edge, from_endpoint)};
    const bool to_portal{has_route_portal(scene, to_id, route.edge, to_endpoint)};
    return {
        std::max(entry_step, exit_step),
        0,
        landing_width_millimetres,
        from_portal,
        from_portal,
        to_portal,
    };
}

[[nodiscard]] bool finite_vector(const GeometryVector3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] bool valid_bounds(const AxisAlignedBounds& bounds) noexcept
{
    return finite_vector(bounds.minimum_metres) && finite_vector(bounds.maximum_metres)
        && bounds.minimum_metres.x <= bounds.maximum_metres.x
        && bounds.minimum_metres.y <= bounds.maximum_metres.y
        && bounds.minimum_metres.z <= bounds.maximum_metres.z;
}

[[nodiscard]] double squared_horizontal_distance(
    const GeometryVector3& left,
    const GeometryVector3& right) noexcept
{
    const double x{left.x - right.x};
    const double z{left.z - right.z};
    return x * x + z * z;
}

[[nodiscard]] double minimum_chamber_radius_metres(
    const ChamberGeometryContract& chamber)
{
    const auto minimum_offset = std::min_element(
        chamber.radial_offsets_millimetres.begin(),
        chamber.radial_offsets_millimetres.end());
    if (minimum_offset == chamber.radial_offsets_millimetres.end()) {
        throw ControllerError{"Chamber collision requires radial offsets."};
    }
    return static_cast<double>(chamber.base_radius_millimetres + *minimum_offset)
        / millimetres_per_metre;
}

[[nodiscard]] bool point_in_bounds(
    const GeometryVector3& point,
    const AxisAlignedBounds& bounds) noexcept
{
    return point.x >= bounds.minimum_metres.x && point.x <= bounds.maximum_metres.x
        && point.y >= bounds.minimum_metres.y && point.y <= bounds.maximum_metres.y
        && point.z >= bounds.minimum_metres.z && point.z <= bounds.maximum_metres.z;
}

struct ClosestRoutePoint {
    GeometryVector3 position{};
    double horizontal_distance_squared{std::numeric_limits<double>::infinity()};
    double slope_radians{};
};

[[nodiscard]] ClosestRoutePoint closest_route_point(
    const RouteCollisionRegion& route,
    const GeometryVector3& point)
{
    ClosestRoutePoint closest;
    for (std::size_t index{}; index + 1U < route.samples.size(); ++index) {
        const GeometryVector3& first{route.samples[index].position_metres};
        const GeometryVector3& second{route.samples[index + 1U].position_metres};
        const double x{second.x - first.x};
        const double z{second.z - first.z};
        const double horizontal_squared{x * x + z * z};
        if (!std::isfinite(horizontal_squared)
            || horizontal_squared <= comparison_tolerance) {
            continue;
        }
        const double parameter{std::clamp(
            ((point.x - first.x) * x + (point.z - first.z) * z)
                / horizontal_squared,
            0.0,
            1.0)};
        const GeometryVector3 candidate{
            first.x + x * parameter,
            first.y + (second.y - first.y) * parameter,
            first.z + z * parameter,
        };
        const double distance_squared{squared_horizontal_distance(point, candidate)};
        if (distance_squared + comparison_tolerance < closest.horizontal_distance_squared) {
            closest.position = candidate;
            closest.horizontal_distance_squared = distance_squared;
            closest.slope_radians = std::atan2(
                std::abs(second.y - first.y), std::sqrt(horizontal_squared));
        }
    }
    return closest;
}

[[nodiscard]] bool better_probe(
    const CollisionProbe& candidate,
    const CollisionProbe& current,
    const double feet_height) noexcept
{
    if (!current.supported) {
        return true;
    }
    const bool candidate_reachable{
        candidate.floor_height_metres <= feet_height + ground_tolerance_metres};
    const bool current_reachable{
        current.floor_height_metres <= feet_height + ground_tolerance_metres};
    if (candidate_reachable != current_reachable) {
        return candidate_reachable;
    }
    if (std::abs(candidate.floor_height_metres - current.floor_height_metres)
        > comparison_tolerance) {
        return candidate.floor_height_metres > current.floor_height_metres;
    }
    return candidate.stable_object_id < current.stable_object_id;
}

[[nodiscard]] CollisionProbe probe_chambers(
    const CollisionWorld& world,
    const PlayerCapsule& capsule,
    const GeometryVector3& feet_position,
    const double maximum_step_up)
{
    CollisionProbe best;
    for (const ChamberCollisionRegion& chamber : world.chambers) {
        if (squared_horizontal_distance(feet_position, chamber.center_metres)
            > chamber.usable_radius_metres * chamber.usable_radius_metres) {
            continue;
        }
        CollisionProbe candidate{
            true,
            chamber.floor_height_metres,
            chamber.ceiling_height_metres,
            0.0,
            GroundContactKind::chamber,
            chamber.chamber_id,
            chamber.stable_object_id,
        };
        if (candidate.floor_height_metres > feet_position.y + maximum_step_up
            || candidate.ceiling_height_metres - candidate.floor_height_metres
                + comparison_tolerance < capsule.height_metres) {
            continue;
        }
        if (better_probe(candidate, best, feet_position.y)) {
            best = candidate;
        }
    }
    return best;
}

[[nodiscard]] CollisionProbe probe_routes(
    const CollisionWorld& world,
    const PlayerCapsule& capsule,
    const GeometryVector3& feet_position,
    const double maximum_step_up)
{
    CollisionProbe best;
    const double maximum_slope{
        static_cast<double>(movement_envelope.maximum_slope_millidegrees)
        * pi / 180'000.0};
    for (const RouteCollisionRegion& route : world.routes) {
        const ClosestRoutePoint closest{closest_route_point(route, feet_position)};
        if (!std::isfinite(closest.horizontal_distance_squared)
            || closest.horizontal_distance_squared
                > route.usable_half_width_metres * route.usable_half_width_metres
            || closest.slope_radians > maximum_slope + comparison_tolerance) {
            continue;
        }
        const double floor_height{
            closest.position.y - route.tunnel_radius_metres};
        const double ceiling_height{
            route.kind == GroundContactKind::bridge
                ? floor_height + static_cast<double>(
                      movement_envelope.minimum_clearance_height_millimetres
                          + movement_envelope.safety_margin_millimetres)
                    / millimetres_per_metre
                : closest.position.y + route.tunnel_radius_metres};
        if (floor_height > feet_position.y + maximum_step_up
            || ceiling_height - floor_height + comparison_tolerance
                < capsule.height_metres) {
            continue;
        }
        CollisionProbe candidate{
            true,
            floor_height,
            ceiling_height,
            closest.slope_radians,
            route.kind,
            std::nullopt,
            route.stable_object_id,
        };
        if (better_probe(candidate, best, feet_position.y)) {
            best = candidate;
        }
    }
    return best;
}

void validate_input(const GroundedMovementInput& input, const double frame_delta_seconds)
{
    if (!std::isfinite(input.forward) || !std::isfinite(input.right)
        || !std::isfinite(input.view_yaw_degrees)) {
        throw ControllerError{"Grounded movement input must be finite."};
    }
    if (!std::isfinite(frame_delta_seconds) || frame_delta_seconds < 0.0) {
        throw ControllerError{"Controller frame delta must be finite and non-negative."};
    }
}

}  // namespace

PlayerCapsule locked_player_capsule() noexcept
{
    return {
        static_cast<double>(movement_envelope.capsule_radius_millimetres)
            / millimetres_per_metre,
        static_cast<double>(movement_envelope.total_height_millimetres)
            / millimetres_per_metre,
    };
}

CollisionWorld build_collision_world(const CaveSceneData& scene)
{
    const PlayerCapsule capsule{locked_player_capsule()};
    const double safety_margin{
        static_cast<double>(movement_envelope.safety_margin_millimetres)
        / millimetres_per_metre};
    CollisionWorld world;
    world.chambers.reserve(scene.chambers.size());
    double minimum_floor{std::numeric_limits<double>::infinity()};
    for (const ChamberGeometryContract& chamber : scene.chambers) {
        const GeometryVector3 center{
            static_cast<double>(chamber.center_millimetres.x_millimetres)
                / millimetres_per_metre,
            static_cast<double>(chamber.center_millimetres.y_millimetres)
                / millimetres_per_metre,
            static_cast<double>(chamber.center_millimetres.z_millimetres)
                / millimetres_per_metre,
        };
        const double usable_radius{
            minimum_chamber_radius_metres(chamber) - capsule.radius_metres
            - safety_margin};
        world.chambers.push_back({
            chamber.node_id,
            static_cast<std::uint64_t>(chamber.node_id.value),
            center,
            center.y,
            center.y + static_cast<double>(chamber.wall_height_millimetres)
                    / millimetres_per_metre,
            usable_radius,
        });
        minimum_floor = std::min(minimum_floor, center.y);
    }

    world.routes.reserve(scene.routes.size());
    for (const RouteGeometryContract& route : scene.routes) {
        const double route_radius{
            static_cast<double>(route.spline.radius_millimetres)
            / millimetres_per_metre};
        const double half_width{route.bridge
                ? static_cast<double>(route.bridge_width_millimetres)
                        / millimetres_per_metre / 2.0
                    - capsule.radius_metres - safety_margin
                : route_radius - capsule.radius_metres - safety_margin};
        std::vector<SplineSample> samples{
            sample_centripetal_catmull_rom(route.spline)};
        const std::int32_t clearance_width{route.bridge
                ? route.bridge_width_millimetres
                : route.spline.radius_millimetres * 2};
        const std::int32_t clearance_height{route.bridge
                ? movement_envelope.minimum_clearance_height_millimetres
                    + movement_envelope.safety_margin_millimetres
                : route.spline.radius_millimetres * 2};
        world.routes.push_back({
            route.edge,
            stable_edge_id(route.edge),
            route.bridge ? GroundContactKind::bridge : GroundContactKind::tunnel,
            std::move(samples),
            half_width,
            route_radius,
            static_cast<double>(route.bridge_rail_height_millimetres)
                / millimetres_per_metre,
            clearance_width,
            clearance_height,
            0,
            {
                directed_measurements(
                    scene, route, RouteDirection::first_to_second, clearance_width),
                directed_measurements(
                    scene, route, RouteDirection::second_to_first, clearance_width),
            },
        });
        world.routes.back().maximum_slope_millidegrees =
            maximum_sample_slope_millidegrees(world.routes.back().samples);
    }

    for (const SceneCollider& collider : scene.colliders) {
        if (collider.kind == ColliderKind::fall_region) {
            world.fall_regions.push_back({collider.stable_object_id, collider.bounds});
            minimum_floor = std::min(minimum_floor, collider.bounds.minimum_metres.y);
        }
    }
    world.kill_plane_metres = minimum_floor - 1.0;

    const auto chamber_order = [](const ChamberCollisionRegion& left,
                                   const ChamberCollisionRegion& right) {
        return left.stable_object_id < right.stable_object_id;
    };
    const auto route_order = [](const RouteCollisionRegion& left,
                                 const RouteCollisionRegion& right) {
        return left.stable_object_id < right.stable_object_id;
    };
    const auto fall_order = [](const FallCollisionRegion& left,
                                const FallCollisionRegion& right) {
        return left.stable_object_id < right.stable_object_id;
    };
    std::sort(world.chambers.begin(), world.chambers.end(), chamber_order);
    std::sort(world.routes.begin(), world.routes.end(), route_order);
    std::sort(world.fall_regions.begin(), world.fall_regions.end(), fall_order);

    const std::vector<std::string> errors{validate_collision_world(world)};
    if (!errors.empty()) {
        throw ControllerError{errors.front()};
    }
    return world;
}

PlayerSpawn find_start_spawn(const CaveGenerationResult& generation)
{
    const auto start = std::find_if(
        generation.generation.topology.nodes.begin(),
        generation.generation.topology.nodes.end(),
        [](const ChamberNode& node) { return node.role == ChamberRole::start; });
    if (start == generation.generation.topology.nodes.end()) {
        throw ControllerError{"Grounded controller requires a Start chamber."};
    }
    return {
        {
            static_cast<double>(start->anchor.x_millimetres) / millimetres_per_metre,
            static_cast<double>(start->anchor.elevation_millimetres)
                / millimetres_per_metre,
            static_cast<double>(start->anchor.z_millimetres) / millimetres_per_metre,
        },
        start->id,
    };
}

std::vector<std::string> validate_collision_world(const CollisionWorld& world)
{
    std::vector<std::string> errors;
    const PlayerCapsule capsule{locked_player_capsule()};
    if (world.chambers.empty()) {
        errors.push_back("Collision world requires at least one chamber region.");
    }
    if (world.routes.empty()) {
        errors.push_back("Collision world requires at least one route region.");
    }
    if (world.fall_regions.empty()) {
        errors.push_back("Collision world requires at least one fall region.");
    }
    if (!std::isfinite(world.kill_plane_metres)) {
        errors.push_back("Collision world kill plane must be finite.");
    }
    bool bridge{};
    for (const ChamberCollisionRegion& chamber : world.chambers) {
        if (!finite_vector(chamber.center_metres)
            || !std::isfinite(chamber.floor_height_metres)
            || !std::isfinite(chamber.ceiling_height_metres)
            || !std::isfinite(chamber.usable_radius_metres)
            || chamber.usable_radius_metres <= 0.0
            || chamber.ceiling_height_metres - chamber.floor_height_metres
                + comparison_tolerance < capsule.height_metres) {
            errors.push_back("Collision world contains an invalid chamber region.");
        }
    }
    for (const RouteCollisionRegion& route : world.routes) {
        bridge = bridge || route.kind == GroundContactKind::bridge;
        if (route.kind != GroundContactKind::tunnel
            && route.kind != GroundContactKind::bridge) {
            errors.push_back("Collision route has an unsupported contact kind.");
        }
        if (route.samples.size() < 2U || !std::isfinite(route.usable_half_width_metres)
            || route.usable_half_width_metres <= 0.0
            || !std::isfinite(route.tunnel_radius_metres)
            || route.tunnel_radius_metres <= 0.0
            || !std::isfinite(route.rail_height_metres)
            || route.rail_height_metres < 0.0) {
            errors.push_back("Collision world contains an invalid route region.");
            continue;
        }
        if (route.kind == GroundContactKind::tunnel
            && route.tunnel_radius_metres * 2.0 + comparison_tolerance
                < capsule.height_metres) {
            errors.push_back("Collision tunnel has insufficient vertical clearance.");
        }
        for (const SplineSample& sample : route.samples) {
            if (!finite_vector(sample.position_metres) || !finite_vector(sample.tangent)
                || !std::isfinite(sample.distance_metres)) {
                errors.push_back("Collision route contains a non-finite sample.");
                break;
            }
        }
    }
    if (!bridge) {
        errors.push_back("Collision world requires a bridge route.");
    }
    for (const FallCollisionRegion& fall : world.fall_regions) {
        if (!valid_bounds(fall.bounds)) {
            errors.push_back("Collision world contains invalid fall bounds.");
        }
    }
    for (const ChamberCollisionRegion& chamber : world.chambers) {
        const GeometryVector3 respawn{
            chamber.center_metres.x,
            chamber.floor_height_metres,
            chamber.center_metres.z,
        };
        if (intersects_fall_region(world, respawn)) {
            errors.push_back("Collision world contains an unsafe chamber respawn location.");
        }
    }
    return errors;
}

CollisionProbe probe_collision_world(
    const CollisionWorld& world,
    const PlayerCapsule& capsule,
    const GeometryVector3& feet_position_metres,
    const double maximum_step_up_metres)
{
    if (!finite_vector(feet_position_metres) || !std::isfinite(capsule.radius_metres)
        || !std::isfinite(capsule.height_metres) || capsule.radius_metres <= 0.0
        || capsule.height_metres <= capsule.radius_metres * 2.0
        || !std::isfinite(maximum_step_up_metres) || maximum_step_up_metres < 0.0) {
        throw ControllerError{"Collision probe received an invalid capsule or position."};
    }
    CollisionProbe chamber{
        probe_chambers(world, capsule, feet_position_metres, maximum_step_up_metres)};
    const CollisionProbe route{
        probe_routes(world, capsule, feet_position_metres, maximum_step_up_metres)};
    if (route.supported && better_probe(route, chamber, feet_position_metres.y)) {
        return route;
    }
    return chamber;
}

bool intersects_fall_region(
    const CollisionWorld& world,
    const GeometryVector3& feet_position_metres) noexcept
{
    if (!finite_vector(feet_position_metres)
        || feet_position_metres.y < world.kill_plane_metres) {
        return true;
    }
    return std::any_of(
        world.fall_regions.begin(),
        world.fall_regions.end(),
        [&](const FallCollisionRegion& fall) {
            return point_in_bounds(feet_position_metres, fall.bounds);
        });
}

GroundedController::GroundedController(CollisionWorld world, const PlayerSpawn spawn)
    : world_(std::move(world))
{
    const std::vector<std::string> errors{validate_collision_world(world_)};
    if (!errors.empty()) {
        throw ControllerError{errors.front()};
    }
    if (!finite_vector(spawn.feet_position_metres)) {
        throw ControllerError{"Player spawn position must be finite."};
    }
    const CollisionProbe support{probe_collision_world(
        world_, capsule_, spawn.feet_position_metres, ground_tolerance_metres)};
    if (!support.supported || intersects_fall_region(world_, spawn.feet_position_metres)
        || !support.chamber_id.has_value()
        || *support.chamber_id != spawn.chamber_id
        || std::abs(spawn.feet_position_metres.y - support.floor_height_metres)
            > ground_tolerance_metres) {
        throw ControllerError{"Player spawn must be grounded inside its safe chamber."};
    }
    state_ = {
        {spawn.feet_position_metres.x, support.floor_height_metres,
         spawn.feet_position_metres.z},
        0.0,
        true,
        {spawn.feet_position_metres.x, support.floor_height_metres,
         spawn.feet_position_metres.z},
        spawn.chamber_id,
    };
}

ControllerAdvanceResult GroundedController::advance(
    const GroundedMovementInput& input,
    const double frame_delta_seconds)
{
    validate_input(input, frame_delta_seconds);
    if (input.jump && !previous_jump_down_) {
        pending_jump_ = true;
    }
    previous_jump_down_ = input.jump;

    const double maximum_delta{
        static_cast<double>(movement_envelope.frame_delta_clamp_milliseconds) / 1'000.0};
    accumulator_seconds_ += std::min(frame_delta_seconds, maximum_delta);
    const double step{fixed_step_seconds()};
    ControllerAdvanceResult result;
    while (accumulator_seconds_ + comparison_tolerance >= step
        && result.fixed_ticks < movement_envelope.maximum_catch_up_ticks) {
        const TickResult tick{simulate_tick(input)};
        result.collided = result.collided || tick.collided;
        result.jumped = result.jumped || tick.jumped;
        result.landed = result.landed || tick.landed;
        result.respawned = result.respawned || tick.respawned;
        accumulator_seconds_ = std::max(0.0, accumulator_seconds_ - step);
        ++result.fixed_ticks;
        if (tick.respawned) {
            accumulator_seconds_ = 0.0;
            break;
        }
    }
    if (accumulator_seconds_ + comparison_tolerance >= step) {
        accumulator_seconds_ = std::fmod(accumulator_seconds_, step);
        result.backlog_discarded = true;
    }
    return result;
}

const PlayerState& GroundedController::state() const noexcept
{
    return state_;
}

GeometryVector3 GroundedController::camera_position_metres() const noexcept
{
    return {
        state_.feet_position_metres.x,
        state_.feet_position_metres.y
            + static_cast<double>(movement_envelope.camera_height_millimetres)
                / millimetres_per_metre,
        state_.feet_position_metres.z,
    };
}

double GroundedController::fixed_step_seconds() const noexcept
{
    return 1.0 / static_cast<double>(movement_envelope.fixed_simulation_hertz);
}

GroundedController::TickResult GroundedController::simulate_tick(
    const GroundedMovementInput& input)
{
    TickResult result;
    const double step{fixed_step_seconds()};
    const double input_length{std::hypot(input.forward, input.right)};
    const double normalized_forward{input_length > 1.0 ? input.forward / input_length
                                                       : input.forward};
    const double normalized_right{input_length > 1.0 ? input.right / input_length
                                                     : input.right};
    const double yaw{input.view_yaw_degrees * pi / 180.0};
    const double forward_x{std::cos(yaw)};
    const double forward_z{std::sin(yaw)};
    const double right_x{-forward_z};
    const double right_z{forward_x};
    const double speed{static_cast<double>(input.sprint
            ? movement_envelope.sprint_speed_millimetres_per_second
            : movement_envelope.walk_speed_millimetres_per_second)
        / millimetres_per_metre};
    const double movement_x{
        (forward_x * normalized_forward + right_x * normalized_right) * speed * step};
    const double movement_z{
        (forward_z * normalized_forward + right_z * normalized_right) * speed * step};
    const double movement_length{std::hypot(movement_x, movement_z)};
    const std::uint32_t substeps{std::max<std::uint32_t>(
        1U,
        static_cast<std::uint32_t>(
            std::ceil(movement_length / maximum_horizontal_substep_metres)))};
    const double substep_x{movement_x / static_cast<double>(substeps)};
    const double substep_z{movement_z / static_cast<double>(substeps)};
    const double maximum_step{
        static_cast<double>(movement_envelope.step_height_millimetres)
        / millimetres_per_metre};

    for (std::uint32_t index{}; index < substeps; ++index) {
        CollisionProbe accepted;
        GeometryVector3 candidate{state_.feet_position_metres};
        candidate.x += substep_x;
        if (!try_horizontal_move(candidate, maximum_step, state_.grounded, accepted)) {
            result.collided = result.collided || std::abs(substep_x) > comparison_tolerance;
        }
        candidate = state_.feet_position_metres;
        candidate.z += substep_z;
        if (!try_horizontal_move(candidate, maximum_step, state_.grounded, accepted)) {
            result.collided = result.collided || std::abs(substep_z) > comparison_tolerance;
        }
    }

    if (pending_jump_ && state_.grounded) {
        state_.vertical_velocity_metres_per_second = static_cast<double>(
            movement_envelope.jump_impulse_millimetres_per_second)
            / millimetres_per_metre;
        state_.grounded = false;
        result.jumped = true;
        pending_jump_ = false;
    } else if (pending_jump_) {
        pending_jump_ = false;
    }

    const CollisionProbe support{probe_collision_world(
        world_, capsule_, state_.feet_position_metres, maximum_step)};
    if (!state_.grounded) {
        state_.vertical_velocity_metres_per_second -= static_cast<double>(
            movement_envelope.gravity_millimetres_per_second_squared)
            / millimetres_per_metre * step;
        double next_height{state_.feet_position_metres.y
            + state_.vertical_velocity_metres_per_second * step};
        if (support.supported
            && state_.vertical_velocity_metres_per_second > 0.0
            && next_height + capsule_.height_metres > support.ceiling_height_metres) {
            next_height = support.ceiling_height_metres - capsule_.height_metres;
            state_.vertical_velocity_metres_per_second = 0.0;
            result.collided = true;
        }
        if (support.supported
            && state_.vertical_velocity_metres_per_second <= 0.0
            && next_height <= support.floor_height_metres + ground_tolerance_metres
            && state_.feet_position_metres.y >= support.floor_height_metres
                - ground_tolerance_metres) {
            next_height = support.floor_height_metres;
            state_.vertical_velocity_metres_per_second = 0.0;
            state_.grounded = true;
            result.landed = true;
        }
        state_.feet_position_metres.y = next_height;
    } else if (support.supported) {
        state_.feet_position_metres.y = support.floor_height_metres;
        state_.vertical_velocity_metres_per_second = 0.0;
    } else {
        state_.grounded = false;
    }

    const CollisionProbe final_support{probe_collision_world(
        world_, capsule_, state_.feet_position_metres, maximum_step)};
    if (state_.grounded) {
        update_safe_chamber(final_support);
    }
    if (intersects_fall_region(world_, state_.feet_position_metres)) {
        respawn();
        result.respawned = true;
    }
    return result;
}

bool GroundedController::try_horizontal_move(
    GeometryVector3 candidate,
    const double maximum_step_up_metres,
    const bool allow_ground_snap,
    CollisionProbe& accepted_probe)
{
    accepted_probe = probe_collision_world(
        world_, capsule_, candidate, maximum_step_up_metres);
    if (!accepted_probe.supported) {
        return false;
    }
    const double feet_height{allow_ground_snap
            ? accepted_probe.floor_height_metres
            : state_.feet_position_metres.y};
    if (feet_height + capsule_.height_metres
            > accepted_probe.ceiling_height_metres + comparison_tolerance
        || accepted_probe.floor_height_metres
            > state_.feet_position_metres.y + maximum_step_up_metres
                + comparison_tolerance) {
        return false;
    }
    candidate.y = feet_height;
    state_.feet_position_metres = candidate;
    return true;
}

void GroundedController::update_safe_chamber(const CollisionProbe& probe)
{
    if (!probe.supported || probe.contact_kind != GroundContactKind::chamber
        || !probe.chamber_id.has_value()) {
        return;
    }
    const auto chamber = std::find_if(
        world_.chambers.begin(),
        world_.chambers.end(),
        [&](const ChamberCollisionRegion& candidate) {
            return candidate.chamber_id == *probe.chamber_id;
        });
    if (chamber == world_.chambers.end()) {
        return;
    }
    state_.safe_chamber_id = chamber->chamber_id;
    state_.safe_feet_position_metres = {
        chamber->center_metres.x,
        chamber->floor_height_metres,
        chamber->center_metres.z,
    };
}

void GroundedController::respawn() noexcept
{
    state_.feet_position_metres = state_.safe_feet_position_metres;
    state_.vertical_velocity_metres_per_second = 0.0;
    state_.grounded = true;
    pending_jump_ = false;
}

}  // namespace crystalbound
