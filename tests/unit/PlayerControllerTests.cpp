#include "PlayerControllerTests.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "crystalbound/AuthoredChamber.hpp"
#include "crystalbound/PlayerController.hpp"

namespace crystalbound::test {
namespace {

constexpr double tolerance{1.0e-6};
constexpr double pi{3.14159265358979323846};

class ControllerTestFailure final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

void require(const bool condition, const std::string_view message)
{
    if (!condition) {
        throw ControllerTestFailure{std::string{message}};
    }
}

void require_near(const double actual, const double expected, const std::string_view message)
{
    if (!std::isfinite(actual) || std::abs(actual - expected) > tolerance) {
        throw ControllerTestFailure{std::string{message} + ": expected "
            + std::to_string(expected) + ", got " + std::to_string(actual)};
    }
}

template <typename Exception, typename Function>
void require_throws(Function&& function, const std::string_view expected, const std::string_view message)
{
    try {
        function();
    } catch (const Exception& error) {
        if (std::string_view{error.what()}.find(expected) == std::string_view::npos) {
            throw ControllerTestFailure{std::string{message} + ": " + error.what()};
        }
        return;
    }
    throw ControllerTestFailure{std::string{message} + ": no exception"};
}

[[nodiscard]] RouteCollisionRegion route(
    const std::uint64_t id,
    const GroundContactKind kind,
    const GeometryVector3 first,
    const GeometryVector3 second,
    const double width = 1.0)
{
    const double length{std::sqrt(
        (second.x - first.x) * (second.x - first.x)
        + (second.y - first.y) * (second.y - first.y)
        + (second.z - first.z) * (second.z - first.z))};
    return {
        make_edge({static_cast<std::uint32_t>(id)}, {static_cast<std::uint32_t>(id + 1U)}),
        id,
        kind,
        {{first, {1.0, 0.0, 0.0}, 0.0}, {second, {1.0, 0.0, 0.0}, length}},
        width,
        1.35,
        kind == GroundContactKind::bridge ? 0.85 : 0.0,
    };
}

[[nodiscard]] FallCollisionRegion distant_fall()
{
    return {900U, {{90.0, -10.0, 90.0}, {91.0, -9.0, 91.0}}};
}

[[nodiscard]] CollisionWorld open_world(const double radius = 50.0)
{
    CollisionWorld world;
    world.chambers.push_back({{0U}, 0U, {0.0, 0.0, 0.0}, 0.0, 5.0, radius});
    world.routes.push_back(route(
        700U, GroundContactKind::bridge,
        {70.0, 1.35, 70.0}, {75.0, 1.35, 70.0}, 0.50));
    world.fall_regions.push_back(distant_fall());
    world.kill_plane_metres = -20.0;
    return world;
}

[[nodiscard]] GroundedController controller(const double radius = 50.0)
{
    return {open_world(radius), {{0.0, 0.0, 0.0}, {0U}}};
}

ControllerAdvanceResult run_frames(
    GroundedController& target,
    const GroundedMovementInput& input,
    const std::uint32_t count,
    const double delta = 1.0 / 60.0)
{
    ControllerAdvanceResult total;
    for (std::uint32_t index{}; index < count; ++index) {
        const ControllerAdvanceResult result{target.advance(input, delta)};
        total.fixed_ticks += result.fixed_ticks;
        total.backlog_discarded |= result.backlog_discarded;
        total.collided |= result.collided;
        total.jumped |= result.jumped;
        total.landed |= result.landed;
        total.respawned |= result.respawned;
    }
    return total;
}

void locked_contract_and_default_state(const std::filesystem::path&)
{
    const PlayerCapsule capsule{locked_player_capsule()};
    require_near(capsule.radius_metres, 0.35, "capsule radius changed");
    require_near(capsule.height_metres, 1.80, "capsule height changed");
    const GroundedController target{controller()};
    require(target.state().grounded, "spawn is not grounded");
    require_near(target.camera_position_metres().y, 1.62, "camera height changed");
    require_near(target.fixed_step_seconds(), 1.0 / 120.0, "fixed rate changed");
}

void walk_sprint_diagonal_and_yaw(const std::filesystem::path&)
{
    GroundedController walk{controller()};
    GroundedController sprint{controller()};
    GroundedController diagonal{controller()};
    GroundedController yaw{controller()};
    run_frames(walk, {1.0, 0.0, 0.0, false, false}, 60U);
    run_frames(sprint, {1.0, 0.0, 0.0, true, false}, 60U);
    run_frames(diagonal, {1.0, 1.0, 0.0, false, false}, 60U);
    run_frames(yaw, {1.0, 0.0, 90.0, false, false}, 60U);
    require_near(walk.state().feet_position_metres.x, 3.5, "walk speed changed");
    require_near(sprint.state().feet_position_metres.x, 5.5, "sprint speed changed");
    require_near(std::hypot(diagonal.state().feet_position_metres.x,
                     diagonal.state().feet_position_metres.z),
        3.5, "diagonal input was not normalized");
    require_near(yaw.state().feet_position_metres.x, 0.0, "yaw changed wrong axis");
    require_near(yaw.state().feet_position_metres.z, 3.5, "yaw was ignored");
}

void fixed_step_is_frame_chunk_independent(const std::filesystem::path&)
{
    GroundedController coarse{controller()};
    GroundedController fine{controller()};
    const GroundedMovementInput input{1.0, 0.25, 22.0, false, false};
    run_frames(coarse, input, 60U, 1.0 / 60.0);
    run_frames(fine, input, 120U, 1.0 / 120.0);
    require_near(coarse.state().feet_position_metres.x,
        fine.state().feet_position_metres.x, "frame chunks changed x");
    require_near(coarse.state().feet_position_metres.z,
        fine.state().feet_position_metres.z, "frame chunks changed z");
}

void repeated_runs_are_exact(const std::filesystem::path&)
{
    GroundedController first{controller()};
    GroundedController second{controller()};
    for (std::uint32_t tick{}; tick < 360U; ++tick) {
        const GroundedMovementInput input{
            tick < 240U ? 0.5 : -0.25,
            tick % 3U == 0U ? 0.75 : -0.10,
            27.0,
            tick >= 120U && tick < 240U,
            tick == 30U,
        };
        const ControllerAdvanceResult first_result{
            first.advance(input, first.fixed_step_seconds())};
        const ControllerAdvanceResult second_result{
            second.advance(input, second.fixed_step_seconds())};
        require(first_result.fixed_ticks == second_result.fixed_ticks,
            "repeated run tick count changed");
    }
    const PlayerState& first_state{first.state()};
    const PlayerState& second_state{second.state()};
    require(first_state.feet_position_metres.x == second_state.feet_position_metres.x
            && first_state.feet_position_metres.y == second_state.feet_position_metres.y
            && first_state.feet_position_metres.z == second_state.feet_position_metres.z
            && first_state.vertical_velocity_metres_per_second
                == second_state.vertical_velocity_metres_per_second
            && first_state.grounded == second_state.grounded,
        "repeated controller runs diverged");
}

void frame_spikes_are_bounded(const std::filesystem::path&)
{
    GroundedController target{controller()};
    const ControllerAdvanceResult result{
        target.advance({1.0, 0.0, 0.0, false, false}, 1.0)};
    require(result.fixed_ticks == 8U, "catch-up tick limit changed");
    require(result.backlog_discarded, "excess backlog was retained");
    require(target.state().feet_position_metres.x < 0.4, "frame spike moved too far");
}

void jump_lands_without_air_jump(const std::filesystem::path&)
{
    GroundedController target{controller()};
    const double step{target.fixed_step_seconds()};
    require(target.advance({0.0, 0.0, 0.0, false, true}, step).jumped,
        "ground jump failed");
    double maximum_height{};
    bool extra_jump{};
    for (std::uint32_t tick{}; tick < 180U; ++tick) {
        const ControllerAdvanceResult result{target.advance(
            {0.0, 0.0, 0.0, false, tick == 20U}, step)};
        extra_jump |= result.jumped;
        maximum_height = std::max(maximum_height, target.state().feet_position_metres.y);
    }
    require(!extra_jump, "air jump was accepted or buffered");
    require(target.state().grounded, "jump did not land");
    require(maximum_height > 0.70 && maximum_height < 0.90, "jump envelope changed");
}

void ceiling_blocks_capsule(const std::filesystem::path&)
{
    CollisionWorld world{open_world()};
    world.chambers.front().ceiling_height_metres = 1.90;
    GroundedController target{std::move(world), {{0.0, 0.0, 0.0}, {0U}}};
    const double step{target.fixed_step_seconds()};
    static_cast<void>(target.advance({0.0, 0.0, 0.0, false, true}, step));
    run_frames(target, {}, 30U, step);
    require(target.state().feet_position_metres.y <= 0.10 + tolerance,
        "capsule crossed ceiling");
}

void slope_limit_is_enforced(const std::filesystem::path&)
{
    CollisionWorld world{open_world()};
    world.chambers.front().center_metres = {-50.0, 0.0, 0.0};
    const auto sloped = [](const std::uint64_t id, const double degrees) {
        return route(id, GroundContactKind::tunnel, {0.0, 1.35, 0.0},
            {4.0, 1.35 + std::tan(degrees * pi / 180.0) * 4.0, 0.0});
    };
    world.routes.insert(world.routes.begin(), sloped(10U, 30.0));
    const double thirty_floor{std::tan(30.0 * pi / 180.0) * 2.0};
    require(probe_collision_world(world, locked_player_capsule(),
                {2.0, thirty_floor, 0.0}, 0.30).supported,
        "30-degree slope was rejected");
    world.routes.front() = sloped(11U, 40.0);
    const double forty_floor{std::tan(40.0 * pi / 180.0) * 2.0};
    require(!probe_collision_world(world, locked_player_capsule(),
                 {2.0, forty_floor, 0.0}, 0.30).supported,
        "40-degree slope was accepted");
}

[[nodiscard]] CollisionWorld step_world(const double height)
{
    CollisionWorld world;
    world.chambers = {
        {{0U}, 0U, {0.0, 0.0, 0.0}, 0.0, 5.0, 2.5},
        {{1U}, 1U, {5.0, height, 0.0}, height, 5.0, 2.5},
    };
    world.routes.push_back(route(700U, GroundContactKind::bridge,
        {70.0, 1.35, 70.0}, {75.0, 1.35, 70.0}, 0.50));
    world.fall_regions.push_back(distant_fall());
    world.kill_plane_metres = -20.0;
    return world;
}

void step_height_is_enforced(const std::filesystem::path&)
{
    GroundedController accepted{step_world(0.30), {{0.0, 0.0, 0.0}, {0U}}};
    GroundedController rejected{step_world(0.31), {{0.0, 0.0, 0.0}, {0U}}};
    run_frames(accepted, {1.0, 0.0, 0.0, false, false}, 60U);
    run_frames(rejected, {1.0, 0.0, 0.0, false, false}, 60U);
    require(accepted.state().feet_position_metres.x > 2.5, "300 mm step was blocked");
    require_near(accepted.state().feet_position_metres.y, 0.30, "step height was not applied");
    require(rejected.state().feet_position_metres.x <= 2.6, "310 mm step was crossed");
}

void walls_slide_and_prevent_tunneling(const std::filesystem::path&)
{
    GroundedController target{controller(3.0)};
    const ControllerAdvanceResult result{
        run_frames(target, {1.0, 1.0, 0.0, true, false}, 120U)};
    const GeometryVector3 position{target.state().feet_position_metres};
    require(result.collided, "wall contact was not reported");
    require(std::hypot(position.x, position.z) <= 3.05, "sprint tunneled through wall");
    require(position.x > 0.0 && position.z > 0.0, "wall slide lost tangential movement");
}

[[nodiscard]] CollisionWorld connected_world(const bool bridge)
{
    CollisionWorld world;
    world.chambers = {
        {{0U}, 0U, {0.0, 0.0, 0.0}, 0.0, 5.0, 4.5},
        {{1U}, 1U, {14.0, 0.0, 0.0}, 0.0, 5.0, 4.5},
    };
    world.routes.push_back(route(10U,
        bridge ? GroundContactKind::bridge : GroundContactKind::tunnel,
        {4.0, 1.35, 0.0}, {10.0, 1.35, 0.0}, bridge ? 0.50 : 1.0));
    if (!bridge) {
        world.routes.push_back(route(700U, GroundContactKind::bridge,
            {70.0, 1.35, 70.0}, {75.0, 1.35, 70.0}, 0.50));
    }
    world.fall_regions.push_back(distant_fall());
    world.kill_plane_metres = -20.0;
    return world;
}

void tunnel_seams_and_checkpoint_work(const std::filesystem::path&)
{
    GroundedController target{connected_world(false), {{0.0, 0.0, 0.0}, {0U}}};
    run_frames(target, {1.0, 0.0, 0.0, false, false}, 240U);
    require(target.state().feet_position_metres.x > 12.0, "tunnel seam blocked movement");
    require(target.state().safe_chamber_id == NodeId{1U}, "checkpoint did not update");
}

void bridge_rails_block_sideways_motion(const std::filesystem::path&)
{
    GroundedController target{connected_world(true), {{0.0, 0.0, 0.0}, {0U}}};
    run_frames(target, {1.0, 0.0, 0.0, false, false}, 120U);
    require(target.state().feet_position_metres.x > 5.0, "bridge deck was not reached");
    const ControllerAdvanceResult result{
        run_frames(target, {0.0, 1.0, 0.0, true, false}, 60U)};
    require(result.collided, "bridge rail contact was not reported");
    require(std::abs(target.state().feet_position_metres.z) <= 0.55,
        "player crossed bridge rail");
}

void fall_respawns_at_safe_chamber(const std::filesystem::path&)
{
    CollisionWorld world{connected_world(false)};
    world.fall_regions.front() = {900U, {{12.5, -0.1, -1.0}, {13.5, 2.0, 1.0}}};
    GroundedController target{std::move(world), {{0.0, 0.0, 0.0}, {0U}}};
    bool respawned{};
    for (std::uint32_t frame{}; frame < 300U && !respawned; ++frame) {
        respawned = target.advance({1.0, 0.0, 0.0, false, false}, 1.0 / 60.0).respawned;
    }
    require(respawned, "fall did not respawn player");
    require(target.state().safe_chamber_id == NodeId{1U}, "wrong safe chamber");
    require_near(target.state().feet_position_metres.x, 14.0, "respawn x changed");
    require_near(target.state().vertical_velocity_metres_per_second, 0.0,
        "respawn kept velocity");
    require(target.state().grounded, "respawn was not grounded");
}

void generated_scene_produces_valid_collision(const std::filesystem::path&)
{
    const CaveGenerationResult generation{generate_cave({42U})};
    const CollisionWorld world{build_collision_world(generation.scene)};
    require(validate_collision_world(world).empty(), "generated collision world is invalid");
    require(world.chambers.size() == generation.scene.chambers.size(), "chambers were lost");
    require(world.routes.size() == generation.scene.routes.size(), "routes were lost");
    GroundedController target{world, find_start_spawn(generation)};
    require(target.state().grounded, "generated spawn is not grounded");
}

[[nodiscard]] const PortalContract& portal_for_bridge(
    const CaveSceneData& scene, const Edge bridge, const NodeId chamber)
{
    const auto found{std::find_if(
        scene.portals.begin(), scene.portals.end(),
        [bridge, chamber](const PortalContract& portal) {
            return portal.route == bridge && portal.chamber_id == chamber;
        })};
    if (found == scene.portals.end()) {
        throw ControllerTestFailure{"generated bridge portal is missing"};
    }
    return *found;
}

[[nodiscard]] const ChamberCollisionRegion& collision_chamber(
    const CollisionWorld& world, const NodeId chamber)
{
    const auto found{std::find_if(
        world.chambers.begin(), world.chambers.end(),
        [chamber](const ChamberCollisionRegion& candidate) {
            return candidate.chamber_id == chamber;
        })};
    if (found == world.chambers.end()) {
        throw ControllerTestFailure{"generated bridge chamber is missing"};
    }
    return *found;
}

[[nodiscard]] GeometryVector3 portal_safe_position(
    const CollisionWorld& world,
    const PortalContract& portal,
    const ChamberCollisionRegion& chamber)
{
    const double inward_x{
        static_cast<double>(portal.inward_direction_millimetres.x_millimetres)};
    const double inward_z{
        static_cast<double>(portal.inward_direction_millimetres.z_millimetres)};
    const double inward_length{std::hypot(inward_x, inward_z)};
    require(inward_length > tolerance, "generated bridge portal direction is invalid");
    constexpr double search_step_metres{0.25};
    constexpr double maximum_inset_metres{8.0};
    constexpr double generated_route_radius_metres{1.35};
    const double portal_floor_height{
        portal.center_millimetres.y_millimetres / 1'000.0
        - generated_route_radius_metres};
    for (double inset{route_junction_depth_millimetres / 1'000.0
             + search_step_metres};
         inset <= maximum_inset_metres;
         inset += search_step_metres) {
        const GeometryVector3 candidate{
            static_cast<double>(portal.center_millimetres.x_millimetres) / 1'000.0
                + inward_x / inward_length * inset,
            portal_floor_height,
            static_cast<double>(portal.center_millimetres.z_millimetres) / 1'000.0
                + inward_z / inward_length * inset,
        };
        const CollisionProbe support{probe_collision_world(
            world, locked_player_capsule(), candidate, 0.30)};
        if (support.supported && support.chamber_id == chamber.chamber_id
            && !intersects_fall_region(world, candidate)) {
            return candidate;
        }
    }
    throw ControllerTestFailure{
        "generated bridge portal has no safe grounded chamber-side transform"};
}

void traverse_generated_route(
    const CaveGenerationResult& generation,
    const CollisionWorld& world,
    const RouteCollisionRegion& route,
    const bool reverse,
    const bool start_at_chamber_center = false)
{
    const NodeId start_id{reverse ? route.edge.second : route.edge.first};
    const NodeId destination_id{reverse ? route.edge.first : route.edge.second};
    const PortalContract& start_portal{
        portal_for_bridge(generation.scene, route.edge, start_id)};
    const PortalContract& destination_portal{
        portal_for_bridge(generation.scene, route.edge, destination_id)};
    const ChamberCollisionRegion& start_chamber{
        collision_chamber(world, start_id)};
    const GeometryVector3 start{start_at_chamber_center
            ? GeometryVector3{start_chamber.center_metres.x,
                start_chamber.floor_height_metres,
                start_chamber.center_metres.z}
            : portal_safe_position(world, start_portal, start_chamber)};
    const GeometryVector3 destination{portal_safe_position(
        world, destination_portal, collision_chamber(world, destination_id))};

    std::vector<GeometryVector3> waypoints;
    waypoints.reserve(route.samples.size() + 1U);
    if (reverse) {
        for (auto sample{route.samples.rbegin()}; sample != route.samples.rend(); ++sample) {
            waypoints.push_back(sample->position_metres);
        }
    } else {
        for (const SplineSample& sample : route.samples) {
            waypoints.push_back(sample.position_metres);
        }
    }
    waypoints.push_back(destination);

    GroundedController controller{world, {start, start_id}};
    constexpr std::uint32_t maximum_ticks{120U * 60U};
    constexpr double waypoint_radius_metres{0.12};
    constexpr double destination_radius_metres{0.08};
    constexpr double maximum_step_metres{0.30};
    std::size_t waypoint_index{};
    bool saw_start_chamber{};
    bool saw_route_after_start{};
    bool saw_destination_after_route{};
    bool respawned{};

    for (std::uint32_t tick{}; tick < maximum_ticks; ++tick) {
        const GeometryVector3 before{controller.state().feet_position_metres};
        while (waypoint_index + 1U < waypoints.size()
            && std::hypot(
                   waypoints[waypoint_index].x - before.x,
                   waypoints[waypoint_index].z - before.z)
                <= waypoint_radius_metres) {
            ++waypoint_index;
        }
        const GeometryVector3& target{waypoints[waypoint_index]};
        const double yaw_degrees{
            std::atan2(target.z - before.z, target.x - before.x) * 180.0 / pi};
        const ControllerAdvanceResult result{controller.advance(
            {1.0, 0.0, yaw_degrees, false, false},
            controller.fixed_step_seconds())};
        respawned = respawned || result.respawned;

        const PlayerState& state{controller.state()};
        require(std::isfinite(state.feet_position_metres.x)
                && std::isfinite(state.feet_position_metres.y)
                && std::isfinite(state.feet_position_metres.z)
                && std::isfinite(state.vertical_velocity_metres_per_second),
            "generated bridge replay produced non-finite state");
        require(std::abs(state.feet_position_metres.y - before.y)
                <= maximum_step_metres + tolerance,
            std::string{"generated route replay crossed an illegal vertical step on edge ("}
                + std::to_string(route.edge.first.value) + ", "
                + std::to_string(route.edge.second.value) + "): before_y="
                + std::to_string(before.y) + ", after_y="
                + std::to_string(state.feet_position_metres.y));
        require(state.grounded, "generated bridge replay became unsupported");

        const CollisionProbe contact{probe_collision_world(
            world, locked_player_capsule(), state.feet_position_metres,
            maximum_step_metres)};
        require(contact.supported, "generated bridge replay found a support gap");
        if (!saw_start_chamber && contact.contact_kind == GroundContactKind::chamber
            && contact.chamber_id == start_id) {
            saw_start_chamber = true;
        } else if (saw_start_chamber && contact.contact_kind == route.kind
            && contact.stable_object_id == route.stable_object_id) {
            saw_route_after_start = true;
        } else if (saw_route_after_start
            && contact.contact_kind == GroundContactKind::chamber
            && contact.chamber_id == destination_id) {
            saw_destination_after_route = true;
        }

        if (std::hypot(destination.x - state.feet_position_metres.x,
                destination.z - state.feet_position_metres.z)
            <= destination_radius_metres) {
            break;
        }
    }

    const GeometryVector3 final_position{controller.state().feet_position_metres};
    const GeometryVector3& route_endpoint{
        reverse ? route.samples.back().position_metres
                : route.samples.front().position_metres};
    const double chamber_distance{std::hypot(
        final_position.x - start_chamber.center_metres.x,
        final_position.z - start_chamber.center_metres.z)};
    const double endpoint_distance{std::hypot(
        final_position.x - route_endpoint.x,
        final_position.z - route_endpoint.z)};
    const double endpoint_floor{route_endpoint.y - route.tunnel_radius_metres};
    require(!respawned, "generated route replay respawned");
    const std::string destination_failure{
        (reverse ? "seed-42 route reverse traversal" : "seed-42 route forward traversal")
        + std::string{" edge=("} + std::to_string(route.edge.first.value)
        + ", " + std::to_string(route.edge.second.value) + "), start=("
        + std::to_string(start.x) + ", " + std::to_string(start.y) + ", "
        + std::to_string(start.z) + ")"
        + std::string{" did not reach the destination chamber; final=("}
        + std::to_string(final_position.x) + ", "
        + std::to_string(final_position.y) + ", "
        + std::to_string(final_position.z) + "), destination=("
        + std::to_string(destination.x) + ", "
        + std::to_string(destination.y) + ", "
        + std::to_string(destination.z) + "), chamber_distance="
        + std::to_string(chamber_distance) + ", chamber_radius="
        + std::to_string(start_chamber.usable_radius_metres)
        + ", endpoint_distance=" + std::to_string(endpoint_distance)
        + ", route_half_width=" + std::to_string(route.usable_half_width_metres)
        + ", endpoint_floor_delta="
        + std::to_string(endpoint_floor - start_chamber.floor_height_metres)
        + ", in_fall_region="
        + (intersects_fall_region(world, final_position) ? "true" : "false")};
    require(std::hypot(destination.x - final_position.x,
                destination.z - final_position.z)
            <= destination_radius_metres,
        destination_failure);
    const std::string direction{reverse ? "reverse" : "forward"};
    require(saw_start_chamber,
        direction + " generated bridge replay missed the start chamber contact");
    require(saw_route_after_start,
        direction + " generated route replay missed the route contact");
    require(saw_destination_after_route,
        direction + " generated route replay missed the destination chamber contact");
}

void generated_seed_42_tunnels_have_continuous_support(
    const std::filesystem::path&)
{
    const CaveGenerationResult generation{generate_cave({42U})};
    const CollisionWorld world{build_collision_world(generation.scene)};
    require(generation.scene.bridge_routes.empty(),
        "seed 42 tunnel layout unexpectedly selected a bridge");
    require(world.routes.size() == 6U,
        "seed 42 tunnel layout must contain six collision routes");
    for (const RouteCollisionRegion& route : world.routes) {
        require(route.kind == GroundContactKind::tunnel,
            "seed 42 connector is not a tunnel collision route");
        require(route.directed[0].chamber_to_junction_supported
                && route.directed[0].junction_to_route_supported
                && route.directed[0].route_to_chamber_supported
                && route.directed[1].chamber_to_junction_supported
                && route.directed[1].junction_to_route_supported
                && route.directed[1].route_to_chamber_supported,
            "seed 42 tunnel has an unsupported endpoint seam");
        for (const SplineSample& sample : route.samples) {
            const GeometryVector3 feet{
                sample.position_metres.x,
                sample.position_metres.y - route.tunnel_radius_metres,
                sample.position_metres.z};
            require(std::abs(feet.y) < tolerance,
                "seed 42 tunnel support changes floor elevation");
            require(probe_collision_world(
                        world, locked_player_capsule(), feet, 0.30)
                        .supported,
                "seed 42 tunnel sample has a support gap");
        }
    }
}

void generated_seed_42_earth_water_tunnel_is_traversable_both_directions(
    const std::filesystem::path& testdata_directory)
{
    const CaveGenerationResult generation{generate_cave({42U})};
    const auto element_node = [&](const Element element) -> const ChamberNode& {
        const auto found{std::find_if(generation.generation.topology.nodes.begin(),
            generation.generation.topology.nodes.end(),
            [element](const ChamberNode& node) {
                return node.element == std::optional<Element>{element};
            })};
        require(found != generation.generation.topology.nodes.end(),
            "seed 42 is missing an elemental chamber");
        return *found;
    };
    const Edge earth_water{make_edge(
        element_node(Element::earth).id, element_node(Element::water).id)};
    const auto scene_route{std::find_if(generation.scene.routes.begin(),
        generation.scene.routes.end(), [earth_water](const RouteGeometryContract& route) {
            return route.edge == earth_water;
        })};
    require(scene_route != generation.scene.routes.end(),
        "seed 42 is missing the direct Earth-Water route");
    require(!scene_route->bridge,
        "seed 42 Earth-Water connection must exercise the ordinary tunnel");

    CollisionWorld world{build_collision_world(generation.scene)};
    const auto collision_route{std::find_if(world.routes.begin(), world.routes.end(),
        [earth_water](const RouteCollisionRegion& route) {
            return route.edge == earth_water
                && route.kind == GroundContactKind::tunnel;
        })};
    require(collision_route != world.routes.end(),
        "seed 42 Earth-Water tunnel collision route is missing");
    const MaterialModelLoadResult water_model{load_obj_material_batches(
        testdata_directory / "WaterChamber.obj")};
    const MaterialModelLoadResult earth_model{load_obj_material_batches(
        testdata_directory / "EarthChamber.obj")};
    append_authored_chamber_collision(world,
        build_water_chamber_collision(water_model,
            water_chamber_placement(generation.scene)));
    append_authored_chamber_collision(world,
        build_earth_chamber_collision(
            earth_model, earth_chamber_placement(generation.scene)));
    const NodeId earth_id{element_node(Element::earth).id};
    const bool earth_to_water_reverse{collision_route->edge.second == earth_id};
    traverse_generated_route(
        generation, world, *collision_route, earth_to_water_reverse);
    traverse_generated_route(
        generation, world, *collision_route, !earth_to_water_reverse);
}

void invalid_data_is_rejected(const std::filesystem::path&)
{
    require_throws<ControllerError>(
        [] { static_cast<void>(GroundedController{{}, {{0.0, 0.0, 0.0}, {0U}}}); },
        "at least one chamber", "empty world must fail");
    GroundedController target{controller()};
    require_throws<ControllerError>(
        [&] { static_cast<void>(target.advance(
            {std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0, false, false}, 0.01)); },
        "must be finite", "non-finite input must fail");
    require_throws<ControllerError>(
        [&] { static_cast<void>(target.advance({}, std::numeric_limits<double>::infinity())); },
        "frame delta", "non-finite delta must fail");
    require_throws<ControllerError>(
        [&] { static_cast<void>(target.advance({}, -0.01)); },
        "non-negative", "negative delta must fail");
    require_throws<ControllerError>(
        [&] { static_cast<void>(probe_collision_world(
            open_world(), {0.0, 1.80}, {0.0, 0.0, 0.0}, 0.30)); },
        "invalid capsule", "invalid capsule must fail");
    require_throws<ControllerError>(
        [] {
            CollisionWorld world{open_world()};
            world.routes.clear();
            static_cast<void>(GroundedController{
                std::move(world), {{0.0, 0.0, 0.0}, {0U}}});
        },
        "route region", "missing route category must fail");
    require_throws<ControllerError>(
        [] {
            CollisionWorld world{open_world()};
            world.fall_regions.clear();
            static_cast<void>(GroundedController{
                std::move(world), {{0.0, 0.0, 0.0}, {0U}}});
        },
        "fall region", "missing fall category must fail");
    require_throws<ControllerError>(
        [] {
            static_cast<void>(GroundedController{
                open_world(), {{80.0, 0.0, 80.0}, {0U}}});
        },
        "spawn must be grounded", "impossible spawn must fail");
    require_throws<ControllerError>(
        [] {
            CollisionWorld world{open_world()};
            world.fall_regions.front() = {
                900U, {{-1.0, -1.0, -1.0}, {1.0, 1.0, 1.0}}};
            static_cast<void>(GroundedController{
                std::move(world), {{0.0, 0.0, 0.0}, {0U}}});
        },
        "unsafe chamber respawn", "unsafe respawn must fail");
}

void supplied_water_room_matches_authored_gateways_and_shrine(
    const std::filesystem::path& testdata_directory)
{
    const MaterialModelLoadResult model{load_obj_material_batches(
        testdata_directory / "WaterChamber.obj")};
    require_near(model.minimum_bounds[0], -25.280001,
        "supplied Water room minimum x changed");
    require_near(model.maximum_bounds[0], 22.450001,
        "supplied Water room maximum x changed");
    require_near(model.minimum_bounds[1], -0.71,
        "supplied Water room minimum y changed");
    require_near(model.maximum_bounds[1], 13.88,
        "supplied Water room maximum y changed");
    require_near(model.minimum_bounds[2], -16.450001,
        "supplied Water room minimum z changed");
    require_near(model.maximum_bounds[2], 19.280001,
        "supplied Water room maximum z changed");

    const auto crystal_socket{std::find_if(
        model.objects.begin(), model.objects.end(),
        [](const MaterialModelObject& object) {
            return object.name == "EMPTY_CRYSTAL_SOCKET";
        })};
    require(crystal_socket != model.objects.end(),
        "supplied Water shrine has no crystal socket marker");
    require_near((crystal_socket->minimum_bounds[0]
                     + crystal_socket->maximum_bounds[0])
            * 0.5,
        0.0, "Water crystal socket is not centered on x");
    require_near((crystal_socket->minimum_bounds[2]
                     + crystal_socket->maximum_bounds[2])
            * 0.5,
        0.0, "Water crystal socket is not centered on z");
    require_near(crystal_socket->maximum_bounds[1], 2.2025,
        "Water crystal socket height changed");

    const CaveGenerationResult generation{generate_cave({42U})};
    const AuthoredChamberPlacement placement{
        water_chamber_placement(generation.scene)};
    const auto compiled{std::find_if(
        generation.scene.compiled_chambers.begin(),
        generation.scene.compiled_chambers.end(),
        [](const CompiledChamberTemplate& chamber) {
            return chamber.role == ChamberTemplateRole::water;
        })};
    require(compiled != generation.scene.compiled_chambers.end(),
        "seed 42 has no compiled Water chamber");

    std::vector<TemplatePoint2> local_portals;
    for (const PortalContract& portal : generation.scene.portals) {
        if (portal.chamber_id != placement.chamber_id) {
            continue;
        }
        const double relative_x{
            portal.center_millimetres.x_millimetres
                / 1'000.0 - placement.translation_metres.x};
        const double relative_z{
            portal.center_millimetres.z_millimetres
                / 1'000.0 - placement.translation_metres.z};
        const double cosine{std::cos(placement.yaw_radians)};
        const double sine{std::sin(placement.yaw_radians)};
        local_portals.push_back({
            static_cast<std::int32_t>(std::llround(
                (relative_x * cosine - relative_z * sine) * 1'000.0)),
            static_cast<std::int32_t>(std::llround(
                (relative_x * sine + relative_z * cosine) * 1'000.0)),
        });
        require(portal.center_millimetres.y_millimetres == 1'350,
            "Water route center does not preserve the shared zero-height floor datum");
    }
    std::sort(local_portals.begin(), local_portals.end(),
        [](const TemplatePoint2 left, const TemplatePoint2 right) {
            if (left.x_millimetres != right.x_millimetres) {
                return left.x_millimetres < right.x_millimetres;
            }
            return left.z_millimetres < right.z_millimetres;
        });
    require(local_portals.size() == 2U,
        "authored Water room requires exactly two connected entrances");
    require(local_portals[0] == TemplatePoint2{-25'200, 0}
            && local_portals[1] == TemplatePoint2{0, 19'200},
        "generated Water routes do not meet the west and south entrances");

    const auto water_visual{std::find_if(
        generation.scene.elemental_visuals.chambers.begin(),
        generation.scene.elemental_visuals.chambers.end(),
        [](const ElementalChamberVisual& chamber) {
            return chamber.element == Element::water;
        })};
    require(water_visual
            != generation.scene.elemental_visuals.chambers.end(),
        "seed 42 has no Water collectible");
    require(water_visual->crystal.base_position_millimetres.x_millimetres
                == static_cast<std::int32_t>(std::llround(
                    placement.translation_metres.x * 1'000.0))
            && water_visual->crystal.base_position_millimetres.z_millimetres
                == static_cast<std::int32_t>(std::llround(
                    placement.translation_metres.z * 1'000.0))
            && water_visual->crystal.base_position_millimetres.y_millimetres
                == static_cast<std::int32_t>(std::llround(
                    (placement.translation_metres.y
                        - authored_water_vertical_offset_metres)
                    * 1'000.0))
                    + authored_water_crystal_base_height_millimetres,
        "Water collectible does not sit in the pavilion socket");
}

void authored_water_objects_build_solid_collision(const std::filesystem::path&)
{
    MaterialModelLoadResult model;
    model.objects = {
        {"ContinuousCollisionFloor", "M_CarraraWet", {-2.0F, -0.50F, -2.0F},
            {2.0F, -0.35F, 2.0F}},
        {"ENTRANCE_WEST_Landing", "M_CarraraPolished", {-4.0F, -0.35F, -1.0F},
            {-2.0F, 1.30F, 1.0F}},
        {"ShrineStepLower", "M_CarraraWet", {-1.5F, -0.35F, -1.5F},
            {1.5F, 0.01F, 1.5F}},
        {"Pillar0_BaseLower", "M_CarraraPolished", {-1.0F, -0.40F, -1.0F},
            {1.0F, 0.60F, 1.0F}},
        {"Pillar0_Shaft", "M_CarraraPolished", {-0.8F, 0.50F, -0.8F},
            {0.8F, 1.60F, 0.8F}},
        {"ClearStillWater", "M_ClearStillWater", {-2.0F, -0.20F, -2.0F},
            {2.0F, 0.50F, 2.0F}},
    };
    const AuthoredChamberPlacement placement{
        {5U}, {10.0, authored_water_vertical_offset_metres, 20.0},
        0.0, {1.0, 1.0, 1.0}};
    const AuthoredChamberCollisionContract collision{
        build_water_chamber_collision(model, placement)};
    require(collision.supports.size() == 3U,
        "authored floor, landing, and shrine step must be walkable");
    require(collision.blockers.size() == 1U,
        "segmented pillar and crust must merge into one solid blocker");
    require(std::all_of(collision.supports.begin(), collision.supports.end(),
                [](const ChamberSupportRegion& support) {
                    return support.chamber_id == NodeId{5U};
                }),
        "authored support lost Water chamber ownership");
    const auto& polygon{collision.blockers.front().world_polygon_millimetres};
    require(polygon.size() == 4U,
        "authored blocker must be a transformed rectangle");
    require(polygon.front() == TemplatePoint2{8'550, 18'550},
        "authored blocker must include the capsule safety expansion");
}

void supplied_water_interior_preserves_scale_and_collision(
    const std::filesystem::path& testdata_directory)
{
    const MaterialModelLoadResult model{load_obj_material_batches(
        testdata_directory / "WaterChamber.obj")};
    require(model.objects.size() == 565U,
        "supplied Water room must retain all 565 named objects");
    require_near(model.minimum_bounds[0], -25.280001,
        "supplied Water interior minimum x changed");
    require_near(model.maximum_bounds[0], 22.450001,
        "supplied Water interior maximum x changed");
    require_near(model.minimum_bounds[1], -0.71,
        "supplied Water interior minimum y changed");
    require_near(model.maximum_bounds[1], 13.88,
        "supplied Water interior maximum y changed");
    require_near(model.minimum_bounds[2], -16.450001,
        "supplied Water interior minimum z changed");
    require_near(model.maximum_bounds[2], 19.280001,
        "supplied Water interior maximum z changed");
    require(std::count_if(model.objects.begin(), model.objects.end(),
                [](const MaterialModelObject& object) {
                    return object.name == "ClearStillWater";
                }) == 1,
        "supplied Water room must have one non-solid water surface");

    const CaveGenerationResult generation{generate_cave({42U})};
    const AuthoredChamberPlacement placement{
        water_chamber_placement(generation.scene)};
    require(placement.scale.x == 1.0 && placement.scale.y == 1.0
            && placement.scale.z == 1.0,
        "supplied Water interior must remain at authored 1:1 scale");
    const AuthoredChamberCollisionContract collision{
        build_water_chamber_collision(model, placement)};
    require(collision.supports.size() == 18U,
        "Water floor, entrance stairs, landings, and shrine tiers must be walkable");
    require(collision.blockers.size() == 41U,
        "Water walls, stairs, passage sides, pillars, and shrine structure must be solid");

    CollisionWorld world{build_collision_world(generation.scene)};
    append_authored_chamber_collision(world, collision);
    const double cosine{std::cos(placement.yaw_radians)};
    const double sine{std::sin(placement.yaw_radians)};
    const auto world_position = [&](const double local_x, const double local_z) {
        return GeometryVector3{
            placement.translation_metres.x
                + local_x * cosine + local_z * sine,
            placement.translation_metres.y
                - authored_water_vertical_offset_metres,
            placement.translation_metres.z
                - local_x * sine + local_z * cosine,
        };
    };
    for (const GeometryVector3 local : {
             GeometryVector3{-15.0, -0.35, 0.0},
             GeometryVector3{15.0, -0.35, 0.0},
             GeometryVector3{0.0, -0.35, -10.0},
             GeometryVector3{0.0, -0.35, 10.0}}) {
        GeometryVector3 feet{world_position(local.x, local.z)};
        feet.y = placement.translation_metres.y + local.y;
        const CollisionProbe probe{probe_collision_world(
            world, locked_player_capsule(), feet, 0.30)};
        require(probe.supported
                && probe.chamber_id == std::optional<NodeId>{placement.chamber_id}
                && !intersects_fall_region(world, feet),
            "authored Water enclosure retains an unsupported floor strip");
    }
    std::size_t traversed_routes{};
    for (const RouteCollisionRegion& route : world.routes) {
        if (route.edge.first != placement.chamber_id
            && route.edge.second != placement.chamber_id) {
            continue;
        }
        const bool leaving_water_is_reverse{
            route.edge.second == placement.chamber_id};
        traverse_generated_route(
            generation, world, route, leaving_water_is_reverse);
        traverse_generated_route(
            generation, world, route, !leaving_water_is_reverse);
        ++traversed_routes;
    }
    require(traversed_routes == 2U,
        "both authored Water entrances must connect to traversable generated routes");
}

void supplied_water_entrances_are_open_and_stairs_are_solid(
    const std::filesystem::path& testdata_directory)
{
    const std::filesystem::path water_path{
        testdata_directory / "WaterChamber.obj"};
    const MaterialModelLoadResult rendered{
        load_water_chamber_render_asset(water_path)};
    require(std::none_of(rendered.batches.begin(), rendered.batches.end(),
                [](const MaterialMeshBatch& batch) {
                    return batch.material_name == "M_PassageDark";
                }),
        "authored Water entrance still renders a walk-through dark wall");
}

void supplied_water_entrance_stairs_have_solid_bodies(
    const std::filesystem::path& testdata_directory)
{
    const std::filesystem::path water_path{
        testdata_directory / "WaterChamber.obj"};
    const MaterialModelLoadResult collision_model{
        load_obj_material_batches(water_path)};
    const CaveGenerationResult generation{generate_cave({42U})};
    const AuthoredChamberPlacement placement{
        water_chamber_placement(generation.scene)};
    const AuthoredChamberCollisionContract collision{
        build_water_chamber_collision(collision_model, placement)};
    require(collision.blockers.size() == 41U,
        "authored Water stair bodies are not solid");

    CollisionWorld world{build_collision_world(generation.scene)};
    append_authored_chamber_collision(world, collision);
    const double cosine{std::cos(placement.yaw_radians)};
    const double sine{std::sin(placement.yaw_radians)};
    const auto world_position = [&](const double local_x,
                                    const double local_y,
                                    const double local_z) {
        return GeometryVector3{
            placement.translation_metres.x
                + local_x * cosine + local_z * sine,
            placement.translation_metres.y + local_y,
            placement.translation_metres.z
                - local_x * sine + local_z * cosine,
        };
    };
    const double first_step_height{
        placement.translation_metres.y + 1.064286};
    for (const GeometryVector3 seam : {
             GeometryVector3{-21.98, 1.30, 0.0},
             GeometryVector3{0.0, 1.30, 15.98}}) {
        const CollisionProbe seam_probe{probe_collision_world(
            world, locked_player_capsule(),
            world_position(seam.x, seam.y, seam.z), 0.30)};
        require(seam_probe.supported,
            "Water entrance seam has no collision support");
        require_near(seam_probe.floor_height_metres, first_step_height,
            "Water entrance seam drops the player behind the first step");
    }
    const GeometryVector3 inside_west_step_three{
        placement.translation_metres.x - 19.99 * cosine,
        placement.translation_metres.y - authored_water_vertical_offset_metres,
        placement.translation_metres.z + 19.99 * sine,
    };
    const CollisionProbe blocked{probe_collision_world(
        world, locked_player_capsule(), inside_west_step_three, 0.30)};
    require(!blocked.supported,
        "player can pass through the solid side of a raised Water entrance step");

    GeometryVector3 recoverable_step_up{inside_west_step_three};
    recoverable_step_up.y = placement.translation_metres.y
        - authored_water_vertical_offset_metres + 0.706143;
    const CollisionProbe recovered{probe_collision_world(
        world, locked_player_capsule(), recoverable_step_up, 0.30)};
    require(recovered.supported,
        "a small downward drift traps the player behind the Water stairs");
    require_near(recovered.floor_height_metres,
        placement.translation_metres.y
            - authored_water_vertical_offset_metres + 0.943,
        "recoverable Water stair step-up chose the wrong tread");

    GeometryVector3 recoverable_south_step_three{
        placement.translation_metres.x + 13.99 * sine,
        placement.translation_metres.y
            - authored_water_vertical_offset_metres + 0.706143,
        placement.translation_metres.z + 13.99 * cosine,
    };
    const CollisionProbe recovered_south{probe_collision_world(
        world, locked_player_capsule(), recoverable_south_step_three, 0.30)};
    require(recovered_south.supported,
        "a small downward drift traps the player behind the south Water stairs");
    require_near(recovered_south.floor_height_metres,
        placement.translation_metres.y
            - authored_water_vertical_offset_metres + 0.943,
        "recoverable south Water stair step-up chose the wrong tread");
}

void supplied_earth_interior_preserves_scale_collision_and_seams(
    const std::filesystem::path& testdata_directory)
{
    const MaterialModelLoadResult model{load_obj_material_batches(
        testdata_directory / "EarthChamber.obj")};
    const auto gateway_floor_count = [&](const std::string_view prefix) {
        return std::count_if(model.objects.begin(), model.objects.end(),
            [prefix](const MaterialModelObject& object) {
                return object.name.rfind(prefix, 0U) == 0U;
            });
    };
    require(gateway_floor_count("Gate0_Floor_") == 4
            && gateway_floor_count("Gate1_Floor_") == 4,
        "supplied Earth chamber must expose both four-piece gateway floors");
    require(model.objects.size() == 2'119U,
        "supplied Earth interior must retain all 2,119 object sections");
    require_near(model.minimum_bounds[0], -21.468859,
        "supplied Earth interior minimum x changed");
    require_near(model.maximum_bounds[0], 23.270716,
        "supplied Earth interior maximum x changed");
    require_near(model.minimum_bounds[1], -1.342646,
        "supplied Earth interior minimum y changed");
    require_near(model.maximum_bounds[1], 12.555,
        "supplied Earth interior maximum y changed");
    require_near(model.minimum_bounds[2], -21.473303,
        "supplied Earth interior minimum z changed");
    require_near(model.maximum_bounds[2], 23.270716,
        "supplied Earth interior maximum z changed");

    const CaveGenerationResult generation{generate_cave({42U})};
    const AuthoredChamberPlacement placement{
        earth_chamber_placement(generation.scene)};
    require(placement.scale.x == 1.0 && placement.scale.y == 1.0
            && placement.scale.z == 1.0,
        "supplied Earth interior must remain at authored 1:1 scale");
    const AuthoredChamberCollisionContract collision{
        build_earth_chamber_collision(model, placement)};
    require(collision.supports.size() == 3U,
        "Earth floor and both continuous gateway floors must be walkable");
    require(collision.blockers.size() == 269U,
        "Earth stones and both gateway structures must be solid");
    require_near(collision.supports.front().floor_height_metres,
        placement.translation_metres.y,
        "full-scale Earth floor must align with chamber ground");

    const auto earth_chamber{std::find_if(generation.scene.chambers.begin(),
        generation.scene.chambers.end(),
        [placement](const ChamberGeometryContract& chamber) {
            return chamber.node_id == placement.chamber_id;
        })};
    require(earth_chamber != generation.scene.chambers.end()
            && earth_chamber->wall_height_millimetres >= 13'000,
        "Earth stone enclosure does not clear the 12.555-metre authored interior");
    require(std::any_of(generation.scene.mesh_pieces.begin(),
                generation.scene.mesh_pieces.end(),
                [placement](const SceneMeshPiece& piece) {
                    return piece.owner_chamber_id
                            == std::optional<NodeId>{placement.chamber_id}
                        && piece.kind == ScenePieceKind::chamber_shell
                        && piece.material == MaterialKind::soil_mineral;
                }),
        "Earth enclosure lost its procedural earth-and-stone material");

    CollisionWorld world{build_collision_world(generation.scene)};
    append_authored_chamber_collision(world, collision);
    const auto compiled{std::find_if(
        generation.scene.compiled_chambers.begin(),
        generation.scene.compiled_chambers.end(),
        [placement](const CompiledChamberTemplate& chamber) {
            return chamber.chamber_id == placement.chamber_id;
        })};
    require(compiled != generation.scene.compiled_chambers.end(),
        "seed 42 has no compiled Earth chamber");
    std::vector<TemplatePoint2> local_portals;
    std::size_t checked_portals{};
    for (const PortalContract& portal : generation.scene.portals) {
        if (portal.chamber_id != placement.chamber_id) {
            continue;
        }
        const TemplatePoint2 relative{
            portal.center_millimetres.x_millimetres
                - earth_chamber->center_millimetres.x_millimetres,
            portal.center_millimetres.z_millimetres
                - earth_chamber->center_millimetres.z_millimetres,
        };
        local_portals.push_back(rotate_template_point(relative,
            static_cast<std::uint8_t>(
                (8U - compiled->orientation_octant) % 8U)));
        const double inward_x{static_cast<double>(
            portal.inward_direction_millimetres.x_millimetres)};
        const double inward_z{static_cast<double>(
            portal.inward_direction_millimetres.z_millimetres)};
        const double inward_length{std::hypot(inward_x, inward_z)};
        require(inward_length > tolerance,
            "Earth portal has no inward direction");
        for (double inset{-0.5}; inset <= 3.0; inset += 0.25) {
            const GeometryVector3 feet{
                portal.center_millimetres.x_millimetres / 1'000.0
                    + inward_x / inward_length * inset,
                placement.translation_metres.y,
                portal.center_millimetres.z_millimetres / 1'000.0
                    + inward_z / inward_length * inset,
            };
            const CollisionProbe probe{probe_collision_world(
                world, locked_player_capsule(), feet, 0.30)};
            require(probe.supported && !intersects_fall_region(world, feet),
                "Earth tunnel-to-floor seam contains a support gap or wall blocker");
        }
        ++checked_portals;
    }
    require(checked_portals == 2U,
        "Earth chamber must connect exactly two tunnel openings");
    std::sort(local_portals.begin(), local_portals.end(),
        [](const TemplatePoint2 left, const TemplatePoint2 right) {
            if (left.x_millimetres != right.x_millimetres) {
                return left.x_millimetres < right.x_millimetres;
            }
            return left.z_millimetres < right.z_millimetres;
        });
    require(local_portals
            == std::vector<TemplatePoint2>{{0, 23'200}, {23'200, 0}},
        "generated Earth routes do not meet Gate0 and Gate1 thresholds");

    const double cosine{std::cos(placement.yaw_radians)};
    const double sine{std::sin(placement.yaw_radians)};
    const auto world_position = [&](const double local_x,
                                    const double local_z) {
        return GeometryVector3{
            placement.translation_metres.x
                + local_x * cosine + local_z * sine,
            placement.translation_metres.y + 0.0462,
            placement.translation_metres.z
                - local_x * sine + local_z * cosine,
        };
    };
    for (double distance{19.2}; distance <= 23.2; distance += 0.05) {
        for (const GeometryVector3 feet : {
                 world_position(distance, 0.0),
                 world_position(0.0, distance)}) {
            const CollisionProbe probe{probe_collision_world(
                world, locked_player_capsule(), feet, 0.30)};
            require(probe.supported && !intersects_fall_region(world, feet),
                "authored Earth gateway floor contains a collision gap");
            require(probe.floor_height_metres
                    >= placement.translation_metres.y - tolerance,
                "authored Earth gateway dropped to a lower fallback floor");
        }
    }

    std::size_t traversed_routes{};
    for (const RouteCollisionRegion& route : world.routes) {
        if (route.edge.first != placement.chamber_id
            && route.edge.second != placement.chamber_id) {
            continue;
        }
        const bool leaving_earth_is_reverse{
            route.edge.second == placement.chamber_id};
        traverse_generated_route(
            generation, world, route, leaving_earth_is_reverse);
        traverse_generated_route(
            generation, world, route, !leaving_earth_is_reverse);
        ++traversed_routes;
    }
    require(traversed_routes == 2U,
        "both authored Earth gateways must be traversable in both directions");
}

void seed_42_air_portals_match_authored_gateway_thresholds(
    const std::filesystem::path& testdata_directory)
{
    const MaterialModelLoadResult model{load_obj_material_batches(
        testdata_directory / "AirChamber.obj")};
    const MaterialModelLoadResult fire_model{load_obj_material_batches(
        testdata_directory / "FireChamber.obj")};
    const MaterialModelLoadResult earth_model{load_obj_material_batches(
        testdata_directory / "EarthChamber.obj")};
    require_near(model.minimum_bounds[0], -24.960577,
        "supplied Air chamber minimum x changed");
    require_near(model.maximum_bounds[0], 24.257584,
        "supplied Air chamber maximum x changed");
    require_near(model.minimum_bounds[1], -1.199669,
        "supplied Air chamber minimum y changed");
    require_near(model.maximum_bounds[1], 22.0,
        "supplied Air chamber maximum y changed");
    require_near(model.minimum_bounds[2], -24.83,
        "supplied Air chamber minimum z changed");
    require_near(model.maximum_bounds[2], 24.83,
        "supplied Air chamber maximum z changed");

    const CaveGenerationResult generation{generate_cave({42U})};
    const auto compiled{std::find_if(
        generation.scene.compiled_chambers.begin(),
        generation.scene.compiled_chambers.end(),
        [](const CompiledChamberTemplate& chamber) {
            return chamber.role == ChamberTemplateRole::air;
        })};
    require(compiled != generation.scene.compiled_chambers.end(),
        "seed 42 has no compiled Air chamber");
    const auto chamber{std::find_if(
        generation.scene.chambers.begin(), generation.scene.chambers.end(),
        [compiled](const ChamberGeometryContract& candidate) {
            return candidate.node_id == compiled->chamber_id;
        })};
    require(chamber != generation.scene.chambers.end(),
        "seed 42 has no Air chamber geometry");

    std::vector<TemplatePoint2> local_portals;
    for (const PortalContract& portal : generation.scene.portals) {
        if (portal.chamber_id != compiled->chamber_id) {
            continue;
        }
        const TemplatePoint2 relative{
            portal.center_millimetres.x_millimetres
                - chamber->center_millimetres.x_millimetres,
            portal.center_millimetres.z_millimetres
                - chamber->center_millimetres.z_millimetres,
        };
        local_portals.push_back(rotate_template_point(relative,
            static_cast<std::uint8_t>(
                (8U - compiled->orientation_octant) % 8U)));
        require(portal.center_millimetres.y_millimetres
                == chamber->center_millimetres.y_millimetres + 1'350,
            "Air portal center does not preserve the common chamber floor height");
    }
    require(local_portals.size() == 2U,
        "authored Air chamber requires exactly two connected gateways");
    std::sort(local_portals.begin(), local_portals.end(),
        [](const TemplatePoint2 left, const TemplatePoint2 right) {
            return left.z_millimetres < right.z_millimetres;
        });
    require(local_portals[0] == TemplatePoint2{0, -24'750}
            && local_portals[1] == TemplatePoint2{0, 24'750},
        "generated Air routes do not meet the two authored passage thresholds");

    const AuthoredChamberPlacement placement{
        air_chamber_placement(generation.scene)};
    require(placement.chamber_id == compiled->chamber_id,
        "authored Air placement lost chamber ownership");
    require_near(placement.scale.x, 1.0,
        "supplied Air chamber x scale changed");
    require_near(placement.scale.y, 1.0,
        "supplied Air chamber y scale changed");
    require_near(placement.scale.z, 1.0,
        "supplied Air chamber z scale changed");
    require_near(placement.translation_metres.y,
        chamber->center_millimetres.y_millimetres / 1'000.0,
        "authored Air floor does not share the generated chamber elevation");

    const AuthoredChamberCollisionContract collision{
        build_air_chamber_collision(model, placement)};
    require(collision.supports.size() == 11U,
        "Air floor, steps, passages, and threshold seams must be walkable");
    require(collision.blockers.size() == 24U,
        "Air boulders, trunks, and gateway structures must be solid");

    CollisionWorld world{build_collision_world(generation.scene)};
    append_authored_chamber_collision(world, collision);
    append_authored_chamber_collision(world,
        build_fire_chamber_collision(
            fire_model, fire_chamber_placement(generation.scene)));
    append_authored_chamber_collision(world,
        build_earth_chamber_collision(
            earth_model, earth_chamber_placement(generation.scene)));
    std::size_t traversed_routes{};
    for (const RouteCollisionRegion& route : world.routes) {
        if (route.edge.first != compiled->chamber_id
            && route.edge.second != compiled->chamber_id) {
            continue;
        }
        const bool leaving_air_is_reverse{
            route.edge.second == compiled->chamber_id};
        traverse_generated_route(
            generation, world, route, leaving_air_is_reverse, true);
        traverse_generated_route(
            generation, world, route, !leaving_air_is_reverse);
        ++traversed_routes;
    }
    require(traversed_routes == 2U,
        "both authored Air gateways must connect to traversable generated routes");
}

void supplied_fire_room_exposes_two_gateways_and_lava(
    const std::filesystem::path& testdata_directory)
{
    const MaterialModelLoadResult model{load_obj_material_batches(
        testdata_directory / "FireChamber.obj")};
    const auto contains_object = [&](const std::string_view name) {
        return std::any_of(model.objects.begin(), model.objects.end(),
            [name](const MaterialModelObject& object) {
                return object.name == name;
            });
    };
    require(contains_object("EntranceLanding0")
            && contains_object("EntranceLanding1"),
        "supplied Fire chamber must expose both authored entrance landings");
    require(contains_object("Tunnel0_Floor0")
            && contains_object("Tunnel1_Floor0"),
        "supplied Fire chamber must expose both authored tunnel floors");
    require(contains_object("FullFloorLavaLake"),
        "supplied Fire chamber must expose its full lava lake");
    require_near(model.minimum_bounds[0], -28.272963,
        "supplied Fire chamber minimum x changed");
    require_near(model.maximum_bounds[0], 31.647102,
        "supplied Fire chamber maximum x changed");
    require_near(model.minimum_bounds[1], -0.445,
        "supplied Fire chamber minimum y changed");
    require_near(model.maximum_bounds[1], 22.740276,
        "supplied Fire chamber maximum y changed");
    require_near(model.minimum_bounds[2], -28.191006,
        "supplied Fire chamber minimum z changed");
    require_near(model.maximum_bounds[2], 31.595076,
        "supplied Fire chamber maximum z changed");
}

void supplied_fire_room_connects_both_routes_and_respawns_at_entrance(
    const std::filesystem::path& testdata_directory)
{
    const MaterialModelLoadResult model{load_obj_material_batches(
        testdata_directory / "FireChamber.obj")};
    const CaveGenerationResult generation{generate_cave({42U})};
    const AuthoredChamberPlacement placement{
        fire_chamber_placement(generation.scene)};
    require(placement.scale.x == 1.0 && placement.scale.y == 1.0
            && placement.scale.z == 1.0,
        "supplied Fire chamber must remain at authored 1:1 scale");
    const AuthoredChamberCollisionContract collision{
        build_fire_chamber_collision(model, placement)};
    require(collision.respawns.size() == 2U,
        "Fire chamber must provide one safe checkpoint per entrance");
    require(std::any_of(collision.supports.begin(), collision.supports.end(),
                [](const ChamberSupportRegion& support) {
                    return support.support_priority == 760U;
                }),
        "Fire entrance and tunnel floors are not high-priority supports");

    CollisionWorld world{build_collision_world(generation.scene)};
    append_authored_chamber_collision(world, collision);
    const auto compiled{std::find_if(generation.scene.compiled_chambers.begin(),
        generation.scene.compiled_chambers.end(),
        [placement](const CompiledChamberTemplate& chamber) {
            return chamber.chamber_id == placement.chamber_id;
        })};
    require(compiled != generation.scene.compiled_chambers.end()
            && compiled->role == ChamberTemplateRole::fire,
        "seed 42 has no compiled Fire chamber");
    const auto chamber{std::find_if(generation.scene.chambers.begin(),
        generation.scene.chambers.end(),
        [placement](const ChamberGeometryContract& candidate) {
            return candidate.node_id == placement.chamber_id;
        })};
    require(chamber != generation.scene.chambers.end(),
        "seed 42 has no Fire chamber geometry");

    std::vector<TemplatePoint2> local_portals;
    for (const PortalContract& portal : generation.scene.portals) {
        if (portal.chamber_id != placement.chamber_id) {
            continue;
        }
        const TemplatePoint2 relative{
            portal.center_millimetres.x_millimetres
                - chamber->center_millimetres.x_millimetres,
            portal.center_millimetres.z_millimetres
                - chamber->center_millimetres.z_millimetres,
        };
        local_portals.push_back(rotate_template_point(relative,
            static_cast<std::uint8_t>(
                (8U - compiled->orientation_octant) % 8U)));
        require(portal.center_millimetres.y_millimetres
                == chamber->center_millimetres.y_millimetres
                    + 1'350 + authored_fire_landing_height_millimetres,
            "Fire portal floor does not align with authored tunnel floors");
        const double inward_x{
            portal.inward_direction_millimetres.x_millimetres / 1'000.0};
        const double inward_z{
            portal.inward_direction_millimetres.z_millimetres / 1'000.0};
        for (double inset{-0.05}; inset <= 5.0; inset += 0.05) {
            const GeometryVector3 feet{
                portal.center_millimetres.x_millimetres / 1'000.0
                    + inward_x * inset,
                placement.translation_metres.y + 0.85,
                portal.center_millimetres.z_millimetres / 1'000.0
                    + inward_z * inset,
            };
            const CollisionProbe probe{probe_collision_world(
                world, locked_player_capsule(), feet, 0.30)};
            require(probe.supported && !intersects_fall_region(world, feet),
                "Fire tunnel-to-entrance seam contains a support gap");
        }
    }
    std::sort(local_portals.begin(), local_portals.end(),
        [](const TemplatePoint2 left, const TemplatePoint2 right) {
            if (left.x_millimetres != right.x_millimetres) {
                return left.x_millimetres < right.x_millimetres;
            }
            return left.z_millimetres < right.z_millimetres;
        });
    require(local_portals
            == std::vector<TemplatePoint2>{{0, 31'500}, {31'500, 0}},
        "generated Fire routes do not meet both authored gateway thresholds");

    std::size_t traversed_routes{};
    for (const RouteCollisionRegion& route : world.routes) {
        if (route.edge.first != placement.chamber_id
            && route.edge.second != placement.chamber_id) {
            continue;
        }
        const bool leaving_fire_is_reverse{
            route.edge.second == placement.chamber_id};
        traverse_generated_route(
            generation, world, route, leaving_fire_is_reverse);
        traverse_generated_route(
            generation, world, route, !leaving_fire_is_reverse);
        ++traversed_routes;
    }
    require(traversed_routes == 2U,
        "both authored Fire gateways must be traversable in both directions");

    const ChamberCollisionRegion& fire_chamber{
        collision_chamber(world, placement.chamber_id)};
    const GeometryVector3 entrance0{collision.respawns[0].feet_position_metres};
    const GeometryVector3 entrance1{collision.respawns[1].feet_position_metres};
    const GeometryVector3 selected0{
        chamber_respawn_position(world, fire_chamber, entrance0)};
    const GeometryVector3 selected1{
        chamber_respawn_position(world, fire_chamber, entrance1)};
    require_near(selected0.x, entrance0.x,
        "Fire recovery selected the wrong first entrance x");
    require_near(selected0.z, entrance0.z,
        "Fire recovery selected the wrong first entrance z");
    require_near(selected1.x, entrance1.x,
        "Fire recovery selected the wrong second entrance x");
    require_near(selected1.z, entrance1.z,
        "Fire recovery selected the wrong second entrance z");
    const GeometryVector3 lava_local{10.0, 0.0, 8.0};
    const double cosine{std::cos(placement.yaw_radians)};
    const double sine{std::sin(placement.yaw_radians)};
    const GeometryVector3 lava_world{
        placement.translation_metres.x
            + lava_local.x * cosine + lava_local.z * sine,
        placement.translation_metres.y,
        placement.translation_metres.z
            - lava_local.x * sine + lava_local.z * cosine,
    };
    require(intersects_fall_region(world, lava_world),
        "authored Fire lava does not trigger recovery");

    GroundedController controller{world, {entrance0, placement.chamber_id}};
    const double local_dx{-1.0};
    const double local_dz{0.25};
    const double world_dx{local_dx * cosine + local_dz * sine};
    const double world_dz{-local_dx * sine + local_dz * cosine};
    const double yaw_degrees{std::atan2(world_dz, world_dx) * 180.0 / pi};
    bool respawned{};
    for (std::uint32_t tick{}; tick < 120U * 20U && !respawned; ++tick) {
        respawned = controller.advance(
            {1.0, 0.0, yaw_degrees, false, false},
            controller.fixed_step_seconds()).respawned;
    }
    require(respawned,
        "walking from the Fire entrance into lava did not trigger respawn");
    require_near(controller.state().feet_position_metres.x, entrance0.x,
        "Fire lava recovery did not return to the entrance x");
    require_near(controller.state().feet_position_metres.y, entrance0.y,
        "Fire lava recovery did not return to the entrance floor");
    require_near(controller.state().feet_position_metres.z, entrance0.z,
        "Fire lava recovery did not return to the entrance z");
}

}  // namespace

std::vector<TestCase> player_controller_test_cases()
{
    return {
        {"locked controller contract and default state", locked_contract_and_default_state},
        {"walk sprint diagonal and yaw movement", walk_sprint_diagonal_and_yaw},
        {"fixed step is frame-chunk independent", fixed_step_is_frame_chunk_independent},
        {"repeated controller runs are exact", repeated_runs_are_exact},
        {"frame spikes are bounded", frame_spikes_are_bounded},
        {"jump lands without air jump", jump_lands_without_air_jump},
        {"ceiling blocks player capsule", ceiling_blocks_capsule},
        {"slope limit is enforced", slope_limit_is_enforced},
        {"step height is enforced", step_height_is_enforced},
        {"walls slide and prevent tunneling", walls_slide_and_prevent_tunneling},
        {"tunnel seams update checkpoint", tunnel_seams_and_checkpoint_work},
        {"bridge rails block sideways motion", bridge_rails_block_sideways_motion},
        {"fall respawns at safe chamber", fall_respawns_at_safe_chamber},
        {"generated scene produces valid collision", generated_scene_produces_valid_collision},
        {"generated seed-42 tunnels have continuous level support",
            generated_seed_42_tunnels_have_continuous_support},
        {"generated seed-42 Earth-Water tunnel is traversable both directions",
            generated_seed_42_earth_water_tunnel_is_traversable_both_directions},
        {"authored Water objects build solid collision",
            authored_water_objects_build_solid_collision},
        {"supplied Water room matches authored gateways and shrine",
            supplied_water_room_matches_authored_gateways_and_shrine},
        {"supplied Water interior preserves scale and collision",
            supplied_water_interior_preserves_scale_and_collision},
        {"supplied Water entrances are open and stairs are solid",
            supplied_water_entrances_are_open_and_stairs_are_solid},
        {"supplied Water entrance stairs have solid bodies",
            supplied_water_entrance_stairs_have_solid_bodies},
        {"supplied Earth interior preserves scale collision and seams",
            supplied_earth_interior_preserves_scale_collision_and_seams},
        {"supplied Fire room exposes two gateways and lava",
            supplied_fire_room_exposes_two_gateways_and_lava},
        {"supplied Fire room connects routes and respawns at entrance",
            supplied_fire_room_connects_both_routes_and_respawns_at_entrance},
        {"seed-42 Air portals match authored gateway thresholds",
            seed_42_air_portals_match_authored_gateway_thresholds},
        {"invalid controller data is rejected", invalid_data_is_rejected},
    };
}

}  // namespace crystalbound::test
