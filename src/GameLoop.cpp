#include "crystalbound/GameLoop.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <tuple>

namespace crystalbound {

bool operator<(
    const SessionBestKey& left,
    const SessionBestKey& right) noexcept
{
    return std::tie(left.generator_version.value, left.effective_seed.value)
        < std::tie(right.generator_version.value, right.effective_seed.value);
}

bool GameUiContract::has_field(const GameUiField field) const noexcept
{
    return std::find(fields.begin(), fields.end(), field) != fields.end();
}

bool GameUiContract::has_action(const GameUiAction action) const noexcept
{
    return std::find(actions.begin(), actions.end(), action) != actions.end();
}

GameUiContract game_ui_contract(const GameState state)
{
    switch (state) {
    case GameState::start:
        return {{GameUiField::premise, GameUiField::objective,
                    GameUiField::controls, GameUiField::seed,
                    GameUiField::crystal_progress},
            {GameUiAction::begin_exploration}};
    case GameState::playing:
        return {{GameUiField::elapsed_time, GameUiField::crystal_progress,
                    GameUiField::interaction_prompt},
            {}};
    case GameState::paused:
        return {{GameUiField::elapsed_time, GameUiField::seed,
                    GameUiField::crystal_progress},
            {GameUiAction::resume, GameUiAction::restart_seed,
                GameUiAction::new_cave, GameUiAction::quit}};
    case GameState::completed:
        return {{GameUiField::elapsed_time, GameUiField::seed,
                    GameUiField::crystal_progress, GameUiField::session_best},
            {GameUiAction::play_again, GameUiAction::restart_seed,
                GameUiAction::quit}};
    }
    return {};
}

std::string format_run_seed_label(const GenerationResult& generation)
{
    if (generation.used_fallback) {
        return "requested " + std::to_string(generation.requested_seed.value)
            + "; fallback " + std::to_string(generation.effective_seed.value);
    }
    return std::to_string(generation.requested_seed.value);
}

Seed choose_new_requested_seed(
    const Seed current,
    const std::function<std::uint64_t()>& source)
{
    if (!source) {
        throw std::invalid_argument("New Cave requires a requested-seed source.");
    }
    Seed requested{source()};
    if (requested == current) {
        requested.value ^= 0x9E3779B97F4A7C15ULL;
    }
    return requested;
}

GameState GameSession::state() const noexcept
{
    return state_;
}

double GameSession::elapsed_seconds(const GameTimePoint now) const noexcept
{
    std::chrono::duration<double> elapsed{accumulated_time_};
    if (state_ == GameState::playing && playing_since_.has_value()
        && now > *playing_since_) {
        elapsed += now - *playing_since_;
    }
    return std::max(0.0, elapsed.count());
}

std::optional<double> GameSession::best_seconds(
    const SessionBestKey& key) const noexcept
{
    const auto found{session_bests_.find(key)};
    if (found == session_bests_.end()) {
        return std::nullopt;
    }
    return found->second;
}

GameTransition GameSession::begin_exploration(const GameTimePoint now) noexcept
{
    const GameTransition result{transition(
        GameState::start, GameState::playing, CursorRequest::capture, true)};
    if (result.accepted) {
        accumulated_time_ = {};
        playing_since_ = now;
    }
    return result;
}

GameTransition GameSession::pause(const GameTimePoint now) noexcept
{
    if (state_ != GameState::playing) {
        return {false, state_, state_};
    }
    accumulate_until(now);
    return transition(
        GameState::playing, GameState::paused, CursorRequest::release, false);
}

GameTransition GameSession::resume(const GameTimePoint now) noexcept
{
    const GameTransition result{transition(
        GameState::paused, GameState::playing, CursorRequest::capture, true)};
    if (result.accepted) {
        playing_since_ = now;
    }
    return result;
}

GameTransition GameSession::complete(
    const GameTimePoint now,
    const SessionBestKey& key)
{
    if (state_ != GameState::playing) {
        return {false, state_, state_};
    }
    accumulate_until(now);
    const GameTransition result{transition(
        GameState::playing, GameState::completed, CursorRequest::release, false)};
    const double final_time{std::max(0.0, accumulated_time_.count())};
    const auto found{session_bests_.find(key)};
    if (found == session_bests_.end() || final_time < found->second) {
        session_bests_[key] = final_time;
    }
    return result;
}

GameTransition GameSession::restart_seed() noexcept
{
    if (state_ == GameState::paused) {
        return reset_from(GameState::paused);
    }
    return reset_from(GameState::completed);
}

GameTransition GameSession::new_cave() noexcept
{
    return reset_from(GameState::paused);
}

GameTransition GameSession::play_again() noexcept
{
    return reset_from(GameState::completed);
}

GameTransition GameSession::transition(
    const GameState expected,
    const GameState next,
    const CursorRequest cursor,
    const bool discard_first_mouse_delta) noexcept
{
    const GameState previous{state_};
    if (previous != expected) {
        return {false, previous, previous};
    }
    state_ = next;
    return {true, previous, next, cursor, true, discard_first_mouse_delta};
}

GameTransition GameSession::reset_from(const GameState expected) noexcept
{
    const GameTransition result{transition(
        expected, GameState::start, CursorRequest::release, false)};
    if (result.accepted) {
        accumulated_time_ = {};
        playing_since_.reset();
    }
    return result;
}

void GameSession::accumulate_until(const GameTimePoint now) noexcept
{
    if (playing_since_.has_value() && now > *playing_since_) {
        accumulated_time_ += now - *playing_since_;
    }
    playing_since_.reset();
}

}  // namespace crystalbound
