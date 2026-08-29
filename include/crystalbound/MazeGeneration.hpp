#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "crystalbound/CaveScene.hpp"

namespace crystalbound {

struct CollisionWorld;

enum class MazeAuthoredModelKind : std::uint8_t {
    wall,
    pillar,
    arch,
};

struct MazeAuthoredModelInstance {
    MazeAuthoredModelKind kind{MazeAuthoredModelKind::wall};
    std::uint64_t stable_object_id{};
    std::int32_t center_x_millimetres{};
    std::int32_t center_z_millimetres{};
    std::uint8_t local_yaw_quarter_turns{};
    std::int32_t target_length_millimetres{};
};

struct MazeAuthoredModelPlacement {
    GeometryVector3 translation_metres{};
    double yaw_radians{};
    GeometryVector3 scale{1.0, 1.0, 1.0};
};

[[nodiscard]] std::vector<MazeRoomContract> build_maze_rooms(
    const TopologyData& topology,
    const std::vector<RouteGeometryContract>& routes,
    Seed effective_seed);
[[nodiscard]] const MazeRoomContract* maze_room_for_route(
    const std::vector<MazeRoomContract>& rooms,
    Edge route) noexcept;
[[nodiscard]] std::array<RouteGeometryContract, 2> maze_tunnel_segments(
    const RouteGeometryContract& route,
    const MazeRoomContract& room);
[[nodiscard]] IntegerPoint3 maze_local_to_world(
    const MazeRoomContract& room,
    std::int32_t local_x_millimetres,
    std::int32_t local_y_millimetres,
    std::int32_t local_z_millimetres) noexcept;
[[nodiscard]] GeometryVector3 maze_cell_center_metres(
    const MazeRoomContract& room,
    MazeCellCoordinate cell);
[[nodiscard]] std::vector<MazeAuthoredModelInstance>
maze_authored_model_instances(const MazeRoomContract& room);
[[nodiscard]] MazeAuthoredModelPlacement maze_authored_model_placement(
    const MazeRoomContract& room,
    const MazeAuthoredModelInstance& instance);
[[nodiscard]] MeshData build_maze_wall_mesh(const MazeRoomContract& room);
void append_maze_room_collision(
    CollisionWorld& world,
    const std::vector<MazeRoomContract>& rooms);
[[nodiscard]] std::vector<std::string> validate_maze_rooms(
    const TopologyData& topology,
    const std::vector<RouteGeometryContract>& routes,
    const std::vector<MazeRoomContract>& rooms);

}  // namespace crystalbound
