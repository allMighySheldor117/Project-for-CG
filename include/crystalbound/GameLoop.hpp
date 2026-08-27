#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "crystalbound/Generation.hpp"

namespace crystalbound {

using GameClock = std::chrono::steady_clock;
using GameTimePoint = GameClock::time_point;

enum class GameState : std::uint8_t {
    start,
    playing,
    paused,
    completed,
};

enum class CursorRequest : std::uint8_t {
    unchanged,
    capture,
    release,
};

struct GameTransition {
    bool accepted{};
    GameState from{GameState::start};
    GameState to{GameState::start};
    CursorRequest cursor{CursorRequest::unchanged};
    bool clear_held_input{};
    bool discard_first_mouse_delta{};
};

struct SessionBestKey {
    GeneratorVersion generator_version{};
    Seed effective_seed{};
};

[[nodiscard]] bool operator<(
    const SessionBestKey& left,
    const SessionBestKey& right) noexcept;

enum class GameUiField : std::uint8_t {
    premise,
    objective,
    controls,
    seed,
    elapsed_time,
    crystal_progress,
    interaction_prompt,
    session_best,
};

enum class GameUiAction : std::uint8_t {
    begin_exploration,
    resume,
    restart_seed,
    new_cave,
    play_again,
    quit,
};

struct GameUiContract {
    std::vector<GameUiField> fields{};
    std::vector<GameUiAction> actions{};

    [[nodiscard]] bool has_field(GameUiField field) const noexcept;
    [[nodiscard]] bool has_action(GameUiAction action) const noexcept;
};

[[nodiscard]] GameUiContract game_ui_contract(GameState state);
[[nodiscard]] std::string format_run_seed_label(const GenerationResult& generation);
[[nodiscard]] Seed choose_new_requested_seed(
    Seed current,
    const std::function<std::uint64_t()>& source);

class GameSession final {
public:
    [[nodiscard]] GameState state() const noexcept;
    [[nodiscard]] double elapsed_seconds(GameTimePoint now) const noexcept;
    [[nodiscard]] std::optional<double> best_seconds(
        const SessionBestKey& key) const noexcept;

    [[nodiscard]] GameTransition begin_exploration(GameTimePoint now) noexcept;
    [[nodiscard]] GameTransition pause(GameTimePoint now) noexcept;
    [[nodiscard]] GameTransition resume(GameTimePoint now) noexcept;
    [[nodiscard]] GameTransition complete(
        GameTimePoint now,
        const SessionBestKey& key);
    [[nodiscard]] GameTransition restart_seed() noexcept;
    [[nodiscard]] GameTransition new_cave() noexcept;
    [[nodiscard]] GameTransition play_again() noexcept;

private:
    [[nodiscard]] GameTransition transition(
        GameState expected,
        GameState next,
        CursorRequest cursor,
        bool discard_first_mouse_delta) noexcept;
    [[nodiscard]] GameTransition reset_from(GameState expected) noexcept;
    void accumulate_until(GameTimePoint now) noexcept;

    GameState state_{GameState::start};
    std::chrono::duration<double> accumulated_time_{};
    std::optional<GameTimePoint> playing_since_{};
    std::map<SessionBestKey, double> session_bests_{};
};

}  // namespace crystalbound
