#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "crystalbound/ElementalVisuals.hpp"

namespace crystalbound {

struct CaveSceneData;

inline constexpr double maximum_crystal_interaction_range_metres{2.2};
inline constexpr double maximum_crystal_focus_angle_degrees{12.0};

struct InteractionFocusLimits {
    double maximum_range_metres{};
    double maximum_angle_degrees{};
};

inline constexpr InteractionFocusLimits crystal_interaction_focus_limits{
    maximum_crystal_interaction_range_metres,
    maximum_crystal_focus_angle_degrees};

struct CrystalInteractionTarget {
    Element element{Element::fire};
    std::uint64_t stable_object_id{};
    // Stable nominal shoulder point used consistently for range, focus, LOS,
    // and angle/distance/ID tie-breaking.
    GeometryVector3 position_metres{};
};

struct CameraInteractionQuery {
    GeometryVector3 origin_metres{};
    GeometryVector3 forward{};
};

struct VisibilityTriangle {
    std::uint64_t stable_object_id{};
    GeometryVector3 first{};
    GeometryVector3 second{};
    GeometryVector3 third{};
};

struct VisibilityChamberSpace {
    std::uint64_t stable_object_id{};
    GeometryVector3 center_metres{};
    double floor_height_metres{};
    double ceiling_height_metres{};
    double radius_metres{};
};

struct VisibilityRouteSpace {
    std::uint64_t stable_object_id{};
    bool bridge{};
    std::vector<SplineSample> samples{};
    double half_width_metres{};
    double radius_metres{};
};

struct VisibilityWorld {
    std::vector<VisibilityTriangle> triangles{};
    std::vector<VisibilityChamberSpace> chambers{};
    std::vector<VisibilityRouteSpace> routes{};
};

enum class InteractionRejectionReason : std::uint8_t {
    none,
    no_press_edge,
    invalid_query,
    no_target,
    out_of_range,
    outside_focus,
    occluded,
    already_collected,
};

struct FocusedCrystal {
    CrystalInteractionTarget target{};
    double distance_metres{};
    double angle_degrees{};
};

struct FocusedCrystalResult {
    std::optional<FocusedCrystal> focused{};
    InteractionRejectionReason rejection{InteractionRejectionReason::no_target};
};

struct CrystalSocketDisplayState {
    std::array<bool, 5> displayed{};

    [[nodiscard]] bool displays(Element element) const noexcept;
};

class CrystalCollectionState final {
public:
    [[nodiscard]] bool is_collected(Element element) const noexcept;
    [[nodiscard]] std::size_t collected_count() const noexcept;
    [[nodiscard]] bool all_collected() const noexcept;
    [[nodiscard]] std::vector<Element> collected_elements() const;
    [[nodiscard]] CrystalSocketDisplayState socket_display_state() const noexcept;
    [[nodiscard]] bool collect(Element element) noexcept;

private:
    std::array<bool, 5> collected_{};
};

struct CollectionAttemptResult {
    bool collected{};
    std::optional<Element> element{};
    InteractionRejectionReason rejection{InteractionRejectionReason::no_target};
};

class RisingEdgeButton final {
public:
    [[nodiscard]] bool update(bool down) noexcept;
    void reset(bool down = false) noexcept;

private:
    bool previous_down_{};
};

[[nodiscard]] std::vector<CrystalInteractionTarget> build_crystal_interaction_targets(
    const ElementalSceneData& visuals);
[[nodiscard]] VisibilityWorld build_crystal_visibility_world(
    const CaveSceneData& scene);
[[nodiscard]] FocusedCrystalResult focus_crystal(
    const std::vector<CrystalInteractionTarget>& targets,
    const CameraInteractionQuery& query,
    const VisibilityWorld& visibility,
    const CrystalCollectionState& collection,
    InteractionFocusLimits limits = crystal_interaction_focus_limits) noexcept;
[[nodiscard]] CollectionAttemptResult attempt_crystal_collection(
    const std::vector<CrystalInteractionTarget>& targets,
    const CameraInteractionQuery& query,
    const VisibilityWorld& visibility,
    bool interaction_pressed_edge,
    CrystalCollectionState& collection) noexcept;
[[nodiscard]] bool is_elemental_piece_visible(
    const ElementalVisualPiece& piece,
    const CrystalCollectionState& collection) noexcept;
[[nodiscard]] std::vector<StableLightCandidate> active_crystal_lights(
    const ElementalSceneData& visuals,
    const CrystalCollectionState& collection,
    NodeId relevant_chamber,
    float elapsed_seconds);

}  // namespace crystalbound
