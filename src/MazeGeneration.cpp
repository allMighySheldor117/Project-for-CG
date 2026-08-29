#include "crystalbound/MazeGeneration.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <set>
#include <string>
#include <utility>

#include "crystalbound/DeterministicRandom.hpp"
#include "crystalbound/PlayerController.hpp"

namespace crystalbound {
namespace {

constexpr double millimetres_per_metre{1'000.0};
constexpr std::uint64_t maze_wall_domain{0x4D415A4557414C4CULL};
constexpr std::uint64_t maze_support_domain{0x4D415A4553555050ULL};
constexpr std::uint64_t maze_pillar_domain{0x4D415A4550494C4CULL};
constexpr std::uint64_t maze_arch_domain{0x4D415A4541524348ULL};
constexpr std::int32_t authored_wall_length_millimetres{4'000};
constexpr std::int32_t authored_pillar_half_extent_millimetres{280};
constexpr std::int32_t authored_pillar_height_millimetres{4'350};
constexpr std::int32_t authored_arch_jamb_center_x_millimetres{1'830};
constexpr std::int32_t authored_arch_jamb_half_width_millimetres{230};
constexpr std::int32_t authored_arch_half_depth_millimetres{750};
constexpr std::int32_t authored_arch_jamb_height_millimetres{2'600};
constexpr std::int32_t authored_arch_header_half_width_millimetres{2'060};
constexpr std::int32_t authored_arch_header_min_height_millimetres{2'200};
constexpr std::int32_t authored_arch_header_max_height_millimetres{4'250};

template <std::size_t CellCount>
[[nodiscard]] constexpr std::array<std::int32_t, CellCount + 1U>
make_boundaries(const std::int32_t half_extent_millimetres) noexcept
{
    std::array<std::int32_t, CellCount + 1U> boundaries{};
    for (std::size_t index{}; index <= CellCount; ++index) {
        const std::int64_t numerator{
            static_cast<std::int64_t>(2 * half_extent_millimetres)
                * static_cast<std::int64_t>(index)};
        boundaries[index] = -half_extent_millimetres
            + static_cast<std::int32_t>(
                (numerator + static_cast<std::int64_t>(CellCount / 2U))
                / static_cast<std::int64_t>(CellCount));
    }
    return boundaries;
}

constexpr auto x_boundaries{
    make_boundaries<maze_grid_columns>(maze_room_half_width_millimetres)};
constexpr auto z_boundaries{
    make_boundaries<maze_grid_rows>(maze_room_core_half_length_millimetres)};
constexpr std::size_t cell_count{maze_grid_columns * maze_grid_rows};
constexpr std::uint32_t entrance_column{maze_grid_columns / 2U};
static_assert(maze_room_door_half_width_millimetres * 2
    == tunnel_clear_width_millimetres);

[[nodiscard]] constexpr std::size_t cell_index(const MazeCellCoordinate cell) noexcept
{
    return static_cast<std::size_t>(cell.row) * maze_grid_columns + cell.column;
}

[[nodiscard]] constexpr std::uint64_t edge_key(
    const MazeCellCoordinate first, const MazeCellCoordinate second) noexcept
{
    const std::size_t a{cell_index(first)};
    const std::size_t b{cell_index(second)};
    const std::size_t low{a < b ? a : b};
    const std::size_t high{a < b ? b : a};
    return (static_cast<std::uint64_t>(low) << 32U) | high;
}

[[nodiscard]] std::uint64_t coordinate_key(
    const std::int32_t x_millimetres,
    const std::int32_t z_millimetres) noexcept
{
    return (static_cast<std::uint64_t>(
                static_cast<std::uint32_t>(x_millimetres))
            << 32U)
        | static_cast<std::uint32_t>(z_millimetres);
}

template <std::size_t BoundaryCount>
[[nodiscard]] std::pair<std::int32_t, std::int32_t> segment_boundaries(
    const std::array<std::int32_t, BoundaryCount>& boundaries,
    const std::int32_t center_millimetres,
    const std::int32_t length_millimetres)
{
    for (std::size_t index{}; index + 1U < boundaries.size(); ++index) {
        if ((boundaries[index] + boundaries[index + 1U]) / 2
                == center_millimetres
            && boundaries[index + 1U] - boundaries[index]
                == length_millimetres) {
            return {boundaries[index], boundaries[index + 1U]};
        }
    }
    throw GeometryError{
        "Maze wall does not align with a generated cell boundary."};
}

[[nodiscard]] std::vector<MazeCellCoordinate> unvisited_neighbours(
    const MazeCellCoordinate cell, const std::array<bool, cell_count>& visited)
{
    std::vector<MazeCellCoordinate> result;
    const auto add = [&](const MazeCellCoordinate candidate) {
        if (!visited[cell_index(candidate)]) {
            result.push_back(candidate);
        }
    };
    if (cell.column > 0U) add({cell.column - 1U, cell.row});
    if (cell.column + 1U < maze_grid_columns) add({cell.column + 1U, cell.row});
    if (cell.row > 0U) add({cell.column, cell.row - 1U});
    if (cell.row + 1U < maze_grid_rows) add({cell.column, cell.row + 1U});
    return result;
}

[[nodiscard]] std::int32_t sign_milli(const std::int32_t value) noexcept
{
    return value > 0 ? 1'000 : value < 0 ? -1'000 : 0;
}

[[nodiscard]] std::uint64_t room_fingerprint(const MazeRoomContract& room) noexcept
{
    std::uint64_t hash{14'695'981'039'346'656'037ULL};
    const auto append = [&hash](const std::uint64_t value) {
        for (unsigned int shift{}; shift < 64U; shift += 8U) {
            hash ^= (value >> shift) & 0xFFU;
            hash *= 1'099'511'628'211ULL;
        }
    };
    append(stable_edge_id(room.route));
    append(room.ordinal);
    for (const MazeWallContract& wall : room.walls) {
        append(wall.stable_object_id);
        append(static_cast<std::uint8_t>(wall.direction));
        append(static_cast<std::uint32_t>(wall.center_x_millimetres));
        append(static_cast<std::uint32_t>(wall.center_z_millimetres));
        append(static_cast<std::uint32_t>(wall.length_millimetres));
    }
    for (const MazeCellCoordinate cell : room.solution_cells) {
        append((static_cast<std::uint64_t>(cell.row) << 32U) | cell.column);
    }
    return hash;
}

[[nodiscard]] MazeRoomContract make_room(const RouteGeometryContract& route,
    const std::uint32_t ordinal, const Seed seed)
{
    if (route.spline.control_points.size() < 2U) {
        throw GeometryError{"Maze route requires two endpoints."};
    }
    const IntegerPoint3& start{route.spline.control_points.front()};
    const IntegerPoint3& finish{route.spline.control_points.back()};
    const std::int32_t dx{finish.x_millimetres - start.x_millimetres};
    const std::int32_t dz{finish.z_millimetres - start.z_millimetres};
    if (start.y_millimetres != finish.y_millimetres
        || (dx != 0 && dz != 0) || (dx == 0 && dz == 0)) {
        throw GeometryError{"Maze rooms require straight, level, axis-aligned routes."};
    }
    const std::int64_t route_length{std::max(
        dx < 0 ? -static_cast<std::int64_t>(dx) : static_cast<std::int64_t>(dx),
        dz < 0 ? -static_cast<std::int64_t>(dz) : static_cast<std::int64_t>(dz))};
    const std::int64_t required_length{2LL
        * (maze_room_connector_half_length_millimetres
            + maze_minimum_tunnel_run_millimetres)};
    if (route_length < required_length) {
        throw GeometryError{"Maze route "
            + std::to_string(route.edge.first.value) + "-"
            + std::to_string(route.edge.second.value) + " is "
            + std::to_string(route_length) + " mm long; at least "
            + std::to_string(required_length)
            + " mm is required for the authored room and clear tunnel runs."};
    }

    MazeRoomContract room;
    room.route = route.edge;
    room.ordinal = ordinal;
    room.floor_center_millimetres = {
        (start.x_millimetres + finish.x_millimetres) / 2,
        start.y_millimetres - route.spline.radius_millimetres,
        (start.z_millimetres + finish.z_millimetres) / 2};
    room.forward_x_milli = sign_milli(dx);
    room.forward_z_milli = sign_milli(dz);
    room.columns = maze_grid_columns;
    room.rows = maze_grid_rows;

    std::array<bool, cell_count> visited{};
    std::array<MazeCellCoordinate, cell_count> parent{};
    std::array<bool, cell_count> has_parent{};
    std::vector<MazeCellCoordinate> stack{{
        entrance_column, maze_grid_rows - 1U}};
    visited[cell_index(stack.back())] = true;
    std::vector<std::uint64_t> passages;
    SplitMix64 random{make_substream(seed.value, random_domain::maze,
        stable_edge_id(route.edge) ^ ordinal)};
    while (!stack.empty()) {
        const MazeCellCoordinate current{stack.back()};
        std::vector<MazeCellCoordinate> available{
            unvisited_neighbours(current, visited)};
        if (available.empty()) {
            stack.pop_back();
            continue;
        }
        const MazeCellCoordinate next{
            available[static_cast<std::size_t>(random.bounded(available.size()))]};
        passages.push_back(edge_key(current, next));
        visited[cell_index(next)] = true;
        parent[cell_index(next)] = current;
        has_parent[cell_index(next)] = true;
        stack.push_back(next);
    }
    std::sort(passages.begin(), passages.end());

    std::uint64_t wall_index{};
    for (std::uint32_t row{}; row < maze_grid_rows; ++row) {
        for (std::uint32_t column{1U}; column < maze_grid_columns; ++column) {
            const MazeCellCoordinate left{column - 1U, row};
            const MazeCellCoordinate right{column, row};
            if (!std::binary_search(passages.begin(), passages.end(), edge_key(left, right))) {
                room.walls.push_back({maze_wall_domain ^ stable_edge_id(route.edge) ^ wall_index++,
                    MazeWallDirection::local_z, x_boundaries[column],
                    (z_boundaries[row] + z_boundaries[row + 1U]) / 2,
                    z_boundaries[row + 1U] - z_boundaries[row]});
            }
        }
    }
    for (std::uint32_t row{1U}; row < maze_grid_rows; ++row) {
        for (std::uint32_t column{}; column < maze_grid_columns; ++column) {
            const MazeCellCoordinate north{column, row - 1U};
            const MazeCellCoordinate south{column, row};
            if (!std::binary_search(passages.begin(), passages.end(), edge_key(north, south))) {
                room.walls.push_back({maze_wall_domain ^ stable_edge_id(route.edge) ^ wall_index++,
                    MazeWallDirection::local_x,
                    (x_boundaries[column] + x_boundaries[column + 1U]) / 2,
                    z_boundaries[row], x_boundaries[column + 1U] - x_boundaries[column]});
            }
        }
    }

    const MazeCellCoordinate start_cell{
        entrance_column, maze_grid_rows - 1U};
    MazeCellCoordinate cursor{entrance_column, 0U};
    room.solution_cells.push_back(cursor);
    while (!(cursor == start_cell)) {
        if (!has_parent[cell_index(cursor)]) {
            throw GeometryError{"Generated maze does not connect both doors."};
        }
        cursor = parent[cell_index(cursor)];
        room.solution_cells.push_back(cursor);
    }
    std::reverse(room.solution_cells.begin(), room.solution_cells.end());
    room.fingerprint = room_fingerprint(room);
    return room;
}

[[nodiscard]] Vertex make_vertex(const GeometryVector3& position,
    const GeometryVector3& normal, const float u, const float v) noexcept
{
    return {{static_cast<float>(position.x), static_cast<float>(position.y),
                static_cast<float>(position.z)},
        {static_cast<float>(normal.x), static_cast<float>(normal.y),
            static_cast<float>(normal.z)}, {u, v}};
}

void append_face(MeshBuilder& builder, const std::array<GeometryVector3, 4>& face,
    const GeometryVector3 normal)
{
    const std::uint32_t a{builder.append_vertex(make_vertex(face[0], normal, 0, 0))};
    const std::uint32_t b{builder.append_vertex(make_vertex(face[1], normal, 1, 0))};
    const std::uint32_t c{builder.append_vertex(make_vertex(face[2], normal, 1, 1))};
    const std::uint32_t d{builder.append_vertex(make_vertex(face[3], normal, 0, 1))};
    builder.append_triangle(a, b, c);
    builder.append_triangle(a, c, d);
}

void append_box(MeshBuilder& builder, const GeometryVector3 center,
    const GeometryVector3 half)
{
    const double x0{center.x-half.x}, x1{center.x+half.x};
    const double y0{center.y-half.y}, y1{center.y+half.y};
    const double z0{center.z-half.z}, z1{center.z+half.z};
    append_face(builder, {{{x1,y0,z0},{x1,y1,z0},{x1,y1,z1},{x1,y0,z1}}}, {1,0,0});
    append_face(builder, {{{x0,y0,z1},{x0,y1,z1},{x0,y1,z0},{x0,y0,z0}}}, {-1,0,0});
    append_face(builder, {{{x0,y1,z0},{x0,y1,z1},{x1,y1,z1},{x1,y1,z0}}}, {0,1,0});
    append_face(builder, {{{x0,y0,z1},{x0,y0,z0},{x1,y0,z0},{x1,y0,z1}}}, {0,-1,0});
    append_face(builder, {{{x1,y0,z1},{x1,y1,z1},{x0,y1,z1},{x0,y0,z1}}}, {0,0,1});
    append_face(builder, {{{x0,y0,z0},{x0,y1,z0},{x1,y1,z0},{x1,y0,z0}}}, {0,0,-1});
}

[[nodiscard]] std::vector<TemplatePoint2> rectangle_polygon(
    const MazeRoomContract& room, const std::int32_t center_x,
    const std::int32_t center_z, const std::int32_t half_x,
    const std::int32_t half_z)
{
    std::vector<TemplatePoint2> polygon;
    for (const auto corner : std::array<std::array<std::int32_t, 2>, 4>{{
        {{center_x-half_x,center_z-half_z}},{{center_x+half_x,center_z-half_z}},
        {{center_x+half_x,center_z+half_z}},{{center_x-half_x,center_z+half_z}}}}) {
        const IntegerPoint3 world{maze_local_to_world(room, corner[0], 0, corner[1])};
        polygon.push_back({world.x_millimetres, world.z_millimetres});
    }
    return polygon;
}

}  // namespace

std::vector<MazeRoomContract> build_maze_rooms(const TopologyData& topology,
    const std::vector<RouteGeometryContract>& routes, const Seed effective_seed)
{
    const auto exit{std::find_if(topology.nodes.begin(), topology.nodes.end(),
        [](const ChamberNode& node) { return node.role == ChamberRole::exit; })};
    if (exit == topology.nodes.end()) throw GeometryError{"Maze generation requires Exit."};
    std::vector<MazeRoomContract> rooms;
    rooms.reserve(fixed_maze_room_count);
    for (const RouteGeometryContract& route : routes) {
        if (route.edge.first != exit->id && route.edge.second != exit->id) {
            rooms.push_back(make_room(route, static_cast<std::uint32_t>(rooms.size()), effective_seed));
        }
    }
    if (rooms.size() != fixed_maze_room_count) {
        throw GeometryError{"Fixed layout must produce exactly five maze rooms."};
    }
    return rooms;
}

const MazeRoomContract* maze_room_for_route(const std::vector<MazeRoomContract>& rooms,
    const Edge route) noexcept
{
    const auto found{std::find_if(rooms.begin(), rooms.end(),
        [route](const MazeRoomContract& room) { return room.route == route; })};
    return found == rooms.end() ? nullptr : &*found;
}

IntegerPoint3 maze_local_to_world(const MazeRoomContract& room,
    const std::int32_t local_x, const std::int32_t local_y,
    const std::int32_t local_z) noexcept
{
    const std::int64_t right_x{room.forward_z_milli};
    const std::int64_t right_z{-room.forward_x_milli};
    return {room.floor_center_millimetres.x_millimetres
            + static_cast<std::int32_t>((right_x*local_x
                - static_cast<std::int64_t>(room.forward_x_milli)*local_z)/1'000),
        room.floor_center_millimetres.y_millimetres + local_y,
        room.floor_center_millimetres.z_millimetres
            + static_cast<std::int32_t>((right_z*local_x
                - static_cast<std::int64_t>(room.forward_z_milli)*local_z)/1'000)};
}

GeometryVector3 maze_cell_center_metres(const MazeRoomContract& room,
    const MazeCellCoordinate cell)
{
    if (cell.column >= maze_grid_columns || cell.row >= maze_grid_rows) {
        throw GeometryError{"Maze cell is outside the grid."};
    }
    const IntegerPoint3 world{maze_local_to_world(room,
        (x_boundaries[cell.column]+x_boundaries[cell.column+1U])/2, 0,
        (z_boundaries[cell.row]+z_boundaries[cell.row+1U])/2)};
    return {world.x_millimetres/millimetres_per_metre,
        world.y_millimetres/millimetres_per_metre,
        world.z_millimetres/millimetres_per_metre};
}

std::vector<MazeAuthoredModelInstance> maze_authored_model_instances(
    const MazeRoomContract& room)
{
    std::vector<MazeAuthoredModelInstance> instances;
    std::set<std::pair<std::int32_t, std::int32_t>> pillar_centers;
    instances.reserve(room.walls.size() * 2U + 2U);
    for (const MazeWallContract& wall : room.walls) {
        const bool along_x{wall.direction == MazeWallDirection::local_x};
        instances.push_back({MazeAuthoredModelKind::wall,
            wall.stable_object_id,
            wall.center_x_millimetres,
            wall.center_z_millimetres,
            static_cast<std::uint8_t>(along_x ? 0U : 1U),
            wall.length_millimetres});
        if (along_x) {
            const auto endpoints{segment_boundaries(x_boundaries,
                wall.center_x_millimetres, wall.length_millimetres)};
            pillar_centers.insert({endpoints.first, wall.center_z_millimetres});
            pillar_centers.insert({endpoints.second, wall.center_z_millimetres});
        } else {
            const auto endpoints{segment_boundaries(z_boundaries,
                wall.center_z_millimetres, wall.length_millimetres)};
            pillar_centers.insert({wall.center_x_millimetres, endpoints.first});
            pillar_centers.insert({wall.center_x_millimetres, endpoints.second});
        }
    }
    const std::uint64_t route_id{stable_edge_id(room.route)};
    for (const auto [center_x, center_z] : pillar_centers) {
        const std::uint64_t stable_id{
            maze_pillar_domain ^ route_id ^ coordinate_key(center_x, center_z)};
        instances.push_back({MazeAuthoredModelKind::pillar, stable_id,
            center_x, center_z, static_cast<std::uint8_t>(stable_id & 3U), 0});
    }
    instances.push_back({MazeAuthoredModelKind::arch,
        maze_arch_domain ^ route_id, 0,
        maze_room_core_half_length_millimetres, 0U, 0});
    instances.push_back({MazeAuthoredModelKind::arch,
        maze_arch_domain ^ route_id ^ 1U, 0,
        -maze_room_core_half_length_millimetres, 2U, 0});
    return instances;
}

MazeAuthoredModelPlacement maze_authored_model_placement(
    const MazeRoomContract& room,
    const MazeAuthoredModelInstance& instance)
{
    if (instance.local_yaw_quarter_turns >= 4U
        || (instance.kind == MazeAuthoredModelKind::wall
            && instance.target_length_millimetres <= 0)
        || (instance.kind != MazeAuthoredModelKind::wall
            && instance.target_length_millimetres != 0)) {
        throw GeometryError{"Authored maze model instance is invalid."};
    }
    constexpr std::array<std::pair<std::int32_t, std::int32_t>, 4>
        local_x_axes{{{1'000, 0}, {0, 1'000}, {-1'000, 0}, {0, -1'000}}};
    const IntegerPoint3 center{maze_local_to_world(room,
        instance.center_x_millimetres, 0, instance.center_z_millimetres)};
    const auto [axis_x, axis_z]{local_x_axes[instance.local_yaw_quarter_turns]};
    const IntegerPoint3 axis_end{maze_local_to_world(room,
        instance.center_x_millimetres + axis_x,
        0,
        instance.center_z_millimetres + axis_z)};
    const double direction_x{
        (axis_end.x_millimetres - center.x_millimetres)
        / millimetres_per_metre};
    const double direction_z{
        (axis_end.z_millimetres - center.z_millimetres)
        / millimetres_per_metre};
    const double scale_x{instance.kind == MazeAuthoredModelKind::wall
        ? static_cast<double>(instance.target_length_millimetres)
            / authored_wall_length_millimetres
        : 1.0};
    return {{center.x_millimetres / millimetres_per_metre,
                center.y_millimetres / millimetres_per_metre,
                center.z_millimetres / millimetres_per_metre},
        std::atan2(-direction_z, direction_x), {scale_x, 1.0, 1.0}};
}

std::array<RouteGeometryContract, 2> maze_tunnel_segments(
    const RouteGeometryContract& route, const MazeRoomContract& room)
{
    RouteGeometryContract first{route};
    RouteGeometryContract second{route};
    const std::int32_t overlap_end{maze_room_connector_half_length_millimetres
        - maze_tunnel_overlap_millimetres};
    const IntegerPoint3 south{maze_local_to_world(
        room, 0, route.spline.radius_millimetres, overlap_end)};
    const IntegerPoint3 north{maze_local_to_world(
        room, 0, route.spline.radius_millimetres, -overlap_end)};
    first.spline.control_points = {route.spline.control_points.front(), south};
    second.spline.control_points = {north, route.spline.control_points.back()};
    first.ring_offsets_millimetres.assign(2U, 0);
    second.ring_offsets_millimetres.assign(2U, 0);
    return {std::move(first), std::move(second)};
}

MeshData build_maze_wall_mesh(const MazeRoomContract& room)
{
    MeshBuilder builder;
    for (const MazeWallContract& wall : room.walls) {
        const IntegerPoint3 center{maze_local_to_world(room, wall.center_x_millimetres,
            maze_wall_height_millimetres/2, wall.center_z_millimetres)};
        const bool along_x{wall.direction == MazeWallDirection::local_x};
        GeometryVector3 half{
            (along_x ? wall.length_millimetres : maze_wall_thickness_millimetres)
                /2.0/millimetres_per_metre,
            maze_wall_height_millimetres/2.0/millimetres_per_metre,
            (along_x ? maze_wall_thickness_millimetres : wall.length_millimetres)
                /2.0/millimetres_per_metre};
        if (room.forward_x_milli != 0) std::swap(half.x, half.z);
        append_box(builder, {center.x_millimetres/millimetres_per_metre,
            center.y_millimetres/millimetres_per_metre,
            center.z_millimetres/millimetres_per_metre}, half);
    }
    return builder.finish();
}

void append_maze_room_collision(CollisionWorld& world,
    const std::vector<MazeRoomContract>& rooms)
{
    for (const MazeRoomContract& room : rooms) {
        const double floor_height{
            room.floor_center_millimetres.y_millimetres/millimetres_per_metre};
        const auto append_blocker = [&](const std::uint64_t stable_id,
                                        const std::int32_t center_x,
                                        const std::int32_t center_z,
                                        const std::int32_t half_x,
                                        const std::int32_t half_z) {
            world.chamber_blockers.push_back({stable_id,
                rectangle_polygon(room, center_x, center_z, half_x, half_z),
                floor_height, floor_height + 4.2, false});
        };
        world.chamber_supports.push_back({room.route.first,
            maze_support_domain ^ stable_edge_id(room.route),
            rectangle_polygon(room, 0, 0, maze_room_half_width_millimetres,
                maze_room_connector_half_length_millimetres),
            floor_height,
            (room.floor_center_millimetres.y_millimetres+4'200)/millimetres_per_metre,
            50U});
        for (const MazeWallContract& wall : room.walls) {
            const bool along_x{wall.direction == MazeWallDirection::local_x};
            world.chamber_blockers.push_back({wall.stable_object_id,
                rectangle_polygon(room, wall.center_x_millimetres,
                    wall.center_z_millimetres,
                    (along_x ? wall.length_millimetres : maze_wall_thickness_millimetres)/2,
                    (along_x ? maze_wall_thickness_millimetres : wall.length_millimetres)/2),
                floor_height,
                (room.floor_center_millimetres.y_millimetres+maze_wall_height_millimetres)
                    /millimetres_per_metre, false});
        }
        for (const MazeAuthoredModelInstance& instance
            : maze_authored_model_instances(room)) {
            if (instance.kind == MazeAuthoredModelKind::wall) {
                continue;
            }
            if (instance.kind == MazeAuthoredModelKind::pillar) {
                world.chamber_blockers.push_back({instance.stable_object_id,
                    rectangle_polygon(room, instance.center_x_millimetres,
                        instance.center_z_millimetres,
                        authored_pillar_half_extent_millimetres,
                        authored_pillar_half_extent_millimetres),
                    floor_height,
                    (room.floor_center_millimetres.y_millimetres
                        + authored_pillar_height_millimetres)
                        / millimetres_per_metre,
                    false});
                continue;
            }
            for (const std::int32_t local_x
                : {-authored_arch_jamb_center_x_millimetres,
                    authored_arch_jamb_center_x_millimetres}) {
                world.chamber_blockers.push_back({
                    instance.stable_object_id
                        ^ static_cast<std::uint64_t>(local_x < 0 ? 1U : 2U),
                    rectangle_polygon(room, local_x,
                        instance.center_z_millimetres,
                        authored_arch_jamb_half_width_millimetres,
                        authored_arch_half_depth_millimetres),
                    floor_height,
                    (room.floor_center_millimetres.y_millimetres
                        + authored_arch_jamb_height_millimetres)
                        / millimetres_per_metre,
                    false});
            }
            world.chamber_blockers.push_back({
                instance.stable_object_id ^ 3U,
                rectangle_polygon(room, 0, instance.center_z_millimetres,
                    authored_arch_header_half_width_millimetres,
                    authored_arch_half_depth_millimetres),
                (room.floor_center_millimetres.y_millimetres
                    + authored_arch_header_min_height_millimetres)
                    / millimetres_per_metre,
                (room.floor_center_millimetres.y_millimetres
                    + authored_arch_header_max_height_millimetres)
                    / millimetres_per_metre,
                false});
        }
        const std::uint64_t boundary_base{
            maze_support_domain ^ stable_edge_id(room.route) ^ 0x424F554E4400ULL};
        constexpr std::int32_t core_boundary_half_thickness{230};
        constexpr std::int32_t connector_jamb_half_width{230};
        constexpr std::int32_t connector_half_length{
            (maze_room_connector_half_length_millimetres
                - maze_room_core_half_length_millimetres) / 2};
        constexpr std::int32_t connector_center_z{
            maze_room_core_half_length_millimetres + connector_half_length};
        constexpr std::int32_t end_wall_half_width{
            (maze_room_half_width_millimetres
                - maze_room_door_half_width_millimetres) / 2};
        constexpr std::int32_t end_wall_center_x{
            maze_room_door_half_width_millimetres + end_wall_half_width};
        constexpr std::int32_t connector_jamb_center_x{
            maze_room_door_half_width_millimetres
                + connector_jamb_half_width};
        append_blocker(boundary_base ^ 1U,
            -maze_room_half_width_millimetres, 0,
            core_boundary_half_thickness,
            maze_room_core_half_length_millimetres);
        append_blocker(boundary_base ^ 2U,
            maze_room_half_width_millimetres, 0,
            core_boundary_half_thickness,
            maze_room_core_half_length_millimetres);
        append_blocker(boundary_base ^ 3U, -end_wall_center_x,
            -maze_room_core_half_length_millimetres, end_wall_half_width,
            core_boundary_half_thickness);
        append_blocker(boundary_base ^ 4U, end_wall_center_x,
            -maze_room_core_half_length_millimetres, end_wall_half_width,
            core_boundary_half_thickness);
        append_blocker(boundary_base ^ 5U, -end_wall_center_x,
            maze_room_core_half_length_millimetres, end_wall_half_width,
            core_boundary_half_thickness);
        append_blocker(boundary_base ^ 6U, end_wall_center_x,
            maze_room_core_half_length_millimetres, end_wall_half_width,
            core_boundary_half_thickness);
        append_blocker(boundary_base ^ 7U, -connector_jamb_center_x,
            -connector_center_z, connector_jamb_half_width,
            connector_half_length);
        append_blocker(boundary_base ^ 8U, connector_jamb_center_x,
            -connector_center_z, connector_jamb_half_width,
            connector_half_length);
        append_blocker(boundary_base ^ 9U, -connector_jamb_center_x,
            connector_center_z, connector_jamb_half_width,
            connector_half_length);
        append_blocker(boundary_base ^ 10U, connector_jamb_center_x,
            connector_center_z, connector_jamb_half_width,
            connector_half_length);
    }
}

std::vector<std::string> validate_maze_rooms(const TopologyData&,
    const std::vector<RouteGeometryContract>& routes,
    const std::vector<MazeRoomContract>& rooms)
{
    std::vector<std::string> errors;
    if (rooms.size() != fixed_maze_room_count) {
        errors.push_back("Fixed cave must contain exactly five maze rooms.");
    }
    for (std::size_t index{}; index < rooms.size(); ++index) {
        const MazeRoomContract& room{rooms[index]};
        if (room.ordinal != index || room.columns != maze_grid_columns
            || room.rows != maze_grid_rows) {
            errors.push_back("Maze room grid contract is inconsistent.");
        }
        if (room.solution_cells.empty()
            || !(room.solution_cells.front()
                == MazeCellCoordinate{entrance_column, maze_grid_rows - 1U})
            || !(room.solution_cells.back()
                == MazeCellCoordinate{entrance_column, 0U})) {
            errors.push_back("Maze room has no south-to-north solution.");
        }
        const auto route{std::find_if(routes.begin(), routes.end(),
            [&](const RouteGeometryContract& candidate) {
                return candidate.edge == room.route;
            })};
        if (route == routes.end()) {
            errors.push_back("Maze room references an unknown route.");
            continue;
        }
        const IntegerPoint3& start{route->spline.control_points.front()};
        const IntegerPoint3& finish{route->spline.control_points.back()};
        const std::int64_t dx{
            static_cast<std::int64_t>(finish.x_millimetres)
                - start.x_millimetres};
        const std::int64_t dz{
            static_cast<std::int64_t>(finish.z_millimetres)
                - start.z_millimetres};
        const std::int64_t route_length{
            std::max(dx < 0 ? -dx : dx, dz < 0 ? -dz : dz)};
        if (route_length / 2
            < maze_room_connector_half_length_millimetres
                + maze_minimum_tunnel_run_millimetres) {
            errors.push_back(
                "Maze room leaves too little tunnel clearance beside a chamber.");
        }
    }
    return errors;
}

}  // namespace crystalbound
