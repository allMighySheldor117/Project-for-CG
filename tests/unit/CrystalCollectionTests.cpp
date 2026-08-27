#include "CrystalCollectionTests.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "crystalbound/CaveScene.hpp"
#include "crystalbound/CrystalCollection.hpp"
#include "crystalbound/PlayerController.hpp"

namespace crystalbound::test {
namespace {

constexpr double pi{3.14159265358979323846};

class CollectionTestFailure final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

void require(const bool condition, const std::string_view message)
{
    if (!condition) {
        throw CollectionTestFailure{std::string{message}};
    }
}

[[nodiscard]] CrystalInteractionTarget target(
    const Element element,
    const std::uint64_t id,
    const GeometryVector3 position)
{
    return {element, id, position};
}

[[nodiscard]] CameraInteractionQuery forward_query()
{
    return {{0.0, 0.0, 0.0}, {0.0, 0.0, -1.0}};
}

[[nodiscard]] VisibilityTriangle blocker_at(const double z)
{
    return {1U, {-1.0, -1.0, z}, {1.0, -1.0, z}, {0.0, 1.0, z}};
}

[[nodiscard]] CollectionAttemptResult collect(
    const CrystalInteractionTarget& crystal,
    CrystalCollectionState& state)
{
    return attempt_crystal_collection(
        {crystal},
        forward_query(),
        {},
        true,
        state);
}

void collection_model_is_fixed_and_ordered(const std::filesystem::path&)
{
    CrystalCollectionState state;
    require(state.collected_count() == 0U && !state.all_collected(),
        "new collection state is not empty");
    require(state.collected_elements().empty(), "empty state reports elements");
    for (const Element element : elemental_order) {
        require(!state.is_collected(element), "new state contains an element");
        require(!state.socket_display_state().displays(element),
            "empty state exposes a socket crystal");
    }
    for (const Element element : elemental_order) {
        require(state.collect(element), "element did not collect");
        require(!state.collect(element), "element collected twice");
    }
    require(state.all_collected() && state.collected_count() == 5U,
        "five-element state did not complete");
    require(state.collected_elements()
            == std::vector<Element>{elemental_order.begin(), elemental_order.end()},
        "collected elements are not in elemental order");
    for (const Element element : elemental_order) {
        require(state.socket_display_state().displays(element),
            "collection is not exposed to its future socket");
    }
}

void every_collection_order_succeeds(const std::filesystem::path&)
{
    std::array<Element, 5> order{elemental_order};
    std::size_t permutation_count{};
    do {
        CrystalCollectionState state;
        for (std::size_t index{}; index < order.size(); ++index) {
            const auto result{collect(target(
                order[index], index + 10U, {0.0, 0.0, -1.0}), state)};
            require(result.collected && result.element == order[index],
                "valid collection permutation failed");
        }
        require(state.all_collected(), "collection permutation was incomplete");
        ++permutation_count;
    } while (std::next_permutation(order.begin(), order.end(),
        [](const Element left, const Element right) {
            return static_cast<std::uint8_t>(left)
                < static_cast<std::uint8_t>(right);
        }));
    require(permutation_count == 120U, "not all collection orders were tested");
}

void one_time_collection_and_edge_are_enforced(const std::filesystem::path&)
{
    RisingEdgeButton button;
    require(!button.update(false), "released E emitted an edge");
    require(button.update(true), "first E press did not emit an edge");
    require(!button.update(true) && !button.update(true), "held E repeated");
    require(!button.update(false) && button.update(true),
        "release and re-press did not create an edge");
    button.reset(true);
    require(!button.update(true),
        "held E repeated after a game-state transition");
    require(!button.update(false) && button.update(true),
        "transition-synchronized E did not recover after release");

    CrystalCollectionState state;
    const auto crystal{target(Element::fire, 7U, {0.0, 0.0, -1.0})};
    const auto no_edge{attempt_crystal_collection(
        {crystal}, forward_query(), {}, false, state)};
    require(!no_edge.collected
            && no_edge.rejection == InteractionRejectionReason::no_press_edge,
        "collection occurred without an E edge");
    require(collect(crystal, state).collected, "first collection failed");
    const auto repeat{collect(crystal, state)};
    require(!repeat.collected
            && repeat.rejection == InteractionRejectionReason::already_collected
            && state.collected_count() == 1U,
        "already collected crystal changed state");
}

void range_and_focus_boundaries_are_locked(const std::filesystem::path&)
{
    const CrystalCollectionState state;
    require(focus_crystal(
                {target(Element::fire, 1U,
                    {0.0, 0.0, -maximum_crystal_interaction_range_metres})},
                forward_query(), {}, state).focused.has_value(),
        "exact 2.2 metre range was rejected");
    const auto outside_range{focus_crystal(
        {target(Element::fire, 1U,
            {0.0, 0.0, -maximum_crystal_interaction_range_metres - 1.0e-5})},
        forward_query(), {}, state)};
    require(!outside_range.focused
            && outside_range.rejection == InteractionRejectionReason::out_of_range,
        "target beyond 2.2 metres was accepted");

    const auto position_at = [](const double degrees) {
        const double angle{degrees * pi / 180.0};
        return GeometryVector3{
            std::sin(angle) * 2.0, 0.0, -std::cos(angle) * 2.0};
    };
    require(focus_crystal(
                {target(Element::air, 2U,
                    position_at(maximum_crystal_focus_angle_degrees))},
                forward_query(), {}, state).focused.has_value(),
        "exact 12 degree focus boundary was rejected");
    const auto outside_focus{focus_crystal(
        {target(Element::air, 2U,
            position_at(maximum_crystal_focus_angle_degrees + 0.001))},
        forward_query(), {}, state)};
    require(!outside_focus.focused
            && outside_focus.rejection == InteractionRejectionReason::outside_focus,
        "target beyond 12 degrees was accepted");
}

void target_selection_uses_angle_distance_and_id(const std::filesystem::path&)
{
    const CrystalCollectionState state;
    const auto position_at = [](const double degrees, const double distance) {
        const double angle{degrees * pi / 180.0};
        return GeometryVector3{
            std::sin(angle) * distance, 0.0, -std::cos(angle) * distance};
    };
    const auto angle_result{focus_crystal({
            target(Element::fire, 9U, position_at(5.0, 1.0)),
            target(Element::water, 8U, position_at(2.0, 2.0))},
        forward_query(), {}, state)};
    require(angle_result.focused->target.element == Element::water,
        "smallest angle did not win");
    const auto distance_result{focus_crystal({
            target(Element::earth, 7U, {0.0, 0.0, -2.0}),
            target(Element::air, 6U, {0.0, 0.0, -1.0})},
        forward_query(), {}, state)};
    require(distance_result.focused->target.element == Element::air,
        "distance tie-break changed");
    const auto id_result{focus_crystal({
            target(Element::aether, 12U, {0.0, 0.0, -1.0}),
            target(Element::fire, 11U, {0.0, 0.0, -1.0})},
        forward_query(), {}, state)};
    require(id_result.focused->target.stable_object_id == 11U,
        "stable object ID tie-break changed");
}

void line_of_sight_handles_occlusion_and_boundaries(const std::filesystem::path&)
{
    const CrystalCollectionState state;
    const auto crystal{target(Element::water, 2U, {0.0, 0.0, -2.0})};
    const auto blocked{focus_crystal(
        {crystal}, forward_query(), {{blocker_at(-1.0)}}, state)};
    require(!blocked.focused
            && blocked.rejection == InteractionRejectionReason::occluded,
        "closer structural blocker was ignored");
    require(focus_crystal(
                {crystal}, forward_query(), {{blocker_at(-3.0)}}, state)
                .focused.has_value(),
        "blocker behind target rejected collection");
    require(focus_crystal({crystal}, forward_query(), {}, state).focused.has_value(),
        "unobstructed target was rejected");
    require(focus_crystal(
                {crystal}, forward_query(), {{blocker_at(-2.0)}}, state)
                .focused.has_value(),
        "boundary contact at target distance counted as closer");
    const VisibilityWorld parallel{{{
        4U, {1.0, -1.0, 0.0}, {1.0, 1.0, 0.0}, {1.0, 0.0, -3.0}}}};
    require(focus_crystal({crystal}, forward_query(), parallel, state)
                .focused.has_value(),
        "parallel ray was treated as blocked");
    require(focus_crystal(
                {crystal}, {{0.0, 0.0, -0.25}, {0.0, 0.0, -1.0}},
                {}, state).focused.has_value(),
        "ray starting inside valid chamber space was rejected");
    VisibilityWorld bounded_space;
    bounded_space.chambers.push_back({
        9U, {0.0, 0.0, 0.0}, -1.0, 1.0, 0.5});
    const auto through_structural_wall{focus_crystal(
        {target(Element::water, 2U, {0.0, 0.0, -1.0})},
        forward_query(), bounded_space, state)};
    require(!through_structural_wall.focused
            && through_structural_wall.rejection
                == InteractionRejectionReason::occluded,
        "ray leaving structural free space was not blocked");
}

void invalid_camera_queries_are_rejected(const std::filesystem::path&)
{
    const CrystalCollectionState state;
    const auto crystal{target(Element::aether, 5U, {0.0, 0.0, -1.0})};
    const std::array<CameraInteractionQuery, 3> invalid{{
        {{0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}},
        {{std::numeric_limits<double>::infinity(), 0.0, 0.0},
            {0.0, 0.0, -1.0}},
        {{0.0, 0.0, 0.0},
            {0.0, std::numeric_limits<double>::quiet_NaN(), -1.0}},
    }};
    for (const auto& query : invalid) {
        const auto result{focus_crystal({crystal}, query, {}, state)};
        require(!result.focused
                && result.rejection == InteractionRejectionReason::invalid_query,
            "invalid camera query was accepted");
    }
}

void rendering_state_hides_only_collected_crystal(const std::filesystem::path&)
{
    const CaveGenerationResult generation{generate_cave({42U})};
    const ElementalChamberVisual& chamber{
        generation.scene.elemental_visuals.chambers.front()};
    CrystalCollectionState state;
    require(state.collect(chamber.element), "test collection failed");
    require(!is_elemental_piece_visible(chamber.crystal, state),
        "collected crystal remains visible");
    require(is_elemental_piece_visible(chamber.pedestal, state),
        "collection hid its pedestal");
    for (const auto& decoration : chamber.decorations) {
        require(is_elemental_piece_visible(decoration, state),
            "collection hid chamber decorations");
    }
    const auto lights{active_crystal_lights(
        generation.scene.elemental_visuals, state, chamber.chamber_id, 1.0F)};
    require(lights.size() == 4U, "collected crystal light was not removed");
    require(std::none_of(lights.begin(), lights.end(), [&](const auto& candidate) {
        return candidate.light.stable_object_id
            == chamber.crystal.stable_object_id;
    }), "collected crystal light remains active");
}

void generated_visibility_does_not_fill_chambers(const std::filesystem::path&)
{
    const CaveGenerationResult generation{generate_cave({42U})};
    const auto visibility{build_crystal_visibility_world(generation.scene)};
    const auto targets{
        build_crystal_interaction_targets(generation.scene.elemental_visuals)};
    const CrystalCollectionState state;
    require(!visibility.triangles.empty(), "visibility world is empty");
    require(targets.size() == 5U, "interaction target count changed");
    for (const auto& crystal : targets) {
        const CameraInteractionQuery query{
            {crystal.position_metres.x, crystal.position_metres.y,
                crystal.position_metres.z + 1.5},
            {0.0, 0.0, -1.0}};
        require(focus_crystal({crystal}, query, visibility, state)
                    .focused.has_value(),
            "broad chamber bounds blocked a valid interior ray");
    }
}

void collection_survives_controller_respawn(const std::filesystem::path&)
{
    CollisionWorld world;
    world.chambers = {
        {{0U}, 0U, {0.0, 0.0, 0.0}, 0.0, 5.0, 4.5},
        {{1U}, 1U, {14.0, 0.0, 0.0}, 0.0, 5.0, 4.5},
    };
    RouteCollisionRegion tunnel;
    tunnel.edge = make_edge({0U}, {1U});
    tunnel.stable_object_id = 10U;
    tunnel.kind = GroundContactKind::tunnel;
    tunnel.samples = {
        {{4.0, 1.35, 0.0}, {1.0, 0.0, 0.0}, 0.0},
        {{10.0, 1.35, 0.0}, {1.0, 0.0, 0.0}, 6.0}};
    tunnel.usable_half_width_metres = 1.0;
    tunnel.tunnel_radius_metres = 1.35;
    world.routes.push_back(tunnel);
    RouteCollisionRegion bridge{tunnel};
    bridge.edge = make_edge({2U}, {3U});
    bridge.stable_object_id = 700U;
    bridge.kind = GroundContactKind::bridge;
    bridge.samples = {
        {{70.0, 1.35, 70.0}, {1.0, 0.0, 0.0}, 0.0},
        {{75.0, 1.35, 70.0}, {1.0, 0.0, 0.0}, 5.0}};
    bridge.usable_half_width_metres = 0.5;
    bridge.rail_height_metres = 0.85;
    world.routes.push_back(bridge);
    world.fall_regions.push_back({
        900U, {{12.5, -0.1, -1.0}, {13.5, 2.0, 1.0}}});
    world.kill_plane_metres = -20.0;
    GroundedController controller{world, {{0.0, 0.0, 0.0}, {0U}}};
    CrystalCollectionState collection;
    require(collection.collect(Element::earth), "test collection failed");
    bool respawned{};
    for (std::size_t frame{}; frame < 600U && !respawned; ++frame) {
        respawned = controller.advance(
            {1.0, 0.0, 0.0, false, false}, 1.0 / 60.0).respawned;
    }
    require(respawned, "controller did not produce a respawn");
    require(collection.is_collected(Element::earth)
            && collection.collected_count() == 1U,
        "respawn cleared collection");
}

void structural_and_seed_contracts_are_preserved(const std::filesystem::path&)
{
    const CaveGenerationResult first{generate_cave({42U})};
    const CaveGenerationResult repeat{generate_cave({42U})};
    require(first.scene.fingerprint == 0x9fb15c446b74730dULL,
        "structural cave fingerprint changed");
    require(first.scene.fingerprint == repeat.scene.fingerprint
            && first.generation.fingerprint == repeat.generation.fingerprint,
        "accepted seed determinism changed");
    require(first.reachability.accepted
            && validate_collision_world(build_collision_world(first.scene)).empty(),
        "collision or reachability contract changed");
    const CaveGenerationResult fallback_first{generate_cave({123'456'789U})};
    const CaveGenerationResult fallback_repeat{generate_cave({123'456'789U})};
    require(fallback_first.generation.used_fallback
            == fallback_repeat.generation.used_fallback
            && fallback_first.scene.fingerprint
                == fallback_repeat.scene.fingerprint,
        "fallback determinism changed");
}

}  // namespace

std::vector<TestCase> crystal_collection_test_cases()
{
    return {
        {"crystal collection model is fixed and ordered", collection_model_is_fixed_and_ordered},
        {"every crystal collection order succeeds", every_collection_order_succeeds},
        {"one-time collection and E edge are enforced", one_time_collection_and_edge_are_enforced},
        {"crystal range and focus boundaries are locked", range_and_focus_boundaries_are_locked},
        {"crystal selection uses angle distance and ID", target_selection_uses_angle_distance_and_id},
        {"line of sight handles occlusion and boundaries", line_of_sight_handles_occlusion_and_boundaries},
        {"invalid camera queries are rejected", invalid_camera_queries_are_rejected},
        {"rendering hides only collected crystal", rendering_state_hides_only_collected_crystal},
        {"generated visibility does not fill chambers", generated_visibility_does_not_fill_chambers},
        {"collection survives controller respawn", collection_survives_controller_respawn},
        {"structural and seed contracts are preserved", structural_and_seed_contracts_are_preserved},
    };
}

}  // namespace crystalbound::test
