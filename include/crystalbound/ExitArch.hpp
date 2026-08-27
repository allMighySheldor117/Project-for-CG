#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "crystalbound/CaveScene.hpp"
#include "crystalbound/CrystalCollection.hpp"

namespace crystalbound {

struct ExitSocketContract {
    Element element{Element::fire};
    std::uint64_t stable_object_id{};
    GeometryVector3 position_metres{};
    MeshData crystal_mesh{};
    LinearColorMilli albedo{};
    LinearColorMilli emission{};
    ElementalAnimationParameters animation{};
};

struct ExitArchData {
    NodeId chamber_id{};
    std::uint64_t stable_object_id{};
    GeometryVector3 interaction_position_metres{};
    MeshData stone_mesh{};
    MeshData portal_mesh{};
    std::array<ExitSocketContract, 5> sockets{};
    std::uint64_t fingerprint{};
};

struct ExitArchDisplayState {
    CrystalSocketDisplayState filled{};
    bool active{};
};

enum class ExitRejectionReason : std::uint8_t {
    none,
    not_playing,
    crystals_missing,
    no_press_edge,
    invalid_query,
    out_of_range,
    outside_focus,
    occluded,
};

struct FocusedExitArch {
    double distance_metres{};
    double angle_degrees{};
};

struct ExitFocusResult {
    std::optional<FocusedExitArch> focused{};
    ExitRejectionReason rejection{ExitRejectionReason::invalid_query};
};

struct ExitAttemptResult {
    bool completed{};
    ExitRejectionReason rejection{ExitRejectionReason::invalid_query};
};

[[nodiscard]] ExitArchData build_exit_arch(const CaveGenerationResult& generation);
[[nodiscard]] std::vector<std::string> validate_exit_arch(
    const CaveGenerationResult& generation,
    const ExitArchData& arch);
[[nodiscard]] ExitArchDisplayState exit_arch_display_state(
    const ExitArchData& arch,
    const CrystalCollectionState& collection) noexcept;
[[nodiscard]] ExitFocusResult focus_exit_arch(
    const ExitArchData& arch,
    const CameraInteractionQuery& query,
    const VisibilityWorld& visibility) noexcept;
[[nodiscard]] ExitAttemptResult attempt_exit_arch(
    const ExitArchData& arch,
    const CameraInteractionQuery& query,
    const VisibilityWorld& visibility,
    bool playing,
    bool interaction_pressed_edge,
    const CrystalCollectionState& collection) noexcept;

}  // namespace crystalbound
