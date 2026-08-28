#include "crystalbound/ChamberTemplates.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <queue>
#include <stdexcept>
#include <tuple>
#include <utility>

#include "crystalbound/ElementalVisuals.hpp"

namespace crystalbound {
namespace {

constexpr std::uint64_t fnv_offset_basis{14695981039346656037ULL};
constexpr std::uint64_t fnv_prime{1099511628211ULL};
constexpr std::uint32_t template_schema_version{1U};

constexpr std::array<TemplatePoint2, 8> socket_directions{{
    {1'000'000, 0},
    {707'107, 707'107},
    {0, 1'000'000},
    {-707'107, 707'107},
    {-1'000'000, 0},
    {-707'107, -707'107},
    {0, -1'000'000},
    {707'107, -707'107},
}};

[[nodiscard]] std::int64_t checked_multiply(
    const std::int64_t left,
    const std::int64_t right)
{
    if (left == 0 || right == 0) {
        return 0;
    }
    if (left == -1 && right == std::numeric_limits<std::int64_t>::min()) {
        throw std::overflow_error{"template integer multiplication overflow"};
    }
    if (right == -1 && left == std::numeric_limits<std::int64_t>::min()) {
        throw std::overflow_error{"template integer multiplication overflow"};
    }
    const std::int64_t maximum{std::numeric_limits<std::int64_t>::max()};
    const std::int64_t minimum{std::numeric_limits<std::int64_t>::min()};
    const bool overflow{left > 0
            ? (right > 0 ? left > maximum / right : right < minimum / left)
            : (right > 0 ? left < minimum / right : left < maximum / right)};
    if (overflow) {
        throw std::overflow_error{"template integer multiplication overflow"};
    }
    return left * right;
}

[[nodiscard]] std::int64_t positive_floor_divide(
    const std::int64_t numerator,
    const std::int64_t denominator)
{
    if (numerator < 0 || denominator <= 0) {
        throw std::invalid_argument{"template scale division requires non-negative numerator"};
    }
    return numerator / denominator;
}

[[nodiscard]] std::int64_t positive_ceiling_divide(
    const std::int64_t numerator,
    const std::int64_t denominator)
{
    const std::int64_t quotient{positive_floor_divide(numerator, denominator)};
    return quotient + (numerator % denominator == 0 ? 0 : 1);
}

[[nodiscard]] std::int32_t checked_narrow(const std::int64_t value)
{
    if (value < std::numeric_limits<std::int32_t>::min()
        || value > std::numeric_limits<std::int32_t>::max()) {
        throw std::overflow_error{"template integer result is outside int32"};
    }
    return static_cast<std::int32_t>(value);
}

[[nodiscard]] std::int64_t round_half_away_from_zero_q30(
    const std::int64_t numerator)
{
    const std::int64_t half{template_rotation_one_q30 / 2};
    if (numerator >= 0) {
        return (numerator + half) / template_rotation_one_q30;
    }
    return -((-numerator + half) / template_rotation_one_q30);
}

[[nodiscard]] std::vector<TemplatePoint2> rectangle(
    const std::int32_t minimum_x,
    const std::int32_t maximum_x,
    const std::int32_t minimum_z,
    const std::int32_t maximum_z)
{
    return {{minimum_x, minimum_z}, {minimum_x, maximum_z},
        {maximum_x, maximum_z}, {maximum_x, minimum_z}};
}

[[nodiscard]] std::vector<TemplatePoint2> octagon(const std::int32_t radius)
{
    const std::int32_t diagonal{static_cast<std::int32_t>(
        static_cast<std::int64_t>(radius) * 707'107 / 1'000'000)};
    return {{-radius, 0}, {-diagonal, diagonal}, {0, radius},
        {diagonal, diagonal}, {radius, 0}, {diagonal, -diagonal},
        {0, -radius}, {-diagonal, -diagonal}};
}

[[nodiscard]] std::int64_t signed_twice_area(
    const std::vector<TemplatePoint2>& polygon)
{
    std::int64_t area{};
    for (std::size_t index{}; index < polygon.size(); ++index) {
        const TemplatePoint2 first{polygon[index]};
        const TemplatePoint2 second{polygon[(index + 1U) % polygon.size()]};
        area += static_cast<std::int64_t>(first.x_millimetres)
                * second.z_millimetres
            - static_cast<std::int64_t>(second.x_millimetres)
                * first.z_millimetres;
    }
    return area;
}

struct Bounds2 {
    std::int32_t minimum_x{};
    std::int32_t maximum_x{};
    std::int32_t minimum_z{};
    std::int32_t maximum_z{};
};

[[nodiscard]] Bounds2 polygon_bounds(const std::vector<TemplatePoint2>& polygon)
{
    Bounds2 result{polygon.front().x_millimetres, polygon.front().x_millimetres,
        polygon.front().z_millimetres, polygon.front().z_millimetres};
    for (const TemplatePoint2 point : polygon) {
        result.minimum_x = std::min(result.minimum_x, point.x_millimetres);
        result.maximum_x = std::max(result.maximum_x, point.x_millimetres);
        result.minimum_z = std::min(result.minimum_z, point.z_millimetres);
        result.maximum_z = std::max(result.maximum_z, point.z_millimetres);
    }
    return result;
}

[[nodiscard]] bool bounds_overlap(const Bounds2 left, const Bounds2 right) noexcept
{
    return left.minimum_x < right.maximum_x && right.minimum_x < left.maximum_x
        && left.minimum_z < right.maximum_z && right.minimum_z < left.maximum_z;
}

[[nodiscard]] TemplateSurfaceKind surface_for_role(const ChamberTemplateRole role)
{
    switch (role) {
    case ChamberTemplateRole::fire:
        return TemplateSurfaceKind::basalt;
    case ChamberTemplateRole::water:
        return TemplateSurfaceKind::shallow_water;
    case ChamberTemplateRole::earth:
        return TemplateSurfaceKind::earth;
    case ChamberTemplateRole::air:
        return TemplateSurfaceKind::wood;
    case ChamberTemplateRole::aether:
        return TemplateSurfaceKind::aether_stone;
    case ChamberTemplateRole::start:
    case ChamberTemplateRole::exit:
    case ChamberTemplateRole::neutral:
        return TemplateSurfaceKind::stone;
    }
    throw std::logic_error{"unknown chamber template role"};
}

[[nodiscard]] TemplateAnchorKind interactable_kind(const ChamberTemplateRole role)
{
    if (role == ChamberTemplateRole::exit) {
        return TemplateAnchorKind::exit_arch;
    }
    if (role == ChamberTemplateRole::start) {
        return TemplateAnchorKind::start_camera_target;
    }
    return TemplateAnchorKind::crystal;
}

[[nodiscard]] ChamberTemplate make_template(
    const ChamberTemplateRole role,
    const std::int32_t outer_width,
    const std::int32_t outer_depth,
    const std::int32_t usable_diameter,
    const std::int32_t usable_height)
{
    ChamberTemplate chamber;
    chamber.role = role;
    chamber.outer_width_millimetres = outer_width;
    chamber.outer_depth_millimetres = outer_depth;
    chamber.usable_diameter_millimetres = usable_diameter;
    chamber.usable_height_millimetres = usable_height;

    const std::int32_t usable_radius{usable_diameter / 2};
    chamber.floor_patches.push_back(
        {1U, octagon(usable_radius), 0, surface_for_role(role), 100U, true});

    const std::int32_t half_width{outer_width / 2};
    const std::int32_t half_depth{outer_depth / 2};
    constexpr std::int32_t wall_thickness{400};
    chamber.boundary_patches = {
        {1U, rectangle(-half_width, -half_width + wall_thickness,
                 -half_depth, half_depth),
            0, usable_height, true, true},
        {2U, rectangle(half_width - wall_thickness, half_width,
                 -half_depth, half_depth),
            0, usable_height, true, true},
        {3U, rectangle(-half_width + wall_thickness,
                 half_width - wall_thickness, -half_depth,
                 -half_depth + wall_thickness),
            0, usable_height, true, true},
        {4U, rectangle(-half_width + wall_thickness,
                 half_width - wall_thickness, half_depth - wall_thickness,
                 half_depth),
            0, usable_height, true, true},
    };

    constexpr std::int32_t landing_half_width{1'200};
    constexpr std::int32_t landing_depth{1'800};
    for (std::uint8_t index{}; index < socket_directions.size(); ++index) {
        const TemplatePoint2 direction{socket_directions[index]};
        const std::int32_t origin_x{direction.x_millimetres == 0
                ? 0
                : (direction.x_millimetres > 0 ? half_width : -half_width)};
        const std::int32_t origin_z{direction.z_millimetres == 0
                ? 0
                : (direction.z_millimetres > 0 ? half_depth : -half_depth)};
        const auto offset = [&](const std::int32_t millimetres,
                                const std::int32_t component) {
            return static_cast<std::int32_t>(
                static_cast<std::int64_t>(millimetres) * component / 1'000'000);
        };
        const TemplatePoint2 origin{origin_x, origin_z};
        const TemplatePoint2 inner{origin_x - offset(1'500, direction.x_millimetres),
            origin_z - offset(1'500, direction.z_millimetres)};
        const TemplatePoint2 outer{origin_x + offset(100, direction.x_millimetres),
            origin_z + offset(100, direction.z_millimetres)};
        const std::uint32_t clear_id{100U + index};
        chamber.sockets.push_back({index, origin, direction, 3'200, 4'000,
            inner, outer, 1U, clear_id});
        chamber.clear_zones.push_back({clear_id,
            rectangle(inner.x_millimetres - landing_half_width,
                inner.x_millimetres + landing_half_width,
                inner.z_millimetres - landing_depth / 2,
                inner.z_millimetres + landing_depth / 2),
            0, movement_envelope.minimum_clearance_height_millimetres,
            TemplateClearPurpose::socket_landing});
        chamber.navigation_edges.push_back({100U + index, 1U, clear_id,
            movement_envelope.minimum_clearance_width_millimetres,
            movement_envelope.step_height_millimetres,
            movement_envelope.maximum_slope_millidegrees,
            movement_envelope.maximum_gap_millimetres, true});
    }

    chamber.clear_zones.push_back({200U, rectangle(-1'100, 1'100, -1'100, 1'100),
        0, 2'500, role == ChamberTemplateRole::exit
                ? TemplateClearPurpose::exit_arch
                : TemplateClearPurpose::crystal});
    chamber.clear_zones.push_back({201U, rectangle(-1'600, 1'600, -3'000, 3'000),
        0, movement_envelope.minimum_clearance_height_millimetres,
        TemplateClearPurpose::required_corridor});
    chamber.navigation_edges.push_back({200U, 1U, 200U, 2'400,
        movement_envelope.step_height_millimetres,
        movement_envelope.maximum_slope_millidegrees,
        movement_envelope.maximum_gap_millimetres, true});

    const std::int32_t landmark_x{role == ChamberTemplateRole::water
            || role == ChamberTemplateRole::air
        ? -usable_radius / 3
        : usable_radius / 3};
    const std::int32_t landmark_z{role == ChamberTemplateRole::fire
            || role == ChamberTemplateRole::aether
        ? usable_radius / 3
        : -usable_radius / 3};
    chamber.anchors = {
        {1U, TemplateAnchorKind::structural_landmark,
            {landmark_x, 0, landmark_z}, 0},
        {2U, interactable_kind(role), {0, 400, 0}, 0},
        {3U, TemplateAnchorKind::pedestal, {0, 0, 0}, 0},
        {4U, TemplateAnchorKind::cosmetic,
            {-landmark_x, usable_height / 3, -landmark_z}, 180'000},
    };

    if (role == ChamberTemplateRole::fire) {
        chamber.hazards = {
            {1U, rectangle(-7'200, -3'200, 1'800, 5'800), -1'000, 500,
                TemplateHazardKind::lava, 10U,
                TemplateRespawnPolicy::last_safe_checkpoint},
            {2U, rectangle(3'200, 7'200, 1'800, 5'800), -1'000, 500,
                TemplateHazardKind::lava, 10U,
                TemplateRespawnPolicy::last_safe_checkpoint},
        };
    } else if (role == ChamberTemplateRole::aether) {
        chamber.hazards = {
            {1U, rectangle(-7'500, -3'500, -5'500, -2'200), -3'000, 200,
                TemplateHazardKind::aether_void, 20U,
                TemplateRespawnPolicy::last_safe_checkpoint},
            {2U, rectangle(3'500, 7'500, -5'500, -2'200), -3'000, 200,
                TemplateHazardKind::aether_void, 20U,
                TemplateRespawnPolicy::last_safe_checkpoint},
        };
    }
    return chamber;
}

[[nodiscard]] ChamberTemplate make_water_template()
{
    ChamberTemplate chamber{
        make_template(ChamberTemplateRole::water,
            50'600, 38'600, 31'000, 14'000)};
    chamber.floor_patches.front().clockwise_polygon =
        rectangle(-22'000, 22'000, -16'000, 16'000);
    const auto add_support = [&](const std::uint32_t stable_id,
                                 const std::vector<TemplatePoint2>& polygon,
                                 const std::int32_t height_millimetres) {
        chamber.floor_patches.push_back({stable_id, polygon, height_millimetres,
            surface_for_role(ChamberTemplateRole::water), 720U, true});
    };
    add_support(2U, rectangle(-25'200, -22'000, -2'875, 2'875), 1'650);
    add_support(3U, rectangle(-21'960, -21'140, -2'675, 2'675), 1'414);
    add_support(4U, rectangle(-21'180, -20'360, -2'675, 2'675), 1'179);
    add_support(5U, rectangle(-20'400, -19'580, -2'675, 2'675), 943);
    add_support(6U, rectangle(-19'620, -18'800, -2'675, 2'675), 707);
    add_support(7U, rectangle(-18'840, -18'020, -2'675, 2'675), 471);
    add_support(8U, rectangle(-18'060, -17'240, -2'675, 2'675), 236);
    add_support(9U, rectangle(-2'875, 2'875, 16'000, 19'200), 1'650);
    add_support(10U, rectangle(-2'675, 2'675, 15'140, 15'960), 1'414);
    add_support(11U, rectangle(-2'675, 2'675, 14'360, 15'180), 1'179);
    add_support(12U, rectangle(-2'675, 2'675, 13'580, 14'400), 943);
    add_support(13U, rectangle(-2'675, 2'675, 12'800, 13'620), 707);
    add_support(14U, rectangle(-2'675, 2'675, 12'020, 12'840), 471);
    add_support(15U, rectangle(-2'675, 2'675, 11'240, 12'060), 236);
    const auto configure_gateway = [&](const std::uint8_t index,
                                       const TemplatePoint2 origin,
                                       const TemplatePoint2 outer,
                                       const std::vector<TemplatePoint2>& clear_polygon) {
        TemplateSocket& socket{chamber.sockets[index]};
        socket.origin_millimetres = origin;
        socket.vestibule_inner_millimetres = origin;
        socket.vestibule_outer_millimetres = outer;
        const std::uint32_t clear_zone_id{socket.landing_clear_zone_id};
        const auto clear_zone{std::find_if(
            chamber.clear_zones.begin(), chamber.clear_zones.end(),
            [clear_zone_id](const TemplateClearZone& zone) {
                return zone.stable_id == clear_zone_id;
            })};
        if (clear_zone == chamber.clear_zones.end()) {
            throw std::logic_error{"Water gateway has no landing clear zone."};
        }
        clear_zone->clockwise_polygon = clear_polygon;
        clear_zone->minimum_y_millimetres = 0;
        clear_zone->maximum_y_millimetres = 5'400;
    };
    configure_gateway(4U, {-22'000, 0}, {-25'200, 0},
        rectangle(-25'300, -16'800, -2'900, 2'900));
    configure_gateway(2U, {0, 16'000}, {0, 19'200},
        rectangle(-2'900, 2'900, 10'800, 19'300));
    const auto crystal_clear_zone{std::find_if(
        chamber.clear_zones.begin(), chamber.clear_zones.end(),
        [](const TemplateClearZone& zone) { return zone.stable_id == 200U; })};
    if (crystal_clear_zone == chamber.clear_zones.end()) {
        throw std::logic_error{"Water template has no crystal clear zone."};
    }
    crystal_clear_zone->clockwise_polygon =
        rectangle(-2'900, 2'900, -2'900, 2'900);
    crystal_clear_zone->maximum_y_millimetres = 6'300;
    return chamber;
}

[[nodiscard]] ChamberTemplate make_start_template()
{
    ChamberTemplate chamber{
        make_template(ChamberTemplateRole::start,
            16'300, 20'150, 15'000, 8'000)};
    chamber.floor_patches.front().clockwise_polygon =
        rectangle(-7'500, 7'500, -9'000, 9'000);
    chamber.floor_patches.push_back({2U,
        rectangle(-1'600, 1'600, -10'500, -9'000), 60,
        surface_for_role(ChamberTemplateRole::start), 700U, true});

    TemplateSocket& socket{chamber.sockets[6U]};
    socket.origin_millimetres = {0, -9'000};
    socket.vestibule_inner_millimetres = {0, -9'000};
    socket.vestibule_outer_millimetres = {0, -10'500};
    const auto clear_zone{std::find_if(chamber.clear_zones.begin(),
        chamber.clear_zones.end(), [socket](const TemplateClearZone& zone) {
            return zone.stable_id == socket.landing_clear_zone_id;
        })};
    if (clear_zone == chamber.clear_zones.end()) {
        throw std::logic_error{"Start template has no entrance clear zone."};
    }
    clear_zone->clockwise_polygon =
        rectangle(-1'850, 1'850, -10'600, -7'800);
    return chamber;
}

[[nodiscard]] ChamberTemplate make_aether_template()
{
    ChamberTemplate chamber{
        make_template(ChamberTemplateRole::aether,
            50'000, 45'400, 44'440, 16'700)};
    chamber.floor_patches.front().clockwise_polygon = octagon(22'220);
    chamber.floor_patches.push_back({2U,
        rectangle(-25'000, 25'000, -2'100, 2'100), 280,
        surface_for_role(ChamberTemplateRole::aether), 720U, true});
    chamber.hazards.clear();

    const auto configure_gateway = [&](const std::uint8_t index,
                                       const std::int32_t direction) {
        TemplateSocket& socket{chamber.sockets[index]};
        socket.origin_millimetres = {direction * 22'000, 0};
        socket.vestibule_inner_millimetres = {direction * 22'000, 0};
        socket.vestibule_outer_millimetres = {direction * 25'000, 0};
        const auto clear_zone{std::find_if(chamber.clear_zones.begin(),
            chamber.clear_zones.end(), [socket](const TemplateClearZone& zone) {
                return zone.stable_id == socket.landing_clear_zone_id;
            })};
        if (clear_zone == chamber.clear_zones.end()) {
            throw std::logic_error{"Aether template has no entrance clear zone."};
        }
        clear_zone->clockwise_polygon = direction < 0
            ? rectangle(-25'100, -19'500, -2'350, 2'350)
            : rectangle(19'500, 25'100, -2'350, 2'350);
    };
    configure_gateway(0U, 1);
    configure_gateway(4U, -1);
    return chamber;
}

[[nodiscard]] ChamberTemplate make_exit_template()
{
    ChamberTemplate chamber{
        make_template(ChamberTemplateRole::exit,
            29'100, 46'100, 28'000, 16'000)};
    chamber.floor_patches.front().clockwise_polygon =
        rectangle(-14'000, 14'000, -21'000, 21'000);
    chamber.floor_patches.push_back({2U,
        rectangle(-2'000, 2'000, 21'000, 24'550), 60,
        surface_for_role(ChamberTemplateRole::exit), 700U, true});

    TemplateSocket& socket{chamber.sockets[2U]};
    socket.origin_millimetres = {0, 21'000};
    socket.vestibule_inner_millimetres = {0, 21'000};
    socket.vestibule_outer_millimetres = {0, 24'550};
    const auto entrance_clear{std::find_if(chamber.clear_zones.begin(),
        chamber.clear_zones.end(), [socket](const TemplateClearZone& zone) {
            return zone.stable_id == socket.landing_clear_zone_id;
        })};
    if (entrance_clear == chamber.clear_zones.end()) {
        throw std::logic_error{"Exit template has no entrance clear zone."};
    }
    entrance_clear->clockwise_polygon =
        rectangle(-2'250, 2'250, 20'000, 24'650);
    for (TemplateClearZone& zone : chamber.clear_zones) {
        if (zone.stable_id == 200U) {
            zone.clockwise_polygon =
                rectangle(-3'500, 3'500, -21'000, -16'500);
            zone.maximum_y_millimetres = 12'000;
        }
    }
    return chamber;
}

[[nodiscard]] ChamberTemplate make_fire_template()
{
    ChamberTemplate chamber{
        make_template(ChamberTemplateRole::fire,
            64'000, 64'000, 55'000, 23'000)};
    chamber.hazards = {
        {1U, rectangle(-27'500, -6'000, -27'500, 27'500), -500, 500,
            TemplateHazardKind::lava, 10U,
            TemplateRespawnPolicy::last_safe_checkpoint},
        {2U, rectangle(6'100, 27'500, -27'500, 27'500), -500, 500,
            TemplateHazardKind::lava, 10U,
            TemplateRespawnPolicy::last_safe_checkpoint},
        {3U, rectangle(-6'000, 6'100, -27'500, -6'100), -500, 500,
            TemplateHazardKind::lava, 10U,
            TemplateRespawnPolicy::last_safe_checkpoint},
        {4U, rectangle(-6'000, 6'100, 5'600, 27'500), -500, 500,
            TemplateHazardKind::lava, 10U,
            TemplateRespawnPolicy::last_safe_checkpoint},
    };
    const auto configure_gateway = [&](const std::uint8_t index,
                                       const TemplatePoint2 origin,
                                       const TemplatePoint2 inner,
                                       const TemplatePoint2 outer,
                                       const std::vector<TemplatePoint2>& clear_polygon) {
        TemplateSocket& socket{chamber.sockets[index]};
        socket.origin_millimetres = origin;
        socket.vestibule_inner_millimetres = inner;
        socket.vestibule_outer_millimetres = outer;
        const auto clear_zone{std::find_if(
            chamber.clear_zones.begin(), chamber.clear_zones.end(),
            [socket](const TemplateClearZone& zone) {
                return zone.stable_id == socket.landing_clear_zone_id;
            })};
        if (clear_zone == chamber.clear_zones.end()) {
            throw std::logic_error{"Fire gateway has no landing clear zone."};
        }
        clear_zone->clockwise_polygon = clear_polygon;
        clear_zone->minimum_y_millimetres = 500;
        clear_zone->maximum_y_millimetres = 6'200;
    };
    configure_gateway(0U, {27'500, 0}, {26'500, 0}, {31'500, 0},
        rectangle(22'000, 31'700, -3'200, 3'200));
    configure_gateway(2U, {0, 27'500}, {0, 26'500}, {0, 31'500},
        rectangle(-3'200, 3'200, 22'000, 31'700));
    for (TemplateClearZone& zone : chamber.clear_zones) {
        if (zone.stable_id == 200U || zone.stable_id == 201U) {
            zone.minimum_y_millimetres = 500;
        }
    }
    return chamber;
}

[[nodiscard]] ChamberTemplate make_air_template()
{
    ChamberTemplate chamber{
        make_template(ChamberTemplateRole::air, 50'000, 50'000, 40'000, 22'000)};
    const auto configure_gateway = [&](const std::uint8_t index,
                                       const std::int32_t direction) {
        TemplateSocket& socket{chamber.sockets[index]};
        socket.origin_millimetres = {0, direction * 21'350};
        socket.vestibule_inner_millimetres = {0, direction * 20'000};
        socket.vestibule_outer_millimetres = {0, direction * 24'750};
        const auto clear_zone{std::find_if(
            chamber.clear_zones.begin(), chamber.clear_zones.end(),
            [socket](const TemplateClearZone& zone) {
                return zone.stable_id == socket.landing_clear_zone_id;
            })};
        if (clear_zone == chamber.clear_zones.end()) {
            throw std::logic_error{"Air gateway has no landing clear zone."};
        }
        clear_zone->clockwise_polygon = direction < 0
            ? rectangle(-2'600, 2'600, -25'000, -18'500)
            : rectangle(-2'600, 2'600, 18'500, 25'000);
    };
    configure_gateway(2U, 1);
    configure_gateway(6U, -1);
    return chamber;
}

[[nodiscard]] ChamberTemplate make_earth_template()
{
    ChamberTemplate chamber{
        make_template(ChamberTemplateRole::earth, 47'000, 47'000, 40'000, 13'000)};
    const auto configure_gateway = [&](const std::uint8_t index,
                                       const TemplatePoint2 origin,
                                       const TemplatePoint2 inner,
                                       const TemplatePoint2 outer,
                                       const std::vector<TemplatePoint2>& clear_polygon) {
        TemplateSocket& socket{chamber.sockets[index]};
        socket.origin_millimetres = origin;
        socket.vestibule_inner_millimetres = inner;
        socket.vestibule_outer_millimetres = outer;
        socket.arch_width_millimetres = 3'200;
        socket.arch_height_millimetres = 4'000;
        const std::uint32_t clear_zone_id{socket.landing_clear_zone_id};
        const auto clear_zone{std::find_if(
            chamber.clear_zones.begin(), chamber.clear_zones.end(),
            [clear_zone_id](const TemplateClearZone& zone) {
                return zone.stable_id == clear_zone_id;
            })};
        if (clear_zone == chamber.clear_zones.end()) {
            throw std::logic_error{"Earth gateway has no landing clear zone."};
        }
        clear_zone->clockwise_polygon = clear_polygon;
        clear_zone->minimum_y_millimetres = 0;
        clear_zone->maximum_y_millimetres = 4'000;
    };
    configure_gateway(0U, {20'000, 0}, {19'200, 0}, {23'200, 0},
        rectangle(18'500, 23'300, -1'500, 1'500));
    configure_gateway(2U, {0, 20'000}, {0, 19'200}, {0, 23'200},
        rectangle(-1'500, 1'500, 18'500, 23'300));
    return chamber;
}

[[nodiscard]] const std::vector<ChamberTemplate>& template_table()
{
    static const std::vector<ChamberTemplate> templates{
        make_start_template(),
        make_fire_template(),
        make_water_template(),
        make_earth_template(),
        make_air_template(),
        make_aether_template(),
        make_exit_template(),
        make_template(ChamberTemplateRole::neutral, 17'000, 19'000, 14'000, 6'000),
    };
    return templates;
}

[[nodiscard]] const ChamberNode* find_node(
    const TopologyData& topology,
    const NodeId id)
{
    const auto found{std::find_if(topology.nodes.begin(), topology.nodes.end(),
        [id](const ChamberNode& node) { return node.id == id; })};
    return found == topology.nodes.end() ? nullptr : &*found;
}

[[nodiscard]] std::vector<NodeId> sorted_neighbours(
    const TopologyData& topology,
    const NodeId id)
{
    std::vector<NodeId> result;
    for (const Edge edge : topology.edges) {
        if (edge.first == id) {
            result.push_back(edge.second);
        } else if (edge.second == id) {
            result.push_back(edge.first);
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

[[nodiscard]] std::uint8_t closest_octant(const TemplatePoint2 vector)
{
    std::uint8_t best{};
    std::int64_t best_dot{std::numeric_limits<std::int64_t>::min()};
    for (std::uint8_t index{}; index < socket_directions.size(); ++index) {
        const std::int64_t dot{
            static_cast<std::int64_t>(vector.x_millimetres)
                    * socket_directions[index].x_millimetres
                + static_cast<std::int64_t>(vector.z_millimetres)
                    * socket_directions[index].z_millimetres};
        if (dot > best_dot) {
            best_dot = dot;
            best = index;
        }
    }
    return best;
}

[[nodiscard]] TemplatePoint2 vector_between(
    const ChamberNode& from,
    const ChamberNode& to)
{
    return {to.anchor.x_millimetres - from.anchor.x_millimetres,
        to.anchor.z_millimetres - from.anchor.z_millimetres};
}

struct AssignmentCandidate {
    std::vector<std::uint8_t> sockets{};
    std::uint64_t score{};
};

[[nodiscard]] bool socket_accepts_vector(
    const TemplatePoint2 socket_direction_million,
    const TemplatePoint2 vector_millimetres,
    const std::int64_t tangent_limit) noexcept
{
    const std::int64_t dot{
        static_cast<std::int64_t>(socket_direction_million.x_millimetres)
                * vector_millimetres.x_millimetres
            + static_cast<std::int64_t>(socket_direction_million.z_millimetres)
                * vector_millimetres.z_millimetres};
    const std::int64_t cross{
        static_cast<std::int64_t>(socket_direction_million.x_millimetres)
                * vector_millimetres.z_millimetres
            - static_cast<std::int64_t>(socket_direction_million.z_millimetres)
                * vector_millimetres.x_millimetres};
    if (dot <= 0 || cross == std::numeric_limits<std::int64_t>::min()) {
        return false;
    }
    const std::int64_t absolute_cross{std::max(cross, -cross)};
    if (absolute_cross > std::numeric_limits<std::int64_t>::max() / 1'000'000LL
        || dot > std::numeric_limits<std::int64_t>::max() / tangent_limit) {
        return false;
    }
    return absolute_cross * 1'000'000LL <= dot * tangent_limit;
}

[[nodiscard]] std::uint64_t socket_tangent_score(
    const TemplatePoint2 socket_direction_million,
    const TemplatePoint2 vector_millimetres) noexcept
{
    const std::int64_t dot{
        static_cast<std::int64_t>(socket_direction_million.x_millimetres)
                * vector_millimetres.x_millimetres
            + static_cast<std::int64_t>(socket_direction_million.z_millimetres)
                * vector_millimetres.z_millimetres};
    const std::int64_t cross{
        static_cast<std::int64_t>(socket_direction_million.x_millimetres)
                * vector_millimetres.z_millimetres
            - static_cast<std::int64_t>(socket_direction_million.z_millimetres)
                * vector_millimetres.x_millimetres};
    if (dot <= 0 || cross == std::numeric_limits<std::int64_t>::min()) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return static_cast<std::uint64_t>(
        std::max(cross, -cross) * 1'000'000LL / dot);
}

void enumerate_assignments(
    const std::vector<TemplatePoint2>& vectors,
    const std::size_t index,
    const std::int64_t tangent_limit,
    std::array<bool, 8>& used,
    AssignmentCandidate& current,
    std::optional<AssignmentCandidate>& best)
{
    if (index == vectors.size()) {
        if (!best.has_value() || current.score < best->score
            || (current.score == best->score && current.sockets < best->sockets)) {
            best = current;
        }
        return;
    }
    const std::uint8_t preferred{closest_octant(vectors[index])};
    std::array<std::uint8_t, 8> order{};
    for (std::uint8_t offset{}; offset < order.size(); ++offset) {
        order[offset] = static_cast<std::uint8_t>((preferred + offset) % 8U);
    }
    std::stable_sort(order.begin(), order.end(), [preferred](const std::uint8_t left,
                                                     const std::uint8_t right) {
        const auto distance = [preferred](const std::uint8_t socket) {
            const std::uint8_t forward{static_cast<std::uint8_t>((socket + 8U - preferred) % 8U)};
            return std::min(forward, static_cast<std::uint8_t>(8U - forward));
        };
        return std::tuple{distance(left), left}
            < std::tuple{distance(right), right};
    });
    for (const std::uint8_t socket : order) {
        if (used[socket]
            || !socket_accepts_vector(
                socket_directions[socket], vectors[index], tangent_limit)) {
            continue;
        }
        const std::int64_t dot{
            static_cast<std::int64_t>(socket_directions[socket].x_millimetres)
                    * vectors[index].x_millimetres
                + static_cast<std::int64_t>(socket_directions[socket].z_millimetres)
                    * vectors[index].z_millimetres};
        const std::int64_t cross{
            static_cast<std::int64_t>(socket_directions[socket].x_millimetres)
                    * vectors[index].z_millimetres
                - static_cast<std::int64_t>(socket_directions[socket].z_millimetres)
                    * vectors[index].x_millimetres};
        const std::uint64_t tangent_score{static_cast<std::uint64_t>(
            (std::max(cross, -cross) * 1'000'000LL) / dot)};
        used[socket] = true;
        current.sockets.push_back(socket);
        current.score += tangent_score;
        enumerate_assignments(
            vectors, index + 1U, tangent_limit, used, current, best);
        current.score -= tangent_score;
        current.sockets.pop_back();
        used[socket] = false;
    }
}

template <typename Value>
void append_bytes(std::uint64_t& hash, Value value)
{
    using Unsigned = std::make_unsigned_t<Value>;
    Unsigned bits{static_cast<Unsigned>(value)};
    for (std::size_t index{}; index < sizeof(Value); ++index) {
        hash ^= static_cast<std::uint8_t>(bits & 0xFFU);
        hash *= fnv_prime;
        bits >>= 8U;
    }
}

void append_polygon(
    std::uint64_t& hash,
    const std::vector<TemplatePoint2>& polygon)
{
    append_bytes(hash, static_cast<std::uint32_t>(polygon.size()));
    for (const TemplatePoint2 point : polygon) {
        append_bytes(hash, point.x_millimetres);
        append_bytes(hash, point.z_millimetres);
    }
}

[[nodiscard]] std::uint64_t structural_hash(const ChamberTemplate& chamber)
{
    std::uint64_t hash{fnv_offset_basis};
    append_bytes(hash, template_schema_version);
    append_bytes(hash, static_cast<std::uint8_t>(chamber.role));
    append_bytes(hash, chamber.outer_width_millimetres);
    append_bytes(hash, chamber.outer_depth_millimetres);
    append_bytes(hash, chamber.usable_diameter_millimetres);
    append_bytes(hash, chamber.usable_height_millimetres);
    append_bytes(hash, static_cast<std::uint32_t>(chamber.floor_patches.size()));
    for (const TemplateFloorPatch& patch : chamber.floor_patches) {
        append_bytes(hash, patch.stable_id);
        append_polygon(hash, patch.clockwise_polygon);
        append_bytes(hash, patch.support_height_millimetres);
        append_bytes(hash, static_cast<std::uint8_t>(patch.surface));
        append_bytes(hash, patch.support_priority);
        append_bytes(hash, static_cast<std::uint8_t>(patch.walkable));
    }
    append_bytes(hash, static_cast<std::uint32_t>(chamber.boundary_patches.size()));
    for (const TemplateBoundaryPatch& patch : chamber.boundary_patches) {
        append_bytes(hash, patch.stable_id);
        append_polygon(hash, patch.clockwise_polygon);
        append_bytes(hash, patch.minimum_y_millimetres);
        append_bytes(hash, patch.maximum_y_millimetres);
        append_bytes(hash, static_cast<std::uint8_t>(patch.capsule_blocking));
        append_bytes(hash, static_cast<std::uint8_t>(patch.structural_line_of_sight));
    }
    append_bytes(hash, static_cast<std::uint32_t>(chamber.hazards.size()));
    for (const TemplateHazardVolume& hazard : chamber.hazards) {
        append_bytes(hash, hazard.stable_id);
        append_polygon(hash, hazard.clockwise_polygon);
        append_bytes(hash, hazard.minimum_y_millimetres);
        append_bytes(hash, hazard.maximum_y_millimetres);
        append_bytes(hash, static_cast<std::uint8_t>(hazard.kind));
        append_bytes(hash, hazard.priority);
    }
    append_bytes(hash, static_cast<std::uint32_t>(chamber.clear_zones.size()));
    for (const TemplateClearZone& zone : chamber.clear_zones) {
        append_bytes(hash, zone.stable_id);
        append_polygon(hash, zone.clockwise_polygon);
        append_bytes(hash, zone.minimum_y_millimetres);
        append_bytes(hash, zone.maximum_y_millimetres);
        append_bytes(hash, static_cast<std::uint8_t>(zone.purpose));
    }
    append_bytes(hash, static_cast<std::uint32_t>(chamber.sockets.size()));
    for (const TemplateSocket& socket : chamber.sockets) {
        append_bytes(hash, socket.index);
        append_bytes(hash, socket.origin_millimetres.x_millimetres);
        append_bytes(hash, socket.origin_millimetres.z_millimetres);
        append_bytes(hash, socket.outward_direction_million.x_millimetres);
        append_bytes(hash, socket.outward_direction_million.z_millimetres);
        append_bytes(hash, socket.arch_width_millimetres);
        append_bytes(hash, socket.arch_height_millimetres);
    }
    append_bytes(hash, static_cast<std::uint32_t>(chamber.navigation_edges.size()));
    for (const TemplateNavigationEdge& edge : chamber.navigation_edges) {
        append_bytes(hash, edge.stable_id);
        append_bytes(hash, edge.from_patch_or_zone_id);
        append_bytes(hash, edge.to_patch_or_zone_id);
        append_bytes(hash, edge.minimum_width_millimetres);
        append_bytes(hash, edge.maximum_step_millimetres);
        append_bytes(hash, edge.maximum_slope_millidegrees);
        append_bytes(hash, edge.maximum_gap_millimetres);
        append_bytes(hash, static_cast<std::uint8_t>(edge.bidirectional));
    }
    append_bytes(hash, static_cast<std::uint32_t>(chamber.anchors.size()));
    for (const TemplateAnchor& anchor : chamber.anchors) {
        append_bytes(hash, anchor.stable_id);
        append_bytes(hash, static_cast<std::uint8_t>(anchor.kind));
        append_bytes(hash, anchor.position_millimetres.x_millimetres);
        append_bytes(hash, anchor.position_millimetres.y_millimetres);
        append_bytes(hash, anchor.position_millimetres.z_millimetres);
        append_bytes(hash, anchor.heading_millidegrees);
    }
    return hash;
}

}  // namespace

ChamberTemplateRole chamber_template_role(const ChamberNode& node)
{
    switch (node.role) {
    case ChamberRole::start:
        return ChamberTemplateRole::start;
    case ChamberRole::exit:
        return ChamberTemplateRole::exit;
    case ChamberRole::neutral:
        return ChamberTemplateRole::neutral;
    case ChamberRole::elemental:
        if (!node.element.has_value()) {
            throw std::invalid_argument{"elemental chamber has no element"};
        }
        switch (*node.element) {
        case Element::fire:
            return ChamberTemplateRole::fire;
        case Element::water:
            return ChamberTemplateRole::water;
        case Element::earth:
            return ChamberTemplateRole::earth;
        case Element::air:
            return ChamberTemplateRole::air;
        case Element::aether:
            return ChamberTemplateRole::aether;
        }
    }
    throw std::invalid_argument{"unknown chamber role"};
}

const ChamberTemplate& chamber_template(const ChamberTemplateRole role)
{
    const auto& templates{template_table()};
    const auto found{std::find_if(templates.begin(), templates.end(),
        [role](const ChamberTemplate& chamber) { return chamber.role == role; })};
    if (found == templates.end()) {
        throw std::invalid_argument{"unknown chamber template role"};
    }
    return *found;
}

const ChamberTemplate& chamber_template(const ChamberNode& node)
{
    return chamber_template(chamber_template_role(node));
}

const std::vector<ChamberTemplate>& chamber_templates()
{
    return template_table();
}

CrystalScaleExtrema crystal_scale_extrema(const CrystalScaleInput& input)
{
    if (input.radial_offsets_millimetres.empty() || input.radius_millimetres <= 0
        || input.height_millimetres <= 0 || input.base_scale_milli == 0U
        || input.minimum_animation_scale_milli == 0U
        || input.maximum_animation_scale_milli < input.minimum_animation_scale_milli) {
        throw std::invalid_argument{"invalid crystal scale input"};
    }
    const std::int32_t maximum_offset{*std::max_element(
        input.radial_offsets_millimetres.begin(),
        input.radial_offsets_millimetres.end())};
    const std::int64_t mesh_radius{
        static_cast<std::int64_t>(input.radius_millimetres) + maximum_offset};
    if (mesh_radius <= 0) {
        throw std::invalid_argument{"crystal maximum mesh radius must be positive"};
    }
    const auto scaled = [&](const std::int64_t measure,
                            const std::uint32_t animation,
                            const bool ceiling) {
        const std::int64_t base{checked_multiply(measure, input.base_scale_milli)};
        const std::int64_t numerator{checked_multiply(base, animation)};
        return checked_narrow(ceiling
                ? positive_ceiling_divide(numerator, 1'000'000)
                : positive_floor_divide(numerator, 1'000'000));
    };
    return {scaled(checked_multiply(2, mesh_radius),
                input.minimum_animation_scale_milli, false),
        scaled(checked_multiply(2, mesh_radius),
            input.maximum_animation_scale_milli, true),
        scaled(input.height_millimetres,
            input.minimum_animation_scale_milli, false),
        scaled(input.height_millimetres,
            input.maximum_animation_scale_milli, true)};
}

std::vector<std::string> validate_chamber_template(const ChamberTemplate& chamber)
{
    std::vector<std::string> errors;
    if (chamber.outer_width_millimetres <= 0 || chamber.outer_depth_millimetres <= 0
        || chamber.usable_diameter_millimetres <= 0
        || chamber.usable_height_millimetres
            < movement_envelope.minimum_clearance_height_millimetres) {
        errors.push_back("template dimensions must be positive and traversable");
    }
    const auto validate_polygon = [&](const std::vector<TemplatePoint2>& polygon,
                                      const char* kind) {
        if (polygon.size() < 3U || signed_twice_area(polygon) >= 0) {
            errors.push_back(std::string{kind} + " polygon must be clockwise and non-degenerate");
        }
    };
    std::uint32_t previous_id{};
    for (const TemplateFloorPatch& patch : chamber.floor_patches) {
        validate_polygon(patch.clockwise_polygon, "floor");
        if (patch.stable_id <= previous_id) {
            errors.push_back("floor patch IDs must be strictly sorted");
        }
        previous_id = patch.stable_id;
    }
    previous_id = 0;
    for (const TemplateBoundaryPatch& patch : chamber.boundary_patches) {
        validate_polygon(patch.clockwise_polygon, "boundary");
        if (patch.stable_id <= previous_id || patch.minimum_y_millimetres
                >= patch.maximum_y_millimetres) {
            errors.push_back("boundary patches must be sorted and vertically valid");
        }
        previous_id = patch.stable_id;
    }
    previous_id = 0;
    for (const TemplateHazardVolume& hazard : chamber.hazards) {
        validate_polygon(hazard.clockwise_polygon, "hazard");
        if (hazard.stable_id <= previous_id
            || hazard.minimum_y_millimetres >= hazard.maximum_y_millimetres) {
            errors.push_back("hazards must be sorted and vertically valid");
        }
        previous_id = hazard.stable_id;
    }
    previous_id = 0;
    for (const TemplateClearZone& zone : chamber.clear_zones) {
        validate_polygon(zone.clockwise_polygon, "clear zone");
        if (zone.stable_id <= previous_id
            || zone.minimum_y_millimetres >= zone.maximum_y_millimetres) {
            errors.push_back("clear zones must be sorted and vertically valid");
        }
        previous_id = zone.stable_id;
        for (const TemplateHazardVolume& hazard : chamber.hazards) {
            if (zone.minimum_y_millimetres < hazard.maximum_y_millimetres
                && hazard.minimum_y_millimetres < zone.maximum_y_millimetres
                && bounds_overlap(polygon_bounds(zone.clockwise_polygon),
                    polygon_bounds(hazard.clockwise_polygon))) {
                errors.push_back("template hazards and clear zones overlap");
            }
        }
    }
    if (chamber.sockets.size() != 8U) {
        errors.push_back("template must declare exactly eight route sockets");
    }
    for (std::size_t index{}; index < chamber.sockets.size(); ++index) {
        const TemplateSocket& socket{chamber.sockets[index]};
        if (socket.index != index || socket.arch_width_millimetres != 3'200
            || socket.arch_height_millimetres != 4'000
            || socket.landing_patch_id == 0U
            || socket.landing_clear_zone_id == 0U) {
            errors.push_back("route socket contract is incomplete or unsorted");
        }
    }
    if (chamber.navigation_edges.empty()
        || std::any_of(chamber.navigation_edges.begin(), chamber.navigation_edges.end(),
            [](const TemplateNavigationEdge& edge) {
                return !edge.bidirectional
                    || edge.minimum_width_millimetres
                        < movement_envelope.minimum_clearance_width_millimetres
                    || edge.maximum_step_millimetres
                        > movement_envelope.step_height_millimetres
                    || edge.maximum_slope_millidegrees
                        > movement_envelope.maximum_slope_millidegrees
                    || edge.maximum_gap_millimetres
                        > movement_envelope.maximum_gap_millimetres;
            })) {
        errors.push_back("template navigation graph violates movement envelope");
    }
    const auto landmark{std::find_if(chamber.anchors.begin(), chamber.anchors.end(),
        [](const TemplateAnchor& anchor) {
            return anchor.kind == TemplateAnchorKind::structural_landmark;
        })};
    if (landmark == chamber.anchors.end()) {
        errors.push_back("template must own a structural landmark anchor");
    }
    return errors;
}

std::vector<std::string> validate_all_chamber_templates()
{
    std::vector<std::string> errors;
    const auto& templates{template_table()};
    if (templates.size() != 8U) {
        errors.push_back("exactly eight chamber templates are required");
    }
    for (const ChamberTemplate& chamber : templates) {
        const std::vector<std::string> chamber_errors{validate_chamber_template(chamber)};
        errors.insert(errors.end(), chamber_errors.begin(), chamber_errors.end());
        std::optional<Element> element;
        switch (chamber.role) {
        case ChamberTemplateRole::fire:
            element = Element::fire;
            break;
        case ChamberTemplateRole::water:
            element = Element::water;
            break;
        case ChamberTemplateRole::earth:
            element = Element::earth;
            break;
        case ChamberTemplateRole::air:
            element = Element::air;
            break;
        case ChamberTemplateRole::aether:
            element = Element::aether;
            break;
        case ChamberTemplateRole::start:
        case ChamberTemplateRole::exit:
        case ChamberTemplateRole::neutral:
            break;
        }
        if (element.has_value()) {
            const ElementalPersona& persona{elemental_persona(*element)};
            const CrystalScaleExtrema extrema{crystal_scale_extrema(
                {persona.crystal_radius_millimetres,
                    persona.crystal_height_millimetres, {20}, 1'000U, 940U,
                    1'080U})};
            const std::int64_t diameter{chamber.usable_diameter_millimetres};
            const std::int64_t height{chamber.usable_height_millimetres};
            if (*element == Element::fire || *element == Element::water
                || *element == Element::earth || *element == Element::air
                || *element == Element::aether) {
                const std::int64_t minimum_height{
                    *element == Element::fire ? 23'000LL
                    : *element == Element::air ? 22'000LL
                    : *element == Element::water ? 14'000LL
                    : *element == Element::aether ? 16'700LL
                                                  : 10'000LL};
                const std::int64_t minimum_diameter{
                    *element == Element::fire ? 55'000LL
                    : *element == Element::water ? 31'000LL
                    : *element == Element::aether ? 44'440LL
                                                  : 40'000LL};
                if (diameter < minimum_diameter || height < minimum_height) {
                    errors.push_back(
                        "authored elemental template does not preserve its full-scale interior");
                }
            } else {
                if (20LL * extrema.maximum_diameter_millimetres > diameter
                    || diameter > 30LL * extrema.minimum_diameter_millimetres) {
                    errors.push_back(
                        "elemental template violates 20-to-30 crystal diameter ratio");
                }
                if (height < std::max<std::int64_t>(
                        4'500LL, 5LL * extrema.maximum_height_millimetres)
                    || height > 8LL * extrema.minimum_height_millimetres) {
                    errors.push_back(
                        "elemental template violates crystal-relative usable height");
                }
            }
        }
    }
    return errors;
}

std::vector<ChamberSocketAssignment> assign_template_sockets(
    const TopologyData& topology)
{
    const auto start{std::find_if(topology.nodes.begin(), topology.nodes.end(),
        [](const ChamberNode& node) { return node.role == ChamberRole::start; })};
    if (start == topology.nodes.end()) {
        throw std::invalid_argument{"socket assignment requires one Start chamber"};
    }
    std::vector<std::uint32_t> distance(topology.nodes.size(),
        std::numeric_limits<std::uint32_t>::max());
    if (start->id.value >= distance.size()) {
        throw std::invalid_argument{"socket assignment requires dense node IDs"};
    }
    std::queue<NodeId> pending;
    distance[start->id.value] = 0U;
    pending.push(start->id);
    while (!pending.empty()) {
        const NodeId current{pending.front()};
        pending.pop();
        for (const NodeId neighbour : sorted_neighbours(topology, current)) {
            if (neighbour.value >= distance.size()) {
                throw std::invalid_argument{"socket assignment references an unknown node"};
            }
            if (distance[neighbour.value] == std::numeric_limits<std::uint32_t>::max()) {
                distance[neighbour.value] = distance[current.value] + 1U;
                pending.push(neighbour);
            }
        }
    }

    std::vector<ChamberSocketAssignment> result;
    result.reserve(topology.nodes.size());
    for (const ChamberNode& node : topology.nodes) {
        const std::vector<NodeId> neighbours{sorted_neighbours(topology, node.id)};
        if (neighbours.empty() || neighbours.size() > 3U) {
            throw std::invalid_argument{"template sockets require chamber degree 1..3"};
        }
        NodeId primary{neighbours.front()};
        if (node.role != ChamberRole::start) {
            const auto predecessor{std::find_if(neighbours.begin(), neighbours.end(),
                [&](const NodeId candidate) {
                    return distance[node.id.value] > 0U
                        && distance[candidate.value] + 1U == distance[node.id.value];
                })};
            if (predecessor == neighbours.end()) {
                throw std::invalid_argument{"chamber has no canonical BFS predecessor"};
            }
            primary = *predecessor;
        }
        const ChamberNode* primary_node{find_node(topology, primary)};
        if (primary_node == nullptr) {
            throw std::invalid_argument{"canonical predecessor is missing"};
        }

        std::vector<std::pair<Edge, TemplatePoint2>> incident;
        for (const NodeId neighbour : neighbours) {
            const ChamberNode* neighbour_node{find_node(topology, neighbour)};
            if (neighbour_node == nullptr) {
                throw std::invalid_argument{"incident edge endpoint is missing"};
            }
            incident.push_back({make_edge(node.id, neighbour),
                vector_between(node, *neighbour_node)});
        }
        std::sort(incident.begin(), incident.end(),
            [](const auto& left, const auto& right) { return left.first < right.first; });
        std::vector<TemplatePoint2> vectors;
        for (const auto& entry : incident) {
            vectors.push_back(entry.second);
        }
        const bool authored_water{
            node.element == std::optional<Element>{Element::water}};
        const bool authored_fire{
            node.element == std::optional<Element>{Element::fire}};
        const bool authored_earth{
            node.element == std::optional<Element>{Element::earth}};
        const bool authored_air{
            node.element == std::optional<Element>{Element::air}};
        const bool authored_aether{
            node.element == std::optional<Element>{Element::aether}};
        const bool authored_start{node.role == ChamberRole::start};
        const bool authored_exit{node.role == ChamberRole::exit};
        const std::int64_t tangent_limit{
            authored_fire
                ? authored_fire_socket_tangent_limit
                : authored_water
                ? authored_water_socket_tangent_limit
                : authored_earth
                    ? authored_earth_socket_tangent_limit
                    : authored_air
                        ? authored_air_socket_tangent_limit
                        : template_socket_tangent_limit};
        if (authored_start || authored_exit) {
            if (incident.size() != 1U) {
                throw std::invalid_argument{authored_start
                        ? "authored Start chamber requires exactly one incident route"
                        : "authored Exit chamber requires exactly one incident route"};
            }
            const std::uint8_t local_door_socket{
                authored_start ? std::uint8_t{6U} : std::uint8_t{2U}};
            std::optional<std::uint8_t> selected_orientation;
            std::uint8_t selected_world_socket{};
            std::uint64_t selected_score{
                std::numeric_limits<std::uint64_t>::max()};
            for (std::uint8_t orientation{};
                 orientation < socket_directions.size(); ++orientation) {
                const std::uint8_t world_socket{static_cast<std::uint8_t>(
                    (local_door_socket + orientation) % 8U)};
                if (!socket_accepts_vector(socket_directions[world_socket],
                        incident.front().second, tangent_limit)) {
                    continue;
                }
                const std::uint64_t score{socket_tangent_score(
                    socket_directions[world_socket], incident.front().second)};
                if (!selected_orientation.has_value()
                    || std::tuple{score, orientation, world_socket}
                        < std::tuple{selected_score, *selected_orientation,
                            selected_world_socket}) {
                    selected_orientation = orientation;
                    selected_world_socket = world_socket;
                    selected_score = score;
                }
            }
            if (!selected_orientation.has_value()) {
                throw std::invalid_argument{authored_start
                        ? "authored Start entrance cannot face its route"
                        : "authored Exit entrance cannot face its route"};
            }
            result.push_back({node.id, *selected_orientation,
                {{incident.front().first, selected_world_socket}}});
            continue;
        }
        if (authored_fire || authored_water || authored_earth
            || authored_aether) {
            if (incident.size() != 2U) {
                throw std::invalid_argument{
                    authored_fire
                        ? "authored Fire chamber requires exactly two incident routes"
                        : authored_water
                            ? "authored Water chamber requires exactly two incident routes"
                            : authored_earth
                                ? "authored Earth chamber requires exactly two incident routes"
                                : "authored Aether chamber requires exactly two incident routes"};
            }
            std::optional<std::uint8_t> selected_orientation;
            std::uint8_t selected_first_socket{};
            std::uint8_t selected_second_socket{};
            std::uint64_t selected_score{
                std::numeric_limits<std::uint64_t>::max()};
            const std::array<std::uint8_t, 2U> local_door_sockets =
                authored_water ? std::array<std::uint8_t, 2U>{4U, 2U}
                : authored_aether ? std::array<std::uint8_t, 2U>{0U, 4U}
                                  : std::array<std::uint8_t, 2U>{0U, 2U};
            for (std::uint8_t orientation{};
                 orientation < socket_directions.size(); ++orientation) {
                for (std::uint8_t permutation{}; permutation < 2U; ++permutation) {
                    const std::uint8_t first_local{
                        local_door_sockets[permutation]};
                    const std::uint8_t second_local{
                        local_door_sockets[1U - permutation]};
                    const std::uint8_t first_world{static_cast<std::uint8_t>(
                        (first_local + orientation) % 8U)};
                    const std::uint8_t second_world{static_cast<std::uint8_t>(
                        (second_local + orientation) % 8U)};
                    if (!socket_accepts_vector(socket_directions[first_world],
                            incident[0].second, tangent_limit)
                        || !socket_accepts_vector(socket_directions[second_world],
                            incident[1].second, tangent_limit)) {
                        continue;
                    }
                    const std::uint64_t score{
                        socket_tangent_score(socket_directions[first_world],
                            incident[0].second)
                        + socket_tangent_score(socket_directions[second_world],
                            incident[1].second)};
                    if (!selected_orientation.has_value()
                        || std::tuple{score, orientation, first_world, second_world}
                            < std::tuple{selected_score, *selected_orientation,
                                selected_first_socket, selected_second_socket}) {
                        selected_orientation = orientation;
                        selected_first_socket = first_world;
                        selected_second_socket = second_world;
                        selected_score = score;
                    }
                }
            }
            if (!selected_orientation.has_value()) {
                throw std::invalid_argument{
                    authored_fire
                        ? "authored Fire gateways cannot face both incident routes"
                        : authored_water
                            ? "authored Water gateways cannot face both incident routes"
                            : authored_earth
                                ? "authored Earth gateways cannot face both incident routes"
                                : "authored Aether gateways cannot face both incident routes"};
            }
            ChamberSocketAssignment assignment{
                node.id, *selected_orientation, {}};
            assignment.incident_edges.push_back(
                {incident[0].first, selected_first_socket});
            assignment.incident_edges.push_back(
                {incident[1].first, selected_second_socket});
            result.push_back(std::move(assignment));
            continue;
        }
        if (authored_air) {
            if (incident.size() != 2U) {
                throw std::invalid_argument{
                    "authored Air chamber requires exactly two incident routes"};
            }
            std::optional<std::uint8_t> selected_axis;
            std::uint64_t selected_score{
                std::numeric_limits<std::uint64_t>::max()};
            for (std::uint8_t axis{}; axis < socket_directions.size(); ++axis) {
                const std::uint8_t opposite{
                    static_cast<std::uint8_t>((axis + 4U) % 8U)};
                if (!socket_accepts_vector(
                        socket_directions[axis], incident[0].second, tangent_limit)
                    || !socket_accepts_vector(
                        socket_directions[opposite], incident[1].second,
                        tangent_limit)) {
                    continue;
                }
                const std::uint64_t score{
                    socket_tangent_score(
                        socket_directions[axis], incident[0].second)
                    + socket_tangent_score(
                        socket_directions[opposite], incident[1].second)};
                if (!selected_axis.has_value() || score < selected_score) {
                    selected_axis = axis;
                    selected_score = score;
                }
            }
            if (!selected_axis.has_value()) {
                throw std::invalid_argument{
                    "authored Air gateways cannot face both incident routes"};
            }
            const std::uint8_t opposite_world_socket{
                static_cast<std::uint8_t>((*selected_axis + 4U) % 8U)};
            ChamberSocketAssignment assignment{
                node.id,
                static_cast<std::uint8_t>((*selected_axis + 6U) % 8U),
                {},
            };
            assignment.incident_edges.push_back(
                {incident[0].first, *selected_axis});
            assignment.incident_edges.push_back(
                {incident[1].first, opposite_world_socket});
            result.push_back(std::move(assignment));
            continue;
        }

        std::array<bool, 8> used{};
        AssignmentCandidate current;
        std::optional<AssignmentCandidate> best;
        enumerate_assignments(vectors, 0U, tangent_limit, used, current, best);
        if (!best.has_value()) {
            std::string detail{
                "no realizable template socket assignment for chamber "
                + std::to_string(node.id.value) + " with vectors"};
            for (const TemplatePoint2 vector : vectors) {
                detail += " (" + std::to_string(vector.x_millimetres)
                    + "," + std::to_string(vector.z_millimetres) + ")";
            }
            throw std::invalid_argument{std::move(detail)};
        }
        ChamberSocketAssignment assignment{node.id,
            closest_octant(vector_between(node, *primary_node)), {}};
        for (std::size_t index{}; index < incident.size(); ++index) {
            assignment.incident_edges.push_back(
                {incident[index].first, best->sockets[index]});
        }
        result.push_back(std::move(assignment));
    }
    return result;
}

std::vector<std::string> validate_template_socket_assignments(
    const TopologyData& topology,
    const std::vector<ChamberSocketAssignment>& assignments)
{
    std::vector<std::string> errors;
    if (assignments.size() != topology.nodes.size()) {
        errors.push_back("every chamber must have one socket assignment");
        return errors;
    }
    for (const ChamberSocketAssignment& assignment : assignments) {
        const ChamberNode* node{find_node(topology, assignment.chamber_id)};
        if (node == nullptr || assignment.orientation_octant >= 8U) {
            errors.push_back("socket assignment references an invalid chamber");
            continue;
        }
        std::array<bool, 8> used{};
        for (const AssignedTemplateSocket& edge_socket : assignment.incident_edges) {
            if (edge_socket.socket_index >= 8U || used[edge_socket.socket_index]) {
                errors.push_back("assigned route sockets must be distinct");
                continue;
            }
            used[edge_socket.socket_index] = true;
            const NodeId neighbour{edge_socket.edge.first == node->id
                    ? edge_socket.edge.second
                    : edge_socket.edge.first};
            const ChamberNode* other{find_node(topology, neighbour)};
            const std::int64_t tangent_limit{
                node->element == std::optional<Element>{Element::fire}
                    ? authored_fire_socket_tangent_limit
                    : node->element == std::optional<Element>{Element::water}
                    ? authored_water_socket_tangent_limit
                    : node->element == std::optional<Element>{Element::earth}
                        ? authored_earth_socket_tangent_limit
                        : node->element == std::optional<Element>{Element::air}
                            ? authored_air_socket_tangent_limit
                            : template_socket_tangent_limit};
            if (other == nullptr || !socket_accepts_vector(
                    socket_directions[edge_socket.socket_index],
                    vector_between(*node, *other), tangent_limit)) {
                errors.push_back("assigned route socket exceeds adapter angle");
            }
        }
        if (assignment.incident_edges.size()
            != sorted_neighbours(topology, node->id).size()) {
            errors.push_back("socket assignment omits an incident edge");
        }
    }
    return errors;
}

std::vector<CompiledChamberTemplate> compile_chamber_templates(
    const TopologyData& topology,
    const std::vector<ChamberSocketAssignment>& assignments)
{
    const std::vector<std::string> assignment_errors{
        validate_template_socket_assignments(topology, assignments)};
    if (!assignment_errors.empty()) {
        throw std::invalid_argument{assignment_errors.front()};
    }
    std::vector<CompiledChamberTemplate> compiled;
    compiled.reserve(topology.nodes.size());
    for (const ChamberNode& node : topology.nodes) {
        const auto assignment{std::find_if(assignments.begin(), assignments.end(),
            [&](const ChamberSocketAssignment& candidate) {
                return candidate.chamber_id == node.id;
            })};
        if (assignment == assignments.end()) {
            throw std::invalid_argument{"compiled chamber is missing socket assignment"};
        }
        const ChamberTemplate& source{chamber_template(node)};
        const auto transform_point = [&](const TemplatePoint2 point) {
            const TemplatePoint2 rotated{
                rotate_template_point(point, assignment->orientation_octant)};
            return TemplatePoint2{
                checked_narrow(static_cast<std::int64_t>(rotated.x_millimetres)
                    + node.anchor.x_millimetres),
                checked_narrow(static_cast<std::int64_t>(rotated.z_millimetres)
                    + node.anchor.z_millimetres)};
        };
        const auto transform_polygon = [&](
                                           const std::vector<TemplatePoint2>& polygon) {
            std::vector<TemplatePoint2> transformed;
            transformed.reserve(polygon.size());
            for (const TemplatePoint2 point : polygon) {
                transformed.push_back(transform_point(point));
            }
            return transformed;
        };
        const auto world_y = [&](const std::int32_t local_y) {
            return checked_narrow(static_cast<std::int64_t>(
                node.anchor.elevation_millimetres) + local_y);
        };
        const auto object_id = [&](const std::uint32_t local_id,
                                   const std::uint8_t kind) {
            return 0x54454D5000000000ULL
                ^ (static_cast<std::uint64_t>(node.id.value) << 32U)
                ^ (static_cast<std::uint64_t>(kind) << 24U) ^ local_id;
        };

        CompiledChamberTemplate chamber;
        chamber.chamber_id = node.id;
        chamber.role = source.role;
        chamber.orientation_octant = assignment->orientation_octant;
        chamber.usable_height_millimetres = source.usable_height_millimetres;
        for (const TemplateFloorPatch& patch : source.floor_patches) {
            chamber.floor_patches.push_back({object_id(patch.stable_id, 1U),
                transform_polygon(patch.clockwise_polygon),
                world_y(patch.support_height_millimetres), patch.surface,
                patch.support_priority, patch.walkable});
        }
        for (const TemplateBoundaryPatch& patch : source.boundary_patches) {
            chamber.boundary_patches.push_back({object_id(patch.stable_id, 2U),
                transform_polygon(patch.clockwise_polygon),
                world_y(patch.minimum_y_millimetres),
                world_y(patch.maximum_y_millimetres), patch.capsule_blocking,
                patch.structural_line_of_sight});
        }
        for (const TemplateHazardVolume& hazard : source.hazards) {
            chamber.hazards.push_back({object_id(hazard.stable_id, 3U),
                transform_polygon(hazard.clockwise_polygon),
                world_y(hazard.minimum_y_millimetres),
                world_y(hazard.maximum_y_millimetres), hazard.kind,
                hazard.priority, hazard.respawn});
        }
        for (const TemplateClearZone& zone : source.clear_zones) {
            chamber.clear_zones.push_back({object_id(zone.stable_id, 4U),
                transform_polygon(zone.clockwise_polygon),
                world_y(zone.minimum_y_millimetres),
                world_y(zone.maximum_y_millimetres), zone.purpose});
        }
        for (const TemplateSocket& socket : source.sockets) {
            const std::uint8_t world_index{static_cast<std::uint8_t>(
                (socket.index + assignment->orientation_octant) % 8U)};
            const auto active{std::find_if(assignment->incident_edges.begin(),
                assignment->incident_edges.end(),
                [world_index](const AssignedTemplateSocket& edge_socket) {
                    return edge_socket.socket_index == world_index;
                })};
            const bool is_active{active != assignment->incident_edges.end()};
            chamber.sockets.push_back({socket.index, world_index,
                transform_point(socket.origin_millimetres),
                rotate_template_point(socket.outward_direction_million,
                    assignment->orientation_octant),
                transform_point(socket.vestibule_inner_millimetres),
                transform_point(socket.vestibule_outer_millimetres),
                is_active,
                !is_active
                    ? std::optional<Edge>{}
                    : std::optional<Edge>{active->edge}});
            if (is_active) {
                const auto landing{std::find_if(source.clear_zones.begin(),
                    source.clear_zones.end(),
                    [&](const TemplateClearZone& zone) {
                        return zone.stable_id == socket.landing_clear_zone_id;
                    })};
                if (landing == source.clear_zones.end()) {
                    throw std::logic_error{
                        "active socket is missing its landing clear zone"};
                }
                chamber.floor_patches.push_back({
                    object_id(landing->stable_id, 1U),
                    transform_polygon(landing->clockwise_polygon),
                    world_y(0),
                    source.floor_patches.front().surface,
                    200U,
                    true,
                });
            }
        }
        chamber.navigation_edges = source.navigation_edges;
        for (const TemplateAnchor& anchor : source.anchors) {
            const TemplatePoint2 planar{transform_point(
                {anchor.position_millimetres.x_millimetres,
                    anchor.position_millimetres.z_millimetres})};
            chamber.anchors.push_back({object_id(anchor.stable_id, 5U), anchor.kind,
                {planar.x_millimetres,
                    world_y(anchor.position_millimetres.y_millimetres),
                    planar.z_millimetres},
                static_cast<std::int32_t>((anchor.heading_millidegrees
                    + assignment->orientation_octant * 45'000)
                    % 360'000)});
        }
        std::uint64_t fingerprint{fnv_offset_basis};
        append_bytes(fingerprint, template_schema_version);
        append_bytes(fingerprint, node.id.value);
        append_bytes(fingerprint, structural_hash(source));
        append_bytes(fingerprint, chamber.orientation_octant);
        for (const CompiledTemplateSocket& socket : chamber.sockets) {
            append_bytes(fingerprint, socket.world_index);
            append_bytes(fingerprint, socket.world_origin_millimetres.x_millimetres);
            append_bytes(fingerprint, socket.world_origin_millimetres.z_millimetres);
            append_bytes(fingerprint, static_cast<std::uint8_t>(socket.active));
        }
        chamber.fingerprint = fingerprint;
        compiled.push_back(std::move(chamber));
    }
    const std::vector<std::string> errors{
        validate_compiled_chamber_templates(topology, compiled)};
    if (!errors.empty()) {
        throw std::invalid_argument{errors.front()};
    }
    return compiled;
}

std::vector<std::string> validate_compiled_chamber_templates(
    const TopologyData& topology,
    const std::vector<CompiledChamberTemplate>& compiled)
{
    std::vector<std::string> errors;
    if (compiled.size() != topology.nodes.size()) {
        errors.push_back("production rooms must compile from every template");
        return errors;
    }
    for (const CompiledChamberTemplate& chamber : compiled) {
        const ChamberNode* node{find_node(topology, chamber.chamber_id)};
        if (node == nullptr || chamber.role != chamber_template_role(*node)
            || chamber.orientation_octant >= 8U || chamber.floor_patches.empty()
            || chamber.sockets.size() != 8U || chamber.anchors.empty()
            || chamber.fingerprint == 0U) {
            errors.push_back("compiled chamber does not match authoritative template");
            continue;
        }
        std::size_t active_count{};
        for (const CompiledTemplateSocket& socket : chamber.sockets) {
            if (socket.active) {
                ++active_count;
                if (!socket.route.has_value()) {
                    errors.push_back("active socket is missing its route");
                }
            } else if (socket.route.has_value()) {
                errors.push_back("sealed socket unexpectedly owns a route");
            }
        }
        if (active_count != sorted_neighbours(topology, chamber.chamber_id).size()) {
            errors.push_back("compiled chamber active socket count changed");
        }
        for (const CompiledTemplateFloorPatch& patch : chamber.floor_patches) {
            if (patch.world_polygon_millimetres.size() < 3U
                || signed_twice_area(patch.world_polygon_millimetres) >= 0) {
                errors.push_back("compiled floor polygon is invalid");
            }
        }
        for (const CompiledTemplateHazardVolume& hazard : chamber.hazards) {
            if (hazard.world_polygon_millimetres.size() < 3U
                || hazard.minimum_y_millimetres >= hazard.maximum_y_millimetres) {
                errors.push_back("compiled hazard is invisible or invalid");
            }
        }
    }
    return errors;
}

TemplatePoint2 rotate_template_point(
    const TemplatePoint2 point,
    const std::uint8_t octant)
{
    if (octant >= 8U) {
        throw std::out_of_range{"template rotation octant must be 0..7"};
    }
    constexpr std::array<std::int64_t, 8> cosine{{
        template_rotation_one_q30, template_rotation_diagonal_q30, 0,
        -template_rotation_diagonal_q30, -template_rotation_one_q30,
        -template_rotation_diagonal_q30, 0, template_rotation_diagonal_q30}};
    constexpr std::array<std::int64_t, 8> sine{{
        0, template_rotation_diagonal_q30, template_rotation_one_q30,
        template_rotation_diagonal_q30, 0, -template_rotation_diagonal_q30,
        -template_rotation_one_q30, -template_rotation_diagonal_q30}};
    const std::int64_t x{checked_multiply(point.x_millimetres, cosine[octant])
        - checked_multiply(point.z_millimetres, sine[octant])};
    const std::int64_t z{checked_multiply(point.x_millimetres, sine[octant])
        + checked_multiply(point.z_millimetres, cosine[octant])};
    return {checked_narrow(round_half_away_from_zero_q30(x)),
        checked_narrow(round_half_away_from_zero_q30(z))};
}

bool template_socket_accepts_vector(
    const TemplatePoint2 socket_direction_million,
    const TemplatePoint2 vector_millimetres) noexcept
{
    return socket_accepts_vector(socket_direction_million, vector_millimetres,
        template_socket_tangent_limit);
}

std::uint64_t template_gameplay_fingerprint(
    const TopologyData& topology,
    const std::vector<ChamberSocketAssignment>& assignments)
{
    std::uint64_t hash{fnv_offset_basis};
    append_bytes(hash, template_schema_version);
    for (const ChamberNode& node : topology.nodes) {
        append_bytes(hash, node.id.value);
        append_bytes(hash, structural_hash(chamber_template(node)));
        append_bytes(hash, node.anchor.x_millimetres);
        append_bytes(hash, node.anchor.elevation_millimetres);
        append_bytes(hash, node.anchor.z_millimetres);
    }
    for (const ChamberSocketAssignment& assignment : assignments) {
        append_bytes(hash, assignment.chamber_id.value);
        append_bytes(hash, assignment.orientation_octant);
        for (const AssignedTemplateSocket& edge_socket : assignment.incident_edges) {
            append_bytes(hash, edge_socket.edge.first.value);
            append_bytes(hash, edge_socket.edge.second.value);
            append_bytes(hash, edge_socket.socket_index);
        }
    }
    return hash;
}

std::uint64_t chamber_template_structural_signature(const ChamberTemplate& chamber)
{
    return structural_hash(chamber);
}

}  // namespace crystalbound
