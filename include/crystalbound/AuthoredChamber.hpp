#pragma once

#include <string>
#include <vector>

#include "crystalbound/CaveScene.hpp"
#include "crystalbound/ObjLoader.hpp"
#include "crystalbound/PlayerController.hpp"

namespace crystalbound {

inline constexpr double authored_water_vertical_offset_metres{0.35};
inline constexpr double authored_fire_vertical_offset_metres{0.0};
inline constexpr double authored_earth_vertical_offset_metres{0.0};
inline constexpr double authored_air_vertical_offset_metres{0.0};
inline constexpr double authored_aether_vertical_offset_metres{0.0};
inline constexpr double authored_start_vertical_offset_metres{0.0};
inline constexpr double authored_exit_vertical_offset_metres{0.0};

struct AuthoredChamberPlacement {
    NodeId chamber_id{};
    GeometryVector3 translation_metres{};
    double yaw_radians{};
    GeometryVector3 scale{1.0, 1.0, 1.0};
};

struct AuthoredChamberCollisionContract {
    std::vector<ChamberSupportRegion> supports{};
    std::vector<ChamberBlockerRegion> blockers{};
    std::vector<ChamberRespawnPoint> respawns{};
};

[[nodiscard]] const std::vector<std::string>&
air_render_excluded_object_names();
[[nodiscard]] const std::vector<std::string>&
exit_render_excluded_object_names();
[[nodiscard]] MaterialModelLoadResult load_water_chamber_render_asset(
    const std::filesystem::path& path);
[[nodiscard]] AuthoredChamberPlacement water_chamber_placement(
    const CaveSceneData& scene);
[[nodiscard]] AuthoredChamberPlacement fire_chamber_placement(
    const CaveSceneData& scene);
[[nodiscard]] AuthoredChamberPlacement earth_chamber_placement(
    const CaveSceneData& scene);
[[nodiscard]] AuthoredChamberPlacement air_chamber_placement(
    const CaveSceneData& scene);
[[nodiscard]] AuthoredChamberPlacement aether_chamber_placement(
    const CaveSceneData& scene);
[[nodiscard]] AuthoredChamberPlacement start_chamber_placement(
    const CaveSceneData& scene);
[[nodiscard]] AuthoredChamberPlacement exit_chamber_placement(
    const CaveSceneData& scene);
[[nodiscard]] AuthoredChamberCollisionContract build_water_chamber_collision(
    const MaterialModelLoadResult& model,
    const AuthoredChamberPlacement& placement);
[[nodiscard]] AuthoredChamberCollisionContract build_fire_chamber_collision(
    const MaterialModelLoadResult& model,
    const AuthoredChamberPlacement& placement);
[[nodiscard]] AuthoredChamberCollisionContract build_earth_chamber_collision(
    const MaterialModelLoadResult& model,
    const AuthoredChamberPlacement& placement);
[[nodiscard]] AuthoredChamberCollisionContract build_air_chamber_collision(
    const MaterialModelLoadResult& model,
    const AuthoredChamberPlacement& placement);
[[nodiscard]] AuthoredChamberCollisionContract build_aether_chamber_collision(
    const MaterialModelLoadResult& model,
    const AuthoredChamberPlacement& placement);
[[nodiscard]] AuthoredChamberCollisionContract build_start_chamber_collision(
    const MaterialModelLoadResult& model,
    const AuthoredChamberPlacement& placement);
[[nodiscard]] AuthoredChamberCollisionContract build_exit_chamber_collision(
    const MaterialModelLoadResult& model,
    const AuthoredChamberPlacement& placement);
void append_authored_chamber_collision(
    CollisionWorld& world,
    const AuthoredChamberCollisionContract& contract);

}  // namespace crystalbound
