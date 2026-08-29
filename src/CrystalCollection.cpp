#include "crystalbound/CrystalCollection.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "crystalbound/CaveScene.hpp"

namespace crystalbound {
namespace {

constexpr double pi{3.14159265358979323846};
constexpr double millimetres_per_metre{1'000.0};
constexpr double numeric_epsilon{1.0e-9};
constexpr double ray_origin_epsilon{1.0e-6};
constexpr double authored_exit_visibility_radius_metres{24.55};
constexpr double authored_exit_visibility_height_metres{16.0};

[[nodiscard]] std::size_t element_index(const Element element) noexcept
{
    switch (element) {
    case Element::fire:
        return 0U;
    case Element::water:
        return 1U;
    case Element::earth:
        return 2U;
    case Element::air:
        return 3U;
    case Element::aether:
        return 4U;
    }
    return elemental_order.size();
}

[[nodiscard]] bool finite(const GeometryVector3 value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y)
        && std::isfinite(value.z);
}

[[nodiscard]] GeometryVector3 subtract(
    const GeometryVector3 left,
    const GeometryVector3 right) noexcept
{
    return {left.x - right.x, left.y - right.y, left.z - right.z};
}

[[nodiscard]] GeometryVector3 add(
    const GeometryVector3 left,
    const GeometryVector3 right) noexcept
{
    return {left.x + right.x, left.y + right.y, left.z + right.z};
}

[[nodiscard]] GeometryVector3 multiply(
    const GeometryVector3 value,
    const double scalar) noexcept
{
    return {value.x * scalar, value.y * scalar, value.z * scalar};
}

[[nodiscard]] double dot(
    const GeometryVector3 left,
    const GeometryVector3 right) noexcept
{
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

[[nodiscard]] GeometryVector3 cross(
    const GeometryVector3 left,
    const GeometryVector3 right) noexcept
{
    return {
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x,
    };
}

[[nodiscard]] double length(const GeometryVector3 value) noexcept
{
    return std::sqrt(dot(value, value));
}

[[nodiscard]] std::optional<GeometryVector3> normalized(
    const GeometryVector3 value) noexcept
{
    if (!finite(value)) {
        return std::nullopt;
    }
    const double magnitude{length(value)};
    if (!std::isfinite(magnitude) || magnitude <= numeric_epsilon) {
        return std::nullopt;
    }
    return multiply(value, 1.0 / magnitude);
}

void append_triangle(
    VisibilityWorld& world,
    const std::uint64_t id,
    const GeometryVector3 first,
    const GeometryVector3 second,
    const GeometryVector3 third)
{
    world.triangles.push_back({id, first, second, third});
}

[[nodiscard]] std::optional<double> ray_triangle_distance(
    const GeometryVector3 origin,
    const GeometryVector3 direction,
    const VisibilityTriangle& triangle) noexcept
{
    if (!finite(triangle.first) || !finite(triangle.second)
        || !finite(triangle.third)) {
        return std::nullopt;
    }
    const GeometryVector3 edge_one{subtract(triangle.second, triangle.first)};
    const GeometryVector3 edge_two{subtract(triangle.third, triangle.first)};
    const GeometryVector3 perpendicular{cross(direction, edge_two)};
    const double determinant{dot(edge_one, perpendicular)};
    if (std::abs(determinant) <= numeric_epsilon) {
        return std::nullopt;
    }
    const double inverse_determinant{1.0 / determinant};
    const GeometryVector3 origin_offset{subtract(origin, triangle.first)};
    const double first_coordinate{
        dot(origin_offset, perpendicular) * inverse_determinant};
    if (first_coordinate < -numeric_epsilon
        || first_coordinate > 1.0 + numeric_epsilon) {
        return std::nullopt;
    }
    const GeometryVector3 second_perpendicular{cross(origin_offset, edge_one)};
    const double second_coordinate{
        dot(direction, second_perpendicular) * inverse_determinant};
    if (second_coordinate < -numeric_epsilon
        || first_coordinate + second_coordinate > 1.0 + numeric_epsilon) {
        return std::nullopt;
    }
    const double distance{
        dot(edge_two, second_perpendicular) * inverse_determinant};
    if (!std::isfinite(distance) || distance <= ray_origin_epsilon) {
        return std::nullopt;
    }
    return distance;
}

[[nodiscard]] double distance_squared(
    const GeometryVector3 left,
    const GeometryVector3 right) noexcept
{
    const GeometryVector3 delta{subtract(left, right)};
    return dot(delta, delta);
}

[[nodiscard]] GeometryVector3 closest_segment_point(
    const GeometryVector3 point,
    const GeometryVector3 first,
    const GeometryVector3 second) noexcept
{
    const GeometryVector3 segment{subtract(second, first)};
    const double squared_length{dot(segment, segment)};
    if (squared_length <= numeric_epsilon) {
        return first;
    }
    const double amount{std::clamp(
        dot(subtract(point, first), segment) / squared_length, 0.0, 1.0)};
    return add(first, multiply(segment, amount));
}

[[nodiscard]] bool inside_visibility_space(
    const GeometryVector3 point,
    const VisibilityWorld& visibility) noexcept
{
    for (const VisibilityChamberSpace& chamber : visibility.chambers) {
        const double x{point.x - chamber.center_metres.x};
        const double z{point.z - chamber.center_metres.z};
        if (x * x + z * z
                <= chamber.radius_metres * chamber.radius_metres
                    + numeric_epsilon
            && point.y >= chamber.floor_height_metres - numeric_epsilon
            && point.y <= chamber.ceiling_height_metres + numeric_epsilon) {
            return true;
        }
    }
    for (const VisibilityRouteSpace& route : visibility.routes) {
        for (std::size_t index{1U}; index < route.samples.size(); ++index) {
            const GeometryVector3 closest{closest_segment_point(
                point,
                route.samples[index - 1U].position_metres,
                route.samples[index].position_metres)};
            if (route.bridge) {
                const double x{point.x - closest.x};
                const double z{point.z - closest.z};
                const double floor{closest.y - route.radius_metres};
                if (x * x + z * z
                        <= route.half_width_metres * route.half_width_metres
                            + numeric_epsilon
                    && point.y >= floor - numeric_epsilon) {
                    return true;
                }
            } else if (distance_squared(point, closest)
                <= route.radius_metres * route.radius_metres
                    + numeric_epsilon) {
                return true;
            }
        }
    }
    return false;
}

[[nodiscard]] bool leaves_visibility_space(
    const CameraInteractionQuery& query,
    const GeometryVector3 direction,
    const double target_distance,
    const VisibilityWorld& visibility) noexcept
{
    if (visibility.chambers.empty() && visibility.routes.empty()) {
        return false;
    }
    constexpr double maximum_sample_spacing_metres{0.05};
    const std::size_t sample_count{std::max<std::size_t>(
        1U,
        static_cast<std::size_t>(
            std::ceil(target_distance / maximum_sample_spacing_metres)))};
    for (std::size_t index{}; index <= sample_count; ++index) {
        const double distance{
            target_distance * static_cast<double>(index)
            / static_cast<double>(sample_count)};
        if (!inside_visibility_space(
                add(query.origin_metres, multiply(direction, distance)),
                visibility)) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool occluded(
    const CameraInteractionQuery& query,
    const GeometryVector3 direction,
    const double target_distance,
    const VisibilityWorld& visibility) noexcept
{
    if (leaves_visibility_space(
            query, direction, target_distance, visibility)) {
        return true;
    }
    for (const VisibilityTriangle& triangle : visibility.triangles) {
        const std::optional<double> hit{
            ray_triangle_distance(query.origin_metres, direction, triangle)};
        if (hit.has_value() && *hit < target_distance - ray_origin_epsilon) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool better_focus(
    const FocusedCrystal& candidate,
    const FocusedCrystal& current) noexcept
{
    if (candidate.angle_degrees
        < current.angle_degrees - numeric_epsilon) {
        return true;
    }
    if (std::abs(candidate.angle_degrees - current.angle_degrees)
        > numeric_epsilon) {
        return false;
    }
    if (candidate.distance_metres
        < current.distance_metres - numeric_epsilon) {
        return true;
    }
    if (std::abs(candidate.distance_metres - current.distance_metres)
        > numeric_epsilon) {
        return false;
    }
    return candidate.target.stable_object_id
        < current.target.stable_object_id;
}

}  // namespace

bool CrystalSocketDisplayState::displays(const Element element) const noexcept
{
    const std::size_t index{element_index(element)};
    return index < displayed.size() && displayed[index];
}

bool CrystalCollectionState::is_collected(const Element element) const noexcept
{
    const std::size_t index{element_index(element)};
    return index < collected_.size() && collected_[index];
}

std::size_t CrystalCollectionState::collected_count() const noexcept
{
    return static_cast<std::size_t>(
        std::count(collected_.begin(), collected_.end(), true));
}

bool CrystalCollectionState::all_collected() const noexcept
{
    return std::all_of(
        collected_.begin(), collected_.end(), [](const bool value) {
            return value;
        });
}

std::vector<Element> CrystalCollectionState::collected_elements() const
{
    std::vector<Element> result;
    result.reserve(collected_count());
    for (const Element element : elemental_order) {
        if (is_collected(element)) {
            result.push_back(element);
        }
    }
    return result;
}

CrystalSocketDisplayState
CrystalCollectionState::socket_display_state() const noexcept
{
    return {collected_};
}

bool CrystalCollectionState::collect(const Element element) noexcept
{
    const std::size_t index{element_index(element)};
    if (index >= collected_.size() || collected_[index]) {
        return false;
    }
    collected_[index] = true;
    return true;
}

bool RisingEdgeButton::update(const bool down) noexcept
{
    const bool rising_edge{down && !previous_down_};
    previous_down_ = down;
    return rising_edge;
}

void RisingEdgeButton::reset(const bool down) noexcept
{
    previous_down_ = down;
}

std::vector<CrystalInteractionTarget> build_crystal_interaction_targets(
    const ElementalSceneData& visuals)
{
    std::vector<CrystalInteractionTarget> targets;
    targets.reserve(visuals.chambers.size());
    for (const ElementalChamberVisual& chamber : visuals.chambers) {
        targets.push_back({
            chamber.element,
            chamber.crystal.stable_object_id,
            crystal_visible_body_aim_point(chamber),
        });
    }
    std::sort(targets.begin(), targets.end(), [](const auto& left, const auto& right) {
        return left.stable_object_id < right.stable_object_id;
    });
    return targets;
}

VisibilityWorld build_crystal_visibility_world(const CaveSceneData& scene)
{
    VisibilityWorld world;
    world.chambers.reserve(scene.chambers.size());
    for (const ChamberGeometryContract& chamber : scene.chambers) {
        if (chamber.side_count < 3U
            || chamber.radial_offsets_millimetres.size()
                != chamber.side_count) {
            throw std::invalid_argument(
                "Crystal visibility requires a valid chamber geometry contract.");
        }
        const double floor_y{
            static_cast<double>(chamber.center_millimetres.y_millimetres)
            / millimetres_per_metre};
        const auto compiled{std::find_if(scene.compiled_chambers.begin(),
            scene.compiled_chambers.end(), [&](const CompiledChamberTemplate& candidate) {
                return candidate.chamber_id == chamber.node_id;
            })};
        const bool authored_exit{compiled != scene.compiled_chambers.end()
            && compiled->role == ChamberTemplateRole::exit};
        world.chambers.push_back({
            chamber.node_id.value,
            {
                static_cast<double>(chamber.center_millimetres.x_millimetres)
                    / millimetres_per_metre,
                floor_y,
                static_cast<double>(chamber.center_millimetres.z_millimetres)
                    / millimetres_per_metre,
            },
            floor_y,
            floor_y + (authored_exit
                    ? authored_exit_visibility_height_metres
                    : static_cast<double>(chamber.wall_height_millimetres)
                        / millimetres_per_metre),
            authored_exit
                ? authored_exit_visibility_radius_metres
                : static_cast<double>(chamber.minimum_safe_ring_radius_millimetres)
                    / millimetres_per_metre,
        });
        if (!authored_exit) {
            for (const ChamberStructuralTriangle& triangle :
                chamber_structure_triangles(chamber, scene.portals)) {
                append_triangle(world, triangle.stable_object_id,
                    triangle.first, triangle.second, triangle.third);
            }
        }
    }
    world.routes.reserve(scene.routes.size());
    for (const RouteGeometryContract& route : scene.routes) {
        const double radius{
            static_cast<double>(route.spline.radius_millimetres)
            / millimetres_per_metre};
        world.routes.push_back({
            route.spline.stable_object_id,
            route.bridge,
            sample_centripetal_catmull_rom(route.spline),
            route.bridge
                ? static_cast<double>(route.bridge_width_millimetres)
                    / millimetres_per_metre / 2.0
                : radius,
            radius,
        });
    }
    std::sort(world.triangles.begin(), world.triangles.end(),
        [](const auto& left, const auto& right) {
            return left.stable_object_id < right.stable_object_id;
        });
    std::sort(world.chambers.begin(), world.chambers.end(),
        [](const auto& left, const auto& right) {
            return left.stable_object_id < right.stable_object_id;
        });
    std::sort(world.routes.begin(), world.routes.end(),
        [](const auto& left, const auto& right) {
            return left.stable_object_id < right.stable_object_id;
        });
    return world;
}

FocusedCrystalResult focus_crystal(
    const std::vector<CrystalInteractionTarget>& targets,
    const CameraInteractionQuery& query,
    const VisibilityWorld& visibility,
    const CrystalCollectionState& collection,
    const InteractionFocusLimits limits) noexcept
{
    const std::optional<GeometryVector3> camera_forward{
        normalized(query.forward)};
    if (!finite(query.origin_metres) || !camera_forward.has_value()
        || !std::isfinite(limits.maximum_range_metres)
        || limits.maximum_range_metres <= 0.0
        || !std::isfinite(limits.maximum_angle_degrees)
        || limits.maximum_angle_degrees < 0.0
        || limits.maximum_angle_degrees > 180.0) {
        return {{}, InteractionRejectionReason::invalid_query};
    }

    std::optional<FocusedCrystal> best;
    bool saw_collected{};
    bool saw_out_of_range{};
    bool saw_outside_focus{};
    bool saw_occluded{};
    const double minimum_dot{
        std::cos(limits.maximum_angle_degrees * pi / 180.0)};
    for (const CrystalInteractionTarget& candidate : targets) {
        if (!finite(candidate.position_metres)
            || candidate.stable_object_id == 0U) {
            return {{}, InteractionRejectionReason::invalid_query};
        }
        if (collection.is_collected(candidate.element)) {
            saw_collected = true;
            continue;
        }
        const GeometryVector3 offset{
            subtract(candidate.position_metres, query.origin_metres)};
        const double distance{length(offset)};
        if (!std::isfinite(distance) || distance <= numeric_epsilon) {
            return {{}, InteractionRejectionReason::invalid_query};
        }
        if (distance > limits.maximum_range_metres + numeric_epsilon) {
            saw_out_of_range = true;
            continue;
        }
        const GeometryVector3 direction{multiply(offset, 1.0 / distance)};
        const double alignment{
            std::clamp(dot(*camera_forward, direction), -1.0, 1.0)};
        if (alignment < minimum_dot - numeric_epsilon) {
            saw_outside_focus = true;
            continue;
        }
        if (occluded(query, direction, distance, visibility)) {
            saw_occluded = true;
            continue;
        }
        const FocusedCrystal focused{
            candidate, distance, std::acos(alignment) * 180.0 / pi};
        if (!best.has_value() || better_focus(focused, *best)) {
            best = focused;
        }
    }
    if (best.has_value()) {
        return {best, InteractionRejectionReason::none};
    }
    if (saw_occluded) {
        return {{}, InteractionRejectionReason::occluded};
    }
    if (saw_outside_focus) {
        return {{}, InteractionRejectionReason::outside_focus};
    }
    if (saw_out_of_range) {
        return {{}, InteractionRejectionReason::out_of_range};
    }
    if (saw_collected) {
        return {{}, InteractionRejectionReason::already_collected};
    }
    return {{}, InteractionRejectionReason::no_target};
}

CollectionAttemptResult attempt_crystal_collection(
    const std::vector<CrystalInteractionTarget>& targets,
    const CameraInteractionQuery& query,
    const VisibilityWorld& visibility,
    const bool interaction_pressed_edge,
    CrystalCollectionState& collection) noexcept
{
    if (!interaction_pressed_edge) {
        return {false, {}, InteractionRejectionReason::no_press_edge};
    }
    const FocusedCrystalResult focus{
        focus_crystal(targets, query, visibility, collection)};
    if (!focus.focused.has_value()) {
        return {false, {}, focus.rejection};
    }
    const Element element{focus.focused->target.element};
    if (!collection.collect(element)) {
        return {
            false, element, InteractionRejectionReason::already_collected};
    }
    return {true, element, InteractionRejectionReason::none};
}

bool is_elemental_piece_visible(
    const ElementalVisualPiece& piece,
    const CrystalCollectionState& collection) noexcept
{
    return piece.kind != ElementalPieceKind::crystal
        || !collection.is_collected(piece.element);
}

std::vector<StableLightCandidate> active_crystal_lights(
    const ElementalSceneData& visuals,
    const CrystalCollectionState& collection,
    const NodeId relevant_chamber,
    const float elapsed_seconds)
{
    std::vector<StableLightCandidate> candidates;
    candidates.reserve(visuals.chambers.size());
    for (const ElementalChamberVisual& chamber : visuals.chambers) {
        if (!collection.is_collected(chamber.element)) {
            candidates.push_back({
                crystal_point_light(chamber, elapsed_seconds),
                chamber.chamber_id == relevant_chamber,
            });
        }
    }
    return candidates;
}

}  // namespace crystalbound
