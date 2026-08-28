#include "crystalbound/AuthoredChamber.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace crystalbound {
namespace {

constexpr double pi{3.14159265358979323846};
constexpr double blocker_expansion_metres{0.45};
constexpr double water_entrance_collision_overlap_metres{0.04};
constexpr double water_collision_ceiling_height_metres{14.5};
constexpr double fire_collision_ceiling_height_metres{23.0};
constexpr double earth_collision_ceiling_height_metres{13.0};
constexpr double air_collision_ceiling_height_metres{24.0};
constexpr double aether_collision_ceiling_height_metres{16.7};
constexpr double start_collision_ceiling_height_metres{8.0};
constexpr double exit_collision_ceiling_height_metres{16.0};
constexpr double air_floor_radius_metres{20.0};
constexpr double air_floor_diagonal_metres{14.142136};
constexpr std::uint64_t authored_collision_domain{0x4155'5448'434F'4C4CULL};
constexpr std::uint64_t fnv_offset_basis{14695981039346656037ULL};
constexpr std::uint64_t fnv_prime{1099511628211ULL};
constexpr std::array<std::pair<std::string_view, std::string_view>, 12U>
    water_solid_stair_bodies{{
        {"ENTRANCE_WEST_Landing", "ENTRANCE_WEST_WaterStep1"},
        {"ENTRANCE_WEST_WaterStep1", "ENTRANCE_WEST_WaterStep2"},
        {"ENTRANCE_WEST_WaterStep2", "ENTRANCE_WEST_WaterStep3"},
        {"ENTRANCE_WEST_WaterStep3", "ENTRANCE_WEST_WaterStep4"},
        {"ENTRANCE_WEST_WaterStep4", "ENTRANCE_WEST_WaterStep5"},
        {"ENTRANCE_WEST_WaterStep5", "ENTRANCE_WEST_WaterStep6"},
        {"ENTRANCE_SOUTH_Landing", "ENTRANCE_SOUTH_WaterStep1"},
        {"ENTRANCE_SOUTH_WaterStep1", "ENTRANCE_SOUTH_WaterStep2"},
        {"ENTRANCE_SOUTH_WaterStep2", "ENTRANCE_SOUTH_WaterStep3"},
        {"ENTRANCE_SOUTH_WaterStep3", "ENTRANCE_SOUTH_WaterStep4"},
        {"ENTRANCE_SOUTH_WaterStep4", "ENTRANCE_SOUTH_WaterStep5"},
        {"ENTRANCE_SOUTH_WaterStep5", "ENTRANCE_SOUTH_WaterStep6"},
    }};

struct SourceBounds {
    std::array<double, 3> minimum{
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),
    };
    std::array<double, 3> maximum{
        -minimum[0], -minimum[1], -minimum[2]};
};

[[nodiscard]] bool begins_with(
    const std::string_view value,
    const std::string_view prefix) noexcept
{
    return value.size() >= prefix.size()
        && value.substr(0, prefix.size()) == prefix;
}

[[nodiscard]] std::optional<std::string> numbered_group(
    const std::string_view name,
    const std::string_view prefix,
    const std::string_view group_prefix)
{
    if (!begins_with(name, prefix)) {
        return std::nullopt;
    }
    std::size_t end{prefix.size()};
    while (end < name.size() && name[end] >= '0' && name[end] <= '9') {
        ++end;
    }
    if (end == prefix.size()) {
        return std::nullopt;
    }
    return std::string{group_prefix} + std::string{name.substr(prefix.size(), end - prefix.size())};
}

[[nodiscard]] std::optional<std::string> water_blocker_group(
    const std::string_view name)
{
    if (const auto pillar{numbered_group(name, "Pillar", "Pillar")};
        pillar.has_value()) {
        return pillar;
    }
    if (const auto shrine_column{
            numbered_group(name, "ShrineColumn", "ShrineColumn")};
        shrine_column.has_value()) {
        return shrine_column;
    }
    if (begins_with(name, "EmptyCrystalPedestal")) {
        return std::string{"EmptyCrystalPedestal"};
    }
    if (name == "NorthWall" || name == "EastWall"
        || begins_with(name, "WestWall")
        || begins_with(name, "SouthWall")
        || (begins_with(name, "ENTRANCE_WEST_PassageSide"))
        || (begins_with(name, "ENTRANCE_SOUTH_PassageSide"))) {
        return std::string{name};
    }
    return std::nullopt;
}

[[nodiscard]] bool water_walkable_object(const std::string_view name) noexcept
{
    return name == "ContinuousCollisionFloor"
        || ((begins_with(name, "ENTRANCE_WEST_")
                || begins_with(name, "ENTRANCE_SOUTH_"))
            && (name.find("Landing") != std::string_view::npos
                || name.find("WaterStep") != std::string_view::npos))
        || name == "ShrineStepLower"
        || name == "ShrineStepUpper"
        || name == "ShrinePlatform";
}

[[nodiscard]] bool earth_solid_object(const std::string_view name) noexcept
{
    const bool gateway_structure{
        begins_with(name, "Gate0_") || begins_with(name, "Gate1_")};
    return (gateway_structure
               && (name.find("_Upright") != std::string_view::npos
                   || name.find("_Lintel") != std::string_view::npos
                   || name.find("_Passage_") != std::string_view::npos
                   || name.find("_Roof_") != std::string_view::npos))
        || begins_with(name, "Monolith")
        || begins_with(name, "MidStone")
        || begins_with(name, "Boulder")
        || begins_with(name, "Fallen")
        || begins_with(name, "Upright")
        || name == "Capstone"
        || name == "Plinth";
}

[[nodiscard]] std::optional<std::string> earth_walkable_group(
    const std::string_view name)
{
    if (name == "FloorPlate") {
        return std::string{"FloorPlate"};
    }
    if (begins_with(name, "Gate0_Floor_")) {
        return std::string{"Gate0_Floor"};
    }
    if (begins_with(name, "Gate1_Floor_")) {
        return std::string{"Gate1_Floor"};
    }
    return std::nullopt;
}

[[nodiscard]] bool air_walkable_object(const std::string_view name) noexcept
{
    return name == "FloorPlate"
        || (begins_with(name, "DOOR_A_")
            || begins_with(name, "DOOR_B_"))
            && (name.find("Step") != std::string_view::npos
                || name.find("PassageFloor") != std::string_view::npos);
}

[[nodiscard]] bool air_solid_object(const std::string_view name) noexcept
{
    if (begins_with(name, "Boulder") || begins_with(name, "Trunk")) {
        return true;
    }
    return (begins_with(name, "DOOR_A_") || begins_with(name, "DOOR_B_"))
        && (name.find("Column") != std::string_view::npos
            || name.find("Pedestal") != std::string_view::npos);
}

[[nodiscard]] std::optional<std::string> fire_walkable_group(
    const std::string_view name)
{
    if (name == "EntranceLanding0" || name == "EntranceLanding0_Inner") {
        return std::string{"EntranceLanding0"};
    }
    if (name == "EntranceLanding1" || name == "EntranceLanding1_Inner") {
        return std::string{"EntranceLanding1"};
    }
    if (begins_with(name, "Tunnel0_Floor")) {
        return std::string{"Tunnel0_Floor"};
    }
    if (begins_with(name, "Tunnel1_Floor")) {
        return std::string{"Tunnel1_Floor"};
    }
    if (begins_with(name, "Route0_Stone")
        || begins_with(name, "Route1_Stone")
        || begins_with(name, "FloatingCrust")
        || name == "CentralVolcanicIsland"
        || name == "SocketAltarLower"
        || name == "SocketAltarUpper") {
        return std::string{name};
    }
    return std::nullopt;
}

[[nodiscard]] bool fire_solid_object(const std::string_view name) noexcept
{
    return begins_with(name, "RuptureGate")
        || (begins_with(name, "Tunnel0_")
            && !begins_with(name, "Tunnel0_Floor"))
        || (begins_with(name, "Tunnel1_")
            && !begins_with(name, "Tunnel1_Floor"))
        || begins_with(name, "BasaltColumn")
        || begins_with(name, "VolcanicRib")
        || begins_with(name, "ObsidianCrown")
        || begins_with(name, "ObsidianShard");
}

void include_bounds(SourceBounds& target, const MaterialModelObject& object)
{
    for (std::size_t axis{}; axis < 3U; ++axis) {
        target.minimum[axis] = std::min(
            target.minimum[axis], static_cast<double>(object.minimum_bounds[axis]));
        target.maximum[axis] = std::max(
            target.maximum[axis], static_cast<double>(object.maximum_bounds[axis]));
    }
}

[[nodiscard]] SourceBounds water_support_collision_bounds(
    const std::string_view name,
    const SourceBounds& bounds,
    const std::map<std::string, SourceBounds>& all_supports)
{
    SourceBounds result{bounds};
    if (name == "ENTRANCE_WEST_WaterStep1") {
        const auto landing{all_supports.find("ENTRANCE_WEST_Landing")};
        if (landing != all_supports.end()) {
            result.minimum[0] = std::min(result.minimum[0],
                landing->second.maximum[0]
                    - water_entrance_collision_overlap_metres);
        }
    } else if (name == "ENTRANCE_SOUTH_WaterStep1") {
        const auto landing{all_supports.find("ENTRANCE_SOUTH_Landing")};
        if (landing != all_supports.end()) {
            result.maximum[2] = std::max(result.maximum[2],
                landing->second.minimum[2]
                    + water_entrance_collision_overlap_metres);
        }
    }
    return result;
}

[[nodiscard]] SourceBounds water_stair_body_collision_bounds(
    const std::string_view upper_name,
    const SourceBounds& upper,
    const SourceBounds& lower)
{
    SourceBounds result{upper};
    if (upper_name == "ENTRANCE_WEST_Landing") {
        result.maximum[0] = std::max(result.maximum[0],
            lower.minimum[0] + water_entrance_collision_overlap_metres);
    } else if (upper_name == "ENTRANCE_SOUTH_Landing") {
        result.minimum[2] = std::min(result.minimum[2],
            lower.maximum[2] - water_entrance_collision_overlap_metres);
    }
    return result;
}

[[nodiscard]] std::uint64_t stable_name_id(const std::string_view name) noexcept
{
    std::uint64_t hash{fnv_offset_basis};
    for (const char character : name) {
        hash ^= static_cast<std::uint8_t>(character);
        hash *= fnv_prime;
    }
    return hash ^ authored_collision_domain;
}

[[nodiscard]] TemplatePoint2 transform_planar_point(
    const double source_x,
    const double source_z,
    const AuthoredChamberPlacement& placement)
{
    const double x{source_x * placement.scale.x};
    const double z{source_z * placement.scale.z};
    const double cosine{std::cos(placement.yaw_radians)};
    const double sine{std::sin(placement.yaw_radians)};
    const double world_x{placement.translation_metres.x + x * cosine + z * sine};
    const double world_z{placement.translation_metres.z - x * sine + z * cosine};
    return {
        static_cast<std::int32_t>(std::llround(world_x * 1'000.0)),
        static_cast<std::int32_t>(std::llround(world_z * 1'000.0)),
    };
}

[[nodiscard]] std::vector<TemplatePoint2> transformed_air_floor(
    const AuthoredChamberPlacement& placement)
{
    const std::array<std::array<double, 2>, 8> local{{
        {-air_floor_radius_metres, 0.0},
        {-air_floor_diagonal_metres, air_floor_diagonal_metres},
        {0.0, air_floor_radius_metres},
        {air_floor_diagonal_metres, air_floor_diagonal_metres},
        {air_floor_radius_metres, 0.0},
        {air_floor_diagonal_metres, -air_floor_diagonal_metres},
        {0.0, -air_floor_radius_metres},
        {-air_floor_diagonal_metres, -air_floor_diagonal_metres},
    }};
    std::vector<TemplatePoint2> result;
    result.reserve(local.size());
    for (const auto& point : local) {
        result.push_back(
            transform_planar_point(point[0], point[1], placement));
    }
    return result;
}

[[nodiscard]] std::vector<TemplatePoint2> transformed_rectangle(
    const SourceBounds& bounds,
    const AuthoredChamberPlacement& placement,
    const double expansion_metres)
{
    const double minimum_x{bounds.minimum[0] - expansion_metres};
    const double maximum_x{bounds.maximum[0] + expansion_metres};
    const double minimum_z{bounds.minimum[2] - expansion_metres};
    const double maximum_z{bounds.maximum[2] + expansion_metres};
    return {
        transform_planar_point(minimum_x, minimum_z, placement),
        transform_planar_point(minimum_x, maximum_z, placement),
        transform_planar_point(maximum_x, maximum_z, placement),
        transform_planar_point(maximum_x, minimum_z, placement),
    };
}

[[nodiscard]] double transform_y(
    const double source_y,
    const AuthoredChamberPlacement& placement) noexcept
{
    return placement.translation_metres.y + source_y * placement.scale.y;
}

[[nodiscard]] AuthoredChamberPlacement authored_placement(
    const CaveSceneData& scene,
    const ChamberTemplateRole role,
    const double vertical_offset_metres)
{
    const auto compiled{std::find_if(scene.compiled_chambers.begin(),
        scene.compiled_chambers.end(), [role](const CompiledChamberTemplate& chamber) {
            return chamber.role == role;
        })};
    if (compiled == scene.compiled_chambers.end()) {
        throw std::invalid_argument{"Scene has no requested authored chamber template."};
    }
    const auto chamber{std::find_if(scene.chambers.begin(), scene.chambers.end(),
        [compiled](const ChamberGeometryContract& candidate) {
            return candidate.node_id == compiled->chamber_id;
        })};
    if (chamber == scene.chambers.end()) {
        throw std::invalid_argument{"Scene has no requested authored chamber geometry."};
    }
    return {
        chamber->node_id,
        {
            chamber->center_millimetres.x_millimetres / 1'000.0,
            chamber->center_millimetres.y_millimetres / 1'000.0
                + vertical_offset_metres,
            chamber->center_millimetres.z_millimetres / 1'000.0,
        },
        -static_cast<double>(compiled->orientation_octant) * pi / 4.0,
        {1.0, 1.0, 1.0},
    };
}

[[nodiscard]] std::optional<std::string> start_walkable_group(
    const std::string_view name)
{
    if (name == "WornOvalFloor" || name == "TunnelCollarFloor") {
        return std::string{name};
    }
    return std::nullopt;
}

[[nodiscard]] bool start_solid_object(const std::string_view name) noexcept
{
    return begins_with(name, "WallFormation")
        || begins_with(name, "TunnelCollarWall")
        || begins_with(name, "TunnelArchPier")
        || begins_with(name, "LockedGate")
        || name == "DecorativeLockedBronzeGate";
}

[[nodiscard]] std::optional<std::string> aether_walkable_group(
    const std::string_view name)
{
    if (name == "CrystalFloorBase" || name == "MainCauseway"
        || name == "Portal1_PassageFloor") {
        return std::string{name};
    }
    return std::nullopt;
}

[[nodiscard]] bool aether_solid_object(const std::string_view name) noexcept
{
    return begins_with(name, "Portal1_PassageWall")
        || begins_with(name, "Portal1_Side")
        || name.find("Nexus") != std::string_view::npos
        || name.find("Pillar") != std::string_view::npos
        || name.find("Obelisk") != std::string_view::npos;
}

[[nodiscard]] std::optional<std::string> exit_walkable_group(
    const std::string_view name)
{
    if (name == "FloorFoundation" || name == "EntranceCorridorFloor"
        || name == "PortalLanding" || begins_with(name, "PortalStep_")) {
        return std::string{name};
    }
    return std::nullopt;
}

[[nodiscard]] bool exit_solid_object(const std::string_view name) noexcept
{
    return name == "WestWall" || name == "EastWall" || name == "PortalWall"
        || begins_with(name, "EntranceWall")
        || begins_with(name, "EntranceCorridorWall")
        || begins_with(name, "StairSideWall")
        || begins_with(name, "Statue") || begins_with(name, "Chest")
        || begins_with(name, "ArchStonePier");
}

template <typename WalkableGroup, typename SolidPredicate>
[[nodiscard]] AuthoredChamberCollisionContract build_simple_authored_collision(
    const MaterialModelLoadResult& model,
    const AuthoredChamberPlacement& placement,
    const double ceiling_height_metres,
    WalkableGroup walkable_group,
    SolidPredicate solid_predicate)
{
    if (model.objects.empty() || !std::isfinite(placement.yaw_radians)
        || !std::isfinite(placement.translation_metres.x)
        || !std::isfinite(placement.translation_metres.y)
        || !std::isfinite(placement.translation_metres.z)
        || placement.scale.x <= 0.0 || placement.scale.y <= 0.0
        || placement.scale.z <= 0.0 || ceiling_height_metres <= 0.0) {
        throw std::invalid_argument{
            "Authored collision requires valid objects and a 1:1 placement."};
    }
    std::map<std::string, SourceBounds> supports;
    std::map<std::string, SourceBounds> blockers;
    for (const MaterialModelObject& object : model.objects) {
        if (const auto group{walkable_group(object.name)}; group.has_value()) {
            include_bounds(supports[*group], object);
        } else if (solid_predicate(object.name)) {
            include_bounds(blockers[object.name], object);
        }
    }
    if (supports.empty()) {
        throw std::invalid_argument{"Authored chamber has no walkable support geometry."};
    }
    AuthoredChamberCollisionContract contract;
    for (const auto& [name, bounds] : supports) {
        const double floor_height{transform_y(bounds.maximum[1], placement)};
        contract.supports.push_back({placement.chamber_id, stable_name_id(name),
            transformed_rectangle(bounds, placement, 0.0), floor_height,
            floor_height + ceiling_height_metres, 800U});
    }
    for (const auto& [name, bounds] : blockers) {
        contract.blockers.push_back({stable_name_id(name),
            transformed_rectangle(bounds, placement, 0.0),
            transform_y(bounds.minimum[1], placement),
            transform_y(bounds.maximum[1], placement)});
    }
    return contract;
}

}  // namespace

MaterialModelLoadResult load_water_chamber_render_asset(
    const std::filesystem::path& path)
{
    return load_obj_material_batches(path, {{
        "ENTRANCE_WEST_DarkPassage",
        "ENTRANCE_SOUTH_DarkPassage",
    }});
}

const std::vector<std::string>& air_render_excluded_object_names()
{
    static const std::vector<std::string> names{
        "COLLISION_SHELL_PREVIEW_ONLY",
        "DOOR_A_DarkPassage", "DOOR_B_DarkPassage",
        "Objective0", "Objective1", "Objective2", "Objective3", "Objective4"};
    return names;
}

const std::vector<std::string>& exit_render_excluded_object_names()
{
    static const std::vector<std::string> names = [] {
        std::vector<std::string> result{"ACTIVATED_PORTAL_SURFACE"};
        for (std::uint32_t index{}; index < 33U; ++index) {
            std::string name{"PortalParticle_"};
            if (index < 10U) {
                name += '0';
            }
            name += std::to_string(index);
            result.push_back(std::move(name));
        }
        return result;
    }();
    return names;
}

AuthoredChamberPlacement water_chamber_placement(const CaveSceneData& scene)
{
    const auto compiled{std::find_if(
        scene.compiled_chambers.begin(), scene.compiled_chambers.end(),
        [](const CompiledChamberTemplate& chamber) {
            return chamber.role == ChamberTemplateRole::water;
        })};
    if (compiled == scene.compiled_chambers.end()) {
        throw std::invalid_argument{"Scene has no Water chamber template."};
    }
    const auto chamber{std::find_if(
        scene.chambers.begin(), scene.chambers.end(),
        [compiled](const ChamberGeometryContract& candidate) {
            return candidate.node_id == compiled->chamber_id;
        })};
    if (chamber == scene.chambers.end()) {
        throw std::invalid_argument{"Scene has no Water chamber geometry."};
    }
    return {
        chamber->node_id,
        {
            chamber->center_millimetres.x_millimetres / 1'000.0,
            chamber->center_millimetres.y_millimetres / 1'000.0
                + authored_water_vertical_offset_metres,
            chamber->center_millimetres.z_millimetres / 1'000.0,
        },
        -static_cast<double>(compiled->orientation_octant) * pi / 4.0,
        {1.0, 1.0, 1.0},
    };
}

AuthoredChamberPlacement fire_chamber_placement(const CaveSceneData& scene)
{
    const auto compiled{std::find_if(
        scene.compiled_chambers.begin(), scene.compiled_chambers.end(),
        [](const CompiledChamberTemplate& chamber) {
            return chamber.role == ChamberTemplateRole::fire;
        })};
    if (compiled == scene.compiled_chambers.end()) {
        throw std::invalid_argument{"Scene has no Fire chamber template."};
    }
    const auto chamber{std::find_if(
        scene.chambers.begin(), scene.chambers.end(),
        [compiled](const ChamberGeometryContract& candidate) {
            return candidate.node_id == compiled->chamber_id;
        })};
    if (chamber == scene.chambers.end()) {
        throw std::invalid_argument{"Scene has no Fire chamber geometry."};
    }
    return {
        chamber->node_id,
        {
            chamber->center_millimetres.x_millimetres / 1'000.0,
            chamber->center_millimetres.y_millimetres / 1'000.0
                + authored_fire_vertical_offset_metres,
            chamber->center_millimetres.z_millimetres / 1'000.0,
        },
        -static_cast<double>(compiled->orientation_octant) * pi / 4.0,
        {1.0, 1.0, 1.0},
    };
}

AuthoredChamberPlacement earth_chamber_placement(const CaveSceneData& scene)
{
    const auto compiled{std::find_if(
        scene.compiled_chambers.begin(), scene.compiled_chambers.end(),
        [](const CompiledChamberTemplate& chamber) {
            return chamber.role == ChamberTemplateRole::earth;
        })};
    if (compiled == scene.compiled_chambers.end()) {
        throw std::invalid_argument{"Scene has no Earth chamber template."};
    }
    const auto chamber{std::find_if(
        scene.chambers.begin(), scene.chambers.end(),
        [compiled](const ChamberGeometryContract& candidate) {
            return candidate.node_id == compiled->chamber_id;
        })};
    if (chamber == scene.chambers.end()) {
        throw std::invalid_argument{"Scene has no Earth chamber geometry."};
    }
    return {
        chamber->node_id,
        {
            chamber->center_millimetres.x_millimetres / 1'000.0,
            chamber->center_millimetres.y_millimetres / 1'000.0
                + authored_earth_vertical_offset_metres,
            chamber->center_millimetres.z_millimetres / 1'000.0,
        },
        -static_cast<double>(compiled->orientation_octant) * pi / 4.0,
        {1.0, 1.0, 1.0},
    };
}

AuthoredChamberPlacement air_chamber_placement(const CaveSceneData& scene)
{
    const auto compiled{std::find_if(
        scene.compiled_chambers.begin(), scene.compiled_chambers.end(),
        [](const CompiledChamberTemplate& chamber) {
            return chamber.role == ChamberTemplateRole::air;
        })};
    if (compiled == scene.compiled_chambers.end()) {
        throw std::invalid_argument{"Scene has no Air chamber template."};
    }
    const auto chamber{std::find_if(
        scene.chambers.begin(), scene.chambers.end(),
        [compiled](const ChamberGeometryContract& candidate) {
            return candidate.node_id == compiled->chamber_id;
        })};
    if (chamber == scene.chambers.end()) {
        throw std::invalid_argument{"Scene has no Air chamber geometry."};
    }
    return {
        chamber->node_id,
        {
            chamber->center_millimetres.x_millimetres / 1'000.0,
            chamber->center_millimetres.y_millimetres / 1'000.0
                + authored_air_vertical_offset_metres,
            chamber->center_millimetres.z_millimetres / 1'000.0,
        },
        -static_cast<double>(compiled->orientation_octant) * pi / 4.0,
        {1.0, 1.0, 1.0},
    };
}

AuthoredChamberPlacement aether_chamber_placement(const CaveSceneData& scene)
{
    return authored_placement(scene, ChamberTemplateRole::aether,
        authored_aether_vertical_offset_metres);
}

AuthoredChamberPlacement start_chamber_placement(const CaveSceneData& scene)
{
    return authored_placement(scene, ChamberTemplateRole::start,
        authored_start_vertical_offset_metres);
}

AuthoredChamberPlacement exit_chamber_placement(const CaveSceneData& scene)
{
    return authored_placement(scene, ChamberTemplateRole::exit,
        authored_exit_vertical_offset_metres);
}

AuthoredChamberCollisionContract build_water_chamber_collision(
    const MaterialModelLoadResult& model,
    const AuthoredChamberPlacement& placement)
{
    if (model.objects.empty() || !std::isfinite(placement.yaw_radians)
        || !std::isfinite(placement.translation_metres.x)
        || !std::isfinite(placement.translation_metres.y)
        || !std::isfinite(placement.translation_metres.z)
        || placement.scale.x <= 0.0 || placement.scale.y <= 0.0
        || placement.scale.z <= 0.0) {
        throw std::invalid_argument{"Authored Water collision requires valid objects and placement."};
    }

    AuthoredChamberCollisionContract contract;
    std::map<std::string, SourceBounds> support_bounds;
    std::map<std::string, SourceBounds> blocker_bounds;
    for (const MaterialModelObject& object : model.objects) {
        if (water_walkable_object(object.name)) {
            include_bounds(support_bounds[object.name], object);
        } else if (const auto group{water_blocker_group(object.name)};
                   group.has_value()) {
            include_bounds(blocker_bounds[*group], object);
        }
    }
    if (support_bounds.find("ContinuousCollisionFloor")
        == support_bounds.end()) {
        throw std::invalid_argument{
            "Authored Water chamber has no ContinuousCollisionFloor."};
    }
    for (const auto& [name, bounds] : support_bounds) {
        const SourceBounds collision_bounds{
            water_support_collision_bounds(name, bounds, support_bounds)};
        const double floor_height{
            transform_y(bounds.maximum[1], placement)};
        contract.supports.push_back({
            placement.chamber_id,
            stable_name_id(name),
            transformed_rectangle(collision_bounds, placement, 0.0),
            floor_height,
            floor_height + water_collision_ceiling_height_metres,
            name == "ContinuousCollisionFloor" ? 700U : 720U,
        });
    }
    for (const auto& [name, bounds] : blocker_bounds) {
        contract.blockers.push_back({
            stable_name_id(name),
            transformed_rectangle(bounds, placement, blocker_expansion_metres),
            transform_y(bounds.minimum[1], placement),
            transform_y(bounds.maximum[1], placement),
        });
    }
    for (const auto& [upper_name, lower_name] : water_solid_stair_bodies) {
        const auto upper{support_bounds.find(std::string{upper_name})};
        const auto lower{support_bounds.find(std::string{lower_name})};
        if (upper == support_bounds.end() || lower == support_bounds.end()) {
            continue;
        }
        const double minimum_height{
            transform_y(upper->second.minimum[1], placement)};
        const double maximum_height{
            transform_y(lower->second.maximum[1], placement)};
        if (minimum_height >= maximum_height) {
            throw std::invalid_argument{
                "Authored Water stair body has invalid vertical bounds."};
        }
        const std::string solid_name{std::string{upper_name} + "_SolidBody"};
        const SourceBounds solid_bounds{water_stair_body_collision_bounds(
            upper_name, upper->second, lower->second)};
        contract.blockers.push_back({
            stable_name_id(solid_name),
            transformed_rectangle(solid_bounds, placement, 0.0),
            minimum_height,
            maximum_height,
            true,
        });
    }
    return contract;
}

AuthoredChamberCollisionContract build_fire_chamber_collision(
    const MaterialModelLoadResult& model,
    const AuthoredChamberPlacement& placement)
{
    if (model.objects.empty() || !std::isfinite(placement.yaw_radians)
        || !std::isfinite(placement.translation_metres.x)
        || !std::isfinite(placement.translation_metres.y)
        || !std::isfinite(placement.translation_metres.z)
        || placement.scale.x <= 0.0 || placement.scale.y <= 0.0
        || placement.scale.z <= 0.0) {
        throw std::invalid_argument{
            "Authored Fire collision requires valid objects and placement."};
    }
    std::map<std::string, SourceBounds> support_bounds;
    std::map<std::string, SourceBounds> blocker_bounds;
    for (const MaterialModelObject& object : model.objects) {
        if (const auto group{fire_walkable_group(object.name)};
            group.has_value()) {
            include_bounds(support_bounds[*group], object);
        } else if (fire_solid_object(object.name)) {
            include_bounds(blocker_bounds[object.name], object);
        }
    }
    for (const std::string_view required : {
             "EntranceLanding0", "EntranceLanding1",
             "Tunnel0_Floor", "Tunnel1_Floor",
             "CentralVolcanicIsland"}) {
        if (support_bounds.find(std::string{required}) == support_bounds.end()) {
            throw std::invalid_argument{
                "Authored Fire chamber is missing required walkable geometry."};
        }
    }

    AuthoredChamberCollisionContract contract;
    for (const auto& [name, bounds] : support_bounds) {
        const double floor_height{transform_y(bounds.maximum[1], placement)};
        contract.supports.push_back({
            placement.chamber_id,
            stable_name_id(name),
            transformed_rectangle(bounds, placement, 0.0),
            floor_height,
            floor_height + fire_collision_ceiling_height_metres,
            begins_with(name, "EntranceLanding")
                    || begins_with(name, "Tunnel")
                ? 760U : 740U,
        });
    }
    for (const auto& [name, bounds] : blocker_bounds) {
        contract.blockers.push_back({
            stable_name_id(name),
            transformed_rectangle(bounds, placement, blocker_expansion_metres),
            transform_y(bounds.minimum[1], placement),
            transform_y(bounds.maximum[1], placement),
        });
    }
    const auto respawn = [&](const std::string_view name,
                             const double local_x,
                             const double local_y,
                             const double local_z) {
        const TemplatePoint2 planar{
            transform_planar_point(local_x, local_z, placement)};
        contract.respawns.push_back({
            placement.chamber_id,
            stable_name_id(name),
            {
                planar.x_millimetres / 1'000.0,
                transform_y(local_y, placement),
                planar.z_millimetres / 1'000.0,
            },
        });
    };
    respawn("FireEntrance0Respawn", 27.0, 1.001106, 0.0);
    respawn("FireEntrance1Respawn", 0.0, 1.001394, 27.0);
    return contract;
}

AuthoredChamberCollisionContract build_earth_chamber_collision(
    const MaterialModelLoadResult& model,
    const AuthoredChamberPlacement& placement)
{
    if (model.objects.empty() || !std::isfinite(placement.yaw_radians)
        || !std::isfinite(placement.translation_metres.x)
        || !std::isfinite(placement.translation_metres.y)
        || !std::isfinite(placement.translation_metres.z)
        || placement.scale.x <= 0.0 || placement.scale.y <= 0.0
        || placement.scale.z <= 0.0) {
        throw std::invalid_argument{
            "Authored Earth collision requires valid objects and placement."};
    }

    std::map<std::string, SourceBounds> support_bounds;
    std::map<std::string, SourceBounds> blocker_bounds;
    for (const MaterialModelObject& object : model.objects) {
        if (const auto group{earth_walkable_group(object.name)};
            group.has_value()) {
            include_bounds(support_bounds[*group], object);
        } else if (earth_solid_object(object.name)) {
            include_bounds(blocker_bounds[object.name], object);
        }
    }
    if (support_bounds.find("FloorPlate") == support_bounds.end()
        || support_bounds.find("Gate0_Floor") == support_bounds.end()
        || support_bounds.find("Gate1_Floor") == support_bounds.end()) {
        throw std::invalid_argument{
            "Authored Earth chamber requires its floor and both gateway floors."};
    }

    AuthoredChamberCollisionContract contract;
    for (const auto& [name, bounds] : support_bounds) {
        const double floor_height{transform_y(bounds.maximum[1], placement)};
        contract.supports.push_back({
            placement.chamber_id,
            stable_name_id(name),
            transformed_rectangle(bounds, placement, 0.0),
            floor_height,
            floor_height + earth_collision_ceiling_height_metres,
            name == "FloorPlate" ? 600U : 650U,
        });
    }
    for (const auto& [name, bounds] : blocker_bounds) {
        contract.blockers.push_back({
            stable_name_id(name),
            transformed_rectangle(bounds, placement, blocker_expansion_metres),
            transform_y(bounds.minimum[1], placement),
            transform_y(bounds.maximum[1], placement),
        });
    }
    return contract;
}

AuthoredChamberCollisionContract build_air_chamber_collision(
    const MaterialModelLoadResult& model,
    const AuthoredChamberPlacement& placement)
{
    if (model.objects.empty() || !std::isfinite(placement.yaw_radians)
        || !std::isfinite(placement.translation_metres.x)
        || !std::isfinite(placement.translation_metres.y)
        || !std::isfinite(placement.translation_metres.z)
        || placement.scale.x <= 0.0 || placement.scale.y <= 0.0
        || placement.scale.z <= 0.0) {
        throw std::invalid_argument{
            "Authored Air collision requires valid objects and placement."};
    }

    std::map<std::string, SourceBounds> support_bounds;
    std::map<std::string, SourceBounds> blocker_bounds;
    for (const MaterialModelObject& object : model.objects) {
        if (air_walkable_object(object.name)) {
            include_bounds(support_bounds[object.name], object);
        } else if (air_solid_object(object.name)) {
            include_bounds(blocker_bounds[object.name], object);
        }
    }
    const auto floor{support_bounds.find("FloorPlate")};
    if (floor == support_bounds.end()) {
        throw std::invalid_argument{
            "Authored Air chamber has no FloorPlate walkable surface."};
    }

    AuthoredChamberCollisionContract contract;
    const double floor_height{transform_y(floor->second.maximum[1], placement)};
    contract.supports.push_back({
        placement.chamber_id,
        stable_name_id("FloorPlate"),
        transformed_air_floor(placement),
        floor_height,
        floor_height + air_collision_ceiling_height_metres,
        700U,
    });
    for (const auto& [name, bounds] : support_bounds) {
        if (name == "FloorPlate") {
            continue;
        }
        const double support_height{transform_y(bounds.maximum[1], placement)};
        contract.supports.push_back({
            placement.chamber_id,
            stable_name_id(name),
            transformed_rectangle(bounds, placement, 0.0),
            support_height,
            support_height + air_collision_ceiling_height_metres,
            650U,
        });
    }

    for (const double direction : {-1.0, 1.0}) {
        SourceBounds seam;
        seam.minimum[0] = -2.575;
        seam.maximum[0] = 2.575;
        seam.minimum[1] = 0.0;
        seam.maximum[1] = 0.14;
        if (direction < 0.0) {
            seam.minimum[2] = -21.40;
            seam.maximum[2] = -20.75;
        } else {
            seam.minimum[2] = 20.75;
            seam.maximum[2] = 21.40;
        }
        const std::string name{direction < 0.0
                ? "DOOR_A_ThresholdSupport"
                : "DOOR_B_ThresholdSupport"};
        const double support_height{transform_y(seam.maximum[1], placement)};
        contract.supports.push_back({
            placement.chamber_id,
            stable_name_id(name),
            transformed_rectangle(seam, placement, 0.0),
            support_height,
            support_height + air_collision_ceiling_height_metres,
            675U,
        });
    }

    for (const auto& [name, bounds] : blocker_bounds) {
        contract.blockers.push_back({
            stable_name_id(name),
            transformed_rectangle(bounds, placement, blocker_expansion_metres),
            transform_y(bounds.minimum[1], placement),
            transform_y(bounds.maximum[1], placement),
        });
    }
    return contract;
}

AuthoredChamberCollisionContract build_aether_chamber_collision(
    const MaterialModelLoadResult& model,
    const AuthoredChamberPlacement& placement)
{
    return build_simple_authored_collision(model, placement,
        aether_collision_ceiling_height_metres,
        aether_walkable_group, aether_solid_object);
}

AuthoredChamberCollisionContract build_start_chamber_collision(
    const MaterialModelLoadResult& model,
    const AuthoredChamberPlacement& placement)
{
    return build_simple_authored_collision(model, placement,
        start_collision_ceiling_height_metres,
        start_walkable_group, start_solid_object);
}

AuthoredChamberCollisionContract build_exit_chamber_collision(
    const MaterialModelLoadResult& model,
    const AuthoredChamberPlacement& placement)
{
    return build_simple_authored_collision(model, placement,
        exit_collision_ceiling_height_metres,
        exit_walkable_group, exit_solid_object);
}

void append_authored_chamber_collision(
    CollisionWorld& world,
    const AuthoredChamberCollisionContract& contract)
{
    world.chamber_supports.insert(
        world.chamber_supports.end(), contract.supports.begin(), contract.supports.end());
    world.chamber_blockers.insert(
        world.chamber_blockers.end(), contract.blockers.begin(), contract.blockers.end());
    world.chamber_respawns.insert(
        world.chamber_respawns.end(), contract.respawns.begin(), contract.respawns.end());
    std::sort(world.chamber_supports.begin(), world.chamber_supports.end(),
        [](const ChamberSupportRegion& left, const ChamberSupportRegion& right) {
            if (left.floor_height_metres != right.floor_height_metres) {
                return left.floor_height_metres > right.floor_height_metres;
            }
            return left.stable_object_id < right.stable_object_id;
        });
    std::sort(world.chamber_blockers.begin(), world.chamber_blockers.end(),
        [](const ChamberBlockerRegion& left, const ChamberBlockerRegion& right) {
            return left.stable_object_id < right.stable_object_id;
        });
    const std::vector<std::string> errors{validate_collision_world(world)};
    if (!errors.empty()) {
        throw ControllerError{
            "Authored chamber collision is invalid: " + errors.front()};
    }
}

}  // namespace crystalbound
