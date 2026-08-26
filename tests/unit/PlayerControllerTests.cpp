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
        {"invalid controller data is rejected", invalid_data_is_rejected},
    };
}

}  // namespace crystalbound::test
