#include "GameLoopTests.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

#include "crystalbound/CrystalCollection.hpp"
#include "crystalbound/ExitArch.hpp"
#include "crystalbound/GameLoop.hpp"

namespace crystalbound::test {
namespace {

class TestFailure final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

void require(const bool condition, const std::string_view message)
{
    if (!condition) {
        throw TestFailure{std::string{message}};
    }
}

void require_near(
    const double actual,
    const double expected,
    const std::string_view message,
    const double tolerance = 1.0e-9)
{
    if (!std::isfinite(actual) || std::abs(actual - expected) > tolerance) {
        throw TestFailure{std::string{message} + ": expected "
            + std::to_string(expected) + ", got " + std::to_string(actual)};
    }
}

[[nodiscard]] GameTimePoint at_seconds(const double seconds)
{
    return GameTimePoint{std::chrono::duration_cast<GameClock::duration>(
        std::chrono::duration<double>{seconds})};
}

[[nodiscard]] SessionBestKey best_key(
    const std::uint32_t version,
    const std::uint64_t seed) noexcept
{
    return {{version}, {seed}};
}

[[nodiscard]] CameraInteractionQuery query_toward(
    const GeometryVector3 target,
    const double distance,
    const double angle_degrees = 0.0)
{
    constexpr double pi{3.14159265358979323846};
    const double angle{angle_degrees * pi / 180.0};
    return {{target.x, target.y, target.z + distance},
        {std::sin(angle), 0.0, -std::cos(angle)}};
}

[[nodiscard]] VisibilityTriangle blocker_between(
    const GeometryVector3 target,
    const double camera_z_offset)
{
    const double z{target.z + camera_z_offset};
    return {900U,
        {target.x - 1.0, target.y - 1.0, z},
        {target.x + 1.0, target.y - 1.0, z},
        {target.x, target.y + 1.0, z}};
}

void initial_state_and_start_transition_are_locked(const std::filesystem::path&)
{
    GameSession session;
    require(session.state() == GameState::start, "session did not start at Start");
    require_near(session.elapsed_seconds(at_seconds(50.0)), 0.0,
        "Start advanced the timer");
    const GameTransition transition{session.begin_exploration(at_seconds(50.0))};
    require(transition.accepted && transition.from == GameState::start
            && transition.to == GameState::playing,
        "Begin Exploration did not enter Playing");
    require(transition.cursor == CursorRequest::capture
            && transition.clear_held_input
            && transition.discard_first_mouse_delta,
        "Begin Exploration did not establish input ownership");
    require_near(session.elapsed_seconds(at_seconds(52.5)), 2.5,
        "Playing timer did not start at Begin Exploration");
}

void pause_resume_and_focus_loss_exclude_hidden_time(const std::filesystem::path&)
{
    GameSession session;
    require(session.begin_exploration(at_seconds(10.0)).accepted,
        "test session did not begin");
    const GameTransition paused{session.pause(at_seconds(14.0))};
    require(paused.accepted && paused.to == GameState::paused
            && paused.cursor == CursorRequest::release
            && paused.clear_held_input,
        "Pause did not release input ownership");
    require_near(session.elapsed_seconds(at_seconds(114.0)), 4.0,
        "Paused time was counted");
    const GameTransition resumed{session.resume(at_seconds(200.0))};
    require(resumed.accepted && resumed.cursor == CursorRequest::capture
            && resumed.discard_first_mouse_delta,
        "Resume did not restore mouse ownership safely");
    require_near(session.elapsed_seconds(at_seconds(203.0)), 7.0,
        "Resume did not continue accumulated time");
    require(session.pause(at_seconds(205.0)).accepted,
        "focus-loss pause was rejected");
    require_near(session.elapsed_seconds(at_seconds(500.0)), 9.0,
        "focus-loss hidden time was counted");
}

void invalid_transitions_do_not_mutate_state_or_time(const std::filesystem::path&)
{
    GameSession session;
    require(!session.pause(at_seconds(1.0)).accepted,
        "Start incorrectly accepted Pause");
    require(!session.resume(at_seconds(2.0)).accepted,
        "Start incorrectly accepted Resume");
    require(!session.complete(at_seconds(3.0), best_key(1U, 42U)).accepted,
        "Start incorrectly accepted completion");
    require(session.state() == GameState::start,
        "invalid transition changed state");
    require_near(session.elapsed_seconds(at_seconds(100.0)), 0.0,
        "invalid transition changed the timer");
}

void completion_freezes_once_and_records_best(const std::filesystem::path&)
{
    GameSession session;
    const SessionBestKey key{best_key(1U, 42U)};
    require(session.begin_exploration(at_seconds(1.0)).accepted,
        "test session did not begin");
    const GameTransition completed{session.complete(at_seconds(7.25), key)};
    require(completed.accepted && completed.to == GameState::completed
            && completed.cursor == CursorRequest::release,
        "valid completion did not enter Completed");
    require_near(session.elapsed_seconds(at_seconds(100.0)), 6.25,
        "Completed timer did not freeze");
    require(!session.complete(at_seconds(200.0), key).accepted,
        "completion was accepted twice");
    require_near(*session.best_seconds(key), 6.25,
        "completion did not record the session best");
}

void session_best_is_keyed_and_only_improves(const std::filesystem::path&)
{
    GameSession session;
    const SessionBestKey first{best_key(1U, 42U)};
    const SessionBestKey second_seed{best_key(1U, 43U)};
    const SessionBestKey second_version{best_key(2U, 42U)};
    require(session.begin_exploration(at_seconds(0.0)).accepted,
        "first run did not begin");
    require(session.complete(at_seconds(8.0), first).accepted,
        "first run did not complete");
    require(session.restart_seed().accepted, "restart was rejected");
    require(session.begin_exploration(at_seconds(20.0)).accepted,
        "slower run did not begin");
    require(session.complete(at_seconds(30.0), first).accepted,
        "slower run did not complete");
    require_near(*session.best_seconds(first), 8.0,
        "slower run replaced the best");
    require(!session.best_seconds(second_seed).has_value(),
        "best leaked to another seed");
    require(!session.best_seconds(second_version).has_value(),
        "best leaked to another generator version");
}

void reset_transitions_return_to_start_and_clear_time(const std::filesystem::path&)
{
    GameSession paused;
    require(paused.begin_exploration(at_seconds(1.0)).accepted,
        "paused reset session did not begin");
    require(paused.pause(at_seconds(4.0)).accepted,
        "paused reset session did not pause");
    require(paused.restart_seed().accepted,
        "Restart Seed from Paused was rejected");
    require(paused.state() == GameState::start
            && paused.elapsed_seconds(at_seconds(10.0)) == 0.0,
        "Restart Seed did not reset run state");

    GameSession new_cave;
    require(new_cave.begin_exploration(at_seconds(1.0)).accepted,
        "New Cave session did not begin");
    require(new_cave.pause(at_seconds(2.0)).accepted,
        "New Cave session did not pause");
    require(new_cave.new_cave().accepted,
        "New Cave from Paused was rejected");

    GameSession play_again;
    require(play_again.begin_exploration(at_seconds(1.0)).accepted,
        "Play Again session did not begin");
    require(play_again.complete(at_seconds(2.0), best_key(1U, 99U)).accepted,
        "Play Again session did not complete");
    require(play_again.play_again().accepted,
        "Play Again from Completed was rejected");
    require(play_again.state() == GameState::start,
        "Play Again did not return to Start");
}

void reset_transition_sources_are_restricted(const std::filesystem::path&)
{
    GameSession session;
    require(!session.restart_seed().accepted,
        "Restart Seed was accepted from Start");
    require(!session.new_cave().accepted,
        "New Cave was accepted from Start");
    require(!session.play_again().accepted,
        "Play Again was accepted from Start");
    require(session.begin_exploration(at_seconds(0.0)).accepted,
        "test session did not begin");
    require(!session.restart_seed().accepted,
        "Restart Seed was accepted from Playing");
    require(!session.new_cave().accepted,
        "New Cave was accepted from Playing");
    require(!session.play_again().accepted,
        "Play Again was accepted from Playing");
}

void ui_contract_exposes_only_approved_fields(const std::filesystem::path&)
{
    const GameUiContract start{game_ui_contract(GameState::start)};
    require(start.has_field(GameUiField::premise)
            && start.has_field(GameUiField::objective)
            && start.has_field(GameUiField::controls)
            && start.has_field(GameUiField::seed)
            && start.has_field(GameUiField::crystal_progress)
            && start.has_action(GameUiAction::begin_exploration),
        "Start UI contract is incomplete");

    const GameUiContract playing{game_ui_contract(GameState::playing)};
    require(playing.has_field(GameUiField::elapsed_time)
            && playing.has_field(GameUiField::crystal_progress)
            && playing.has_field(GameUiField::interaction_prompt)
            && !playing.has_field(GameUiField::seed),
        "normal HUD fields violate the contract");

    const GameUiContract paused{game_ui_contract(GameState::paused)};
    require(paused.has_field(GameUiField::seed)
            && paused.has_action(GameUiAction::resume)
            && paused.has_action(GameUiAction::restart_seed)
            && paused.has_action(GameUiAction::new_cave)
            && paused.has_action(GameUiAction::quit),
        "Pause UI contract is incomplete");

    const GameUiContract completed{game_ui_contract(GameState::completed)};
    require(completed.has_field(GameUiField::session_best)
            && completed.has_action(GameUiAction::play_again)
            && completed.has_action(GameUiAction::restart_seed)
            && completed.has_action(GameUiAction::quit),
        "Completed UI contract is incomplete");
}

void seed_labels_distinguish_requested_and_fallback(const std::filesystem::path&)
{
    GenerationResult normal;
    normal.requested_seed = {42U};
    normal.effective_seed = {42U};
    require(format_run_seed_label(normal) == "42",
        "normal requested seed label changed");
    normal.used_fallback = true;
    normal.effective_seed = {123U};
    require(format_run_seed_label(normal) == "requested 42; fallback 123",
        "fallback seed label does not expose both seeds");
}

void new_cave_seed_source_is_injected_and_changes(const std::filesystem::path&)
{
    std::size_t calls{};
    const Seed selected{choose_new_requested_seed({42U}, [&calls] {
        ++calls;
        return 99U;
    })};
    require(selected == Seed{99U} && calls == 1U,
        "New Cave did not use the injected seed source exactly once");
    const Seed repeated{choose_new_requested_seed({42U}, [] { return 42U; })};
    require(repeated != Seed{42U},
        "New Cave retained the current requested seed");
}

void exit_arch_is_deterministic_and_element_ordered(const std::filesystem::path&)
{
    const CaveGenerationResult generation{generate_cave({42U})};
    const ExitArchData first{build_exit_arch(generation)};
    const ExitArchData repeat{build_exit_arch(generation)};
    require(validate_exit_arch(generation, first).empty(),
        "generated exit arch is invalid");
    require(first.chamber_id == repeat.chamber_id
            && first.stable_object_id == repeat.stable_object_id
            && first.fingerprint == repeat.fingerprint,
        "exit arch generation is not deterministic");
    require(!first.stone_mesh.vertices.empty()
            && !first.portal_mesh.vertices.empty(),
        "exit arch did not generate visible geometry");
    require(first.sockets.size() == elemental_order.size(),
        "exit arch socket count changed");
    for (std::size_t index{}; index < elemental_order.size(); ++index) {
        require(first.sockets[index].element == elemental_order[index],
            "exit socket order changed");
        require(!first.sockets[index].crystal_mesh.vertices.empty(),
            "socket crystal mesh is empty");
    }
    const auto exit_node{std::find_if(
        generation.generation.topology.nodes.begin(),
        generation.generation.topology.nodes.end(),
        [](const ChamberNode& node) { return node.role == ChamberRole::exit; })};
    require(exit_node != generation.generation.topology.nodes.end()
            && first.chamber_id == exit_node->id,
        "arch was not placed in the Exit chamber");
}

void socket_display_tracks_any_collection_order(const std::filesystem::path&)
{
    const ExitArchData arch{build_exit_arch(generate_cave({42U}))};
    std::array<Element, 5> order{elemental_order};
    std::size_t permutation_count{};
    do {
        CrystalCollectionState collection;
        ExitArchDisplayState display{exit_arch_display_state(arch, collection)};
        require(!display.active, "empty arch was active");
        for (std::size_t index{}; index < order.size(); ++index) {
            require(collection.collect(order[index]), "test collection failed");
            display = exit_arch_display_state(arch, collection);
            require(display.filled.displays(order[index]),
                "collected crystal did not fill its matching socket");
            require(display.active == (index + 1U == order.size()),
                "arch activation did not match fifth collection");
        }
        ++permutation_count;
    } while (std::next_permutation(order.begin(), order.end(),
        [](const Element left, const Element right) {
            return static_cast<std::uint8_t>(left)
                < static_cast<std::uint8_t>(right);
        }));
    require(permutation_count == 120U,
        "not all socket collection orders were tested");
}

void exit_requires_playing_collection_focus_los_and_edge(const std::filesystem::path&)
{
    const ExitArchData arch{build_exit_arch(generate_cave({42U}))};
    const CameraInteractionQuery query{
        query_toward(arch.interaction_position_metres, 1.5)};
    CrystalCollectionState collection;
    require(attempt_exit_arch(
                arch, query, {}, false, true, collection)
                .rejection == ExitRejectionReason::not_playing,
        "exit accepted outside Playing");
    require(attempt_exit_arch(
                arch, query, {}, true, true, collection)
                .rejection == ExitRejectionReason::crystals_missing,
        "locked exit accepted incomplete collection");
    for (const Element element : elemental_order) {
        require(collection.collect(element), "test collection failed");
    }
    require(attempt_exit_arch(
                arch, query, {}, true, false, collection)
                .rejection == ExitRejectionReason::no_press_edge,
        "exit completed without an E edge");
    require(attempt_exit_arch(
                arch, query, {}, true, true, collection).completed,
        "valid arch interaction did not complete");
    VisibilityWorld blocked;
    blocked.triangles.push_back(
        blocker_between(arch.interaction_position_metres, 0.75));
    require(attempt_exit_arch(
                arch, query, blocked, true, true, collection)
                .rejection == ExitRejectionReason::occluded,
        "structural wall did not block exit interaction");
}

void exit_range_and_focus_boundaries_are_locked(const std::filesystem::path&)
{
    const ExitArchData arch{build_exit_arch(generate_cave({42U}))};
    CrystalCollectionState collection;
    for (const Element element : elemental_order) {
        require(collection.collect(element), "test collection failed");
    }
    require(attempt_exit_arch(arch,
                query_toward(arch.interaction_position_metres,
                    exit_arch_interaction_focus_limits.maximum_range_metres),
                {}, true, true, collection).completed,
        "exact exit range boundary was rejected");
    require(attempt_exit_arch(arch,
                query_toward(arch.interaction_position_metres,
                    exit_arch_interaction_focus_limits.maximum_range_metres
                        + 1.0e-5),
                {}, true, true, collection)
                .rejection == ExitRejectionReason::out_of_range,
        "exit beyond range boundary was accepted");
    require(attempt_exit_arch(arch,
                query_toward(arch.interaction_position_metres, 2.0,
                    exit_arch_interaction_focus_limits.maximum_angle_degrees),
                {}, true, true, collection).completed,
        "exact exit focus boundary was rejected");
    require(attempt_exit_arch(arch,
                query_toward(arch.interaction_position_metres, 2.0,
                    exit_arch_interaction_focus_limits.maximum_angle_degrees
                        + 0.001),
                {}, true, true, collection)
                .rejection == ExitRejectionReason::outside_focus,
        "exit beyond focus boundary was accepted");
}

void expanded_exit_interaction_is_player_friendly(const std::filesystem::path&)
{
    const ExitArchData arch{build_exit_arch(generate_cave({42U}))};
    CrystalCollectionState collection;
    for (const Element element : elemental_order) {
        require(collection.collect(element), "test collection failed");
    }
    require(attempt_exit_arch(arch,
                query_toward(arch.interaction_position_metres, 3.0),
                {}, true, true, collection).completed,
        "final portal cannot be activated from three metres away");
    require(attempt_exit_arch(arch,
                query_toward(arch.interaction_position_metres, 2.0, 24.0),
                {}, true, true, collection).completed,
        "final portal interaction still requires overly precise aiming");
}

void invalid_exit_query_is_rejected(const std::filesystem::path&)
{
    const ExitArchData arch{build_exit_arch(generate_cave({42U}))};
    CrystalCollectionState collection;
    for (const Element element : elemental_order) {
        require(collection.collect(element), "test collection failed");
    }
    const CameraInteractionQuery invalid{
        arch.interaction_position_metres,
        {0.0, std::numeric_limits<double>::quiet_NaN(), 0.0}};
    require(attempt_exit_arch(arch, invalid, {}, true, true, collection)
                .rejection == ExitRejectionReason::invalid_query,
        "invalid exit query was accepted");
}

void step_nine_preserves_structural_contracts(const std::filesystem::path&)
{
    const CaveGenerationResult first{generate_cave({42U})};
    static_cast<void>(build_exit_arch(first));
    const CaveGenerationResult repeat{generate_cave({42U})};
    require(first.scene.fingerprint == 0x52CCEB23A788803DULL
            && first.scene.fingerprint == repeat.scene.fingerprint,
        "exit arch changed the structural cave fingerprint");
    require(first.generation.fingerprint == repeat.generation.fingerprint
            && first.reachability == repeat.reachability,
        "exit arch changed topology or reachability determinism");
}

}  // namespace

std::vector<TestCase> game_loop_test_cases()
{
    return {
        {"game starts only after Begin Exploration", initial_state_and_start_transition_are_locked},
        {"pause and focus loss exclude hidden time", pause_resume_and_focus_loss_exclude_hidden_time},
        {"invalid game transitions are inert", invalid_transitions_do_not_mutate_state_or_time},
        {"completion freezes once and records best", completion_freezes_once_and_records_best},
        {"session bests are keyed and only improve", session_best_is_keyed_and_only_improves},
        {"run resets return to Start", reset_transitions_return_to_start_and_clear_time},
        {"reset transition sources are restricted", reset_transition_sources_are_restricted},
        {"UI fields and actions are state-specific", ui_contract_exposes_only_approved_fields},
        {"seed labels distinguish fallback", seed_labels_distinguish_requested_and_fallback},
        {"New Cave uses an injected seed source", new_cave_seed_source_is_injected_and_changes},
        {"exit arch is deterministic and ordered", exit_arch_is_deterministic_and_element_ordered},
        {"socket display supports every collection order", socket_display_tracks_any_collection_order},
        {"exit gating requires the full interaction contract", exit_requires_playing_collection_focus_los_and_edge},
        {"exit range and focus boundaries are locked", exit_range_and_focus_boundaries_are_locked},
        {"expanded exit interaction is player friendly",
            expanded_exit_interaction_is_player_friendly},
        {"invalid exit query is rejected", invalid_exit_query_is_rejected},
        {"Step 9 preserves structural contracts", step_nine_preserves_structural_contracts},
    };
}

}  // namespace crystalbound::test
