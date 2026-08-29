#include "CaveSceneTests.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <set>
#include <vector>

#include "crystalbound/CaveScene.hpp"
#include "crystalbound/MazeGeneration.hpp"
#include "crystalbound/PlayerController.hpp"

namespace crystalbound::test {
namespace {

class CaveSceneTestFailure final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

void require(const bool condition, const std::string_view message)
{
    if (!condition) {
        throw CaveSceneTestFailure{std::string{message}};
    }
}

template <typename Exception, typename Function>
void require_throws(
    Function&& function,
    const std::string_view expected_message,
    const std::string_view failure_message)
{
    try {
        function();
    } catch (const Exception& error) {
        if (std::string_view{error.what()}.find(expected_message) == std::string_view::npos) {
            throw CaveSceneTestFailure{
                std::string{failure_message} + ": unexpected message: " + error.what()};
        }
        return;
    } catch (const std::exception& error) {
        throw CaveSceneTestFailure{
            std::string{failure_message} + ": wrong exception type: " + error.what()};
    }
    throw CaveSceneTestFailure{std::string{failure_message} + ": no exception was thrown"};
}

[[nodiscard]] bool has_collider(
    const CaveSceneData& scene,
    const ColliderKind kind)
{
    return std::any_of(
        scene.colliders.begin(), scene.colliders.end(), [kind](const SceneCollider& collider) {
            return collider.kind == kind;
        });
}

[[nodiscard]] bool has_piece(
    const CaveSceneData& scene,
    const ScenePieceKind kind)
{
    return std::any_of(
        scene.mesh_pieces.begin(), scene.mesh_pieces.end(), [kind](const SceneMeshPiece& piece) {
            return piece.kind == kind;
        });
}

void fixed_seed_builds_complete_scene(const std::filesystem::path&)
{
    const CaveGenerationResult result{generate_cave({42U})};
    require(!result.generation.used_fallback, "canonical scene seed unexpectedly used fallback");
    require(
        validate_cave_scene(result.generation.topology, result.scene).empty(),
        "fixed seed scene did not validate");
    require(
        result.scene.chambers.size() == result.generation.topology.nodes.size(),
        "fixed seed lost a chamber");
    require(
        result.scene.routes.size() == result.generation.topology.routes.size(),
        "fixed seed lost a route");
    require(
        result.scene.portals.size() == result.scene.routes.size() * 2U,
        "fixed seed lost route portals");
    require(result.scene.bridge_routes.empty(), "fixed layout unexpectedly generated a bridge");
    require(result.scene.static_vertex_count > 0U, "fixed seed generated no vertices");
    require(
        format_fingerprint(result.scene.fingerprint) == "52cceb23a788803d",
        "canonical authored scene fingerprint changed");
}

void same_seed_repeats_scene_contract(const std::filesystem::path&)
{
    const CaveGenerationResult first{generate_cave({123'456'789U})};
    const CaveGenerationResult second{generate_cave({123'456'789U})};
    require(
        first.generation.effective_seed == second.generation.effective_seed,
        "same seed changed effective seed");
    require(
        first.generation.fingerprint == second.generation.fingerprint,
        "same seed changed topology fingerprint");
    require(
        first.scene.fingerprint == second.scene.fingerprint,
        "same seed changed scene fingerprint");
    require(first.scene.bridge_routes == second.scene.bridge_routes, "same seed changed bridge route");
    require(
        first.scene.static_vertex_count == second.scene.static_vertex_count,
        "same seed changed vertex count");
}

void representative_seeds_vary(const std::filesystem::path&)
{
    std::vector<std::uint64_t> fingerprints;
    for (const std::uint64_t seed : {0U, 1U, 42U, 123'456'789U, 987'654'321U}) {
        const CaveGenerationResult result{generate_cave({seed})};
        fingerprints.push_back(result.scene.fingerprint);
    }
    std::sort(fingerprints.begin(), fingerprints.end());
    fingerprints.erase(std::unique(fingerprints.begin(), fingerprints.end()), fingerprints.end());
    require(fingerprints.size() >= 2U, "representative seeds did not vary the scene contract");
}

void geometry_pieces_are_valid_and_budgeted(const std::filesystem::path&)
{
    const CaveGenerationResult result{generate_cave({123'456'789U})};
    std::uint64_t vertices{};
    for (const SceneMeshPiece& piece : result.scene.mesh_pieces) {
        validate_procedural_mesh(piece.mesh);
        vertices += piece.mesh.vertices.size();
    }
    require(vertices == result.scene.static_vertex_count, "scene vertex total drifted");
    require(vertices <= geometry_budgets.maximum_static_vertices, "scene exceeded vertex budget");
    require(
        result.scene.opaque_draw_call_count <= geometry_budgets.maximum_opaque_draw_calls,
        "scene exceeded draw-call budget");
    require(has_piece(result.scene, ScenePieceKind::chamber_shell), "scene has no chamber shell");
    require(has_piece(result.scene, ScenePieceKind::chamber_floor), "scene has no chamber floor");
    require(has_piece(result.scene, ScenePieceKind::tunnel), "scene has no tunnel");
    require(has_piece(result.scene, ScenePieceKind::junction), "scene has no junction");
    require(!has_piece(result.scene, ScenePieceKind::bridge),
        "tunnel-only scene unexpectedly has a bridge mesh");
    require(has_piece(result.scene, ScenePieceKind::natural_formation),
        "scene has no general natural formation batch");
}

[[nodiscard]] bool contains_error(
    const std::vector<std::string>& errors,
    const std::string_view expected)
{
    return std::any_of(errors.begin(), errors.end(),
        [expected](const std::string& error) {
            return error.find(expected) != std::string::npos;
        });
}

void elemental_chambers_obey_player_relative_spatial_contract(
    const std::filesystem::path&)
{
    const CaveGenerationResult result{generate_cave({42U})};
    constexpr std::int32_t capsule_radius_millimetres{350};
    constexpr std::int32_t safety_margin_millimetres{100};
    for (const ElementalChamberVisual& visual : result.scene.elemental_visuals.chambers) {
        const auto found = std::find_if(
            result.scene.chambers.begin(), result.scene.chambers.end(),
            [&](const ChamberGeometryContract& chamber) {
                return chamber.node_id == visual.chamber_id;
            });
        require(found != result.scene.chambers.end(),
            "elemental visual has no chamber geometry");
        const auto minimum_offset = std::min_element(
            found->radial_offsets_millimetres.begin(),
            found->radial_offsets_millimetres.end());
        require(minimum_offset != found->radial_offsets_millimetres.end(),
            "elemental chamber has no radial contract");
        const std::int64_t usable_diameter{
            2LL * (found->base_radius_millimetres + *minimum_offset
                - capsule_radius_millimetres - safety_margin_millimetres)};
        const std::int64_t maximum_crystal_diameter{
            2LL * (visual.crystal_shape.radius_millimetres + 20)
            * visual.crystal.base_scale_milli * 1'080 / 1'000'000};
        const std::int64_t minimum_crystal_diameter{
            2LL * (visual.crystal_shape.radius_millimetres + 20)
            * visual.crystal.base_scale_milli * 940 / 1'000'000};
        if (visual.element == Element::fire
            || visual.element == Element::earth
            || visual.element == Element::air) {
            const std::int64_t required_diameter{
                visual.element == Element::fire ? 63'000LL : 40'000LL};
            require(usable_diameter >= required_diameter,
                "authored elemental chamber does not preserve its full-scale interior");
        } else {
            require(20LL * maximum_crystal_diameter <= usable_diameter,
                "elemental chamber is less than 20 crystal widths across");
            require(usable_diameter <= 30LL * minimum_crystal_diameter,
                "elemental chamber is more than 30 crystal widths across");
        }
        const std::int64_t maximum_crystal_height{
            static_cast<std::int64_t>(visual.crystal_shape.height_millimetres)
            * visual.crystal.base_scale_milli * 1'080 / 1'000'000};
        const std::int64_t minimum_crystal_height{
            static_cast<std::int64_t>(visual.crystal_shape.height_millimetres)
            * visual.crystal.base_scale_milli * 940 / 1'000'000};
        if (visual.element == Element::fire
            || visual.element == Element::earth
            || visual.element == Element::air) {
            const std::int32_t required_height{
                visual.element == Element::fire ? 23'000
                : visual.element == Element::air ? 22'000 : 10'000};
            require(found->wall_height_millimetres >= required_height,
                "authored elemental chamber ceiling is not high enough");
        } else {
            require(found->wall_height_millimetres >= 4'500,
                "elemental chamber is below the minimum usable height");
            require(5LL * maximum_crystal_height <= found->wall_height_millimetres,
                "elemental chamber is less than five crystal heights tall");
            require(found->wall_height_millimetres <= 8LL * minimum_crystal_height,
                "elemental chamber is more than eight crystal heights tall");
        }
    }
}

void chamber_shells_are_multi_ring_and_elementally_distinct(
    const std::filesystem::path&)
{
    const CaveGenerationResult result{generate_cave({42U})};
    std::set<std::uint8_t> floors;
    std::set<std::uint8_t> shells;
    std::set<std::uint8_t> entrances;
    std::set<std::uint8_t> landmarks;
    std::set<std::uint8_t> vertical_profiles;
    for (const ChamberGeometryContract& chamber : result.scene.chambers) {
        require(chamber.rings.size() == 5U,
            "generated chamber must use five structural rings");
        require(chamber.rings.front().height_millimetres == 0
                && chamber.rings.back().height_millimetres
                    == chamber.wall_height_millimetres,
            "chamber rings do not span floor to ceiling");
        bool saw_per_side_variation{};
        for (const ChamberRingContract& ring : chamber.rings) {
            saw_per_side_variation = saw_per_side_variation
                || *std::min_element(ring.radii_millimetres.begin(),
                       ring.radii_millimetres.end())
                    != *std::max_element(ring.radii_millimetres.begin(),
                       ring.radii_millimetres.end());
        }
        require(saw_per_side_variation,
            "chamber rings collapsed to a uniform radial profile");
        require(*std::min_element(chamber.rings.back().radii_millimetres.begin(),
                    chamber.rings.back().radii_millimetres.end()) >= 1'500,
            "chamber ceiling collapsed to a single apex");

        const auto node = std::find_if(result.generation.topology.nodes.begin(),
            result.generation.topology.nodes.end(), [&](const ChamberNode& candidate) {
                return candidate.id == chamber.node_id;
            });
        if (node != result.generation.topology.nodes.end()
            && node->role == ChamberRole::elemental) {
            floors.insert(static_cast<std::uint8_t>(chamber.identity.floor));
            shells.insert(static_cast<std::uint8_t>(chamber.identity.shell));
            entrances.insert(static_cast<std::uint8_t>(chamber.identity.entrance));
            landmarks.insert(static_cast<std::uint8_t>(chamber.identity.landmark));
            vertical_profiles.insert(chamber.identity.vertical_profile);
        }
    }
    require(floors.size() == 5U && shells.size() == 5U
            && entrances.size() == 5U && landmarks.size() == 5U
            && vertical_profiles.size() == 5U,
        "elemental chambers do not have five pairwise-distinct structural identities");
}

void render_visibility_and_collision_share_chamber_contract(
    const std::filesystem::path&)
{
    const CaveGenerationResult result{generate_cave({42U})};
    const CollisionWorld collision{build_collision_world(result.scene)};
    std::size_t shell_index{};
    for (const ChamberGeometryContract& chamber : result.scene.chambers) {
        while (shell_index < result.scene.mesh_pieces.size()
            && result.scene.mesh_pieces[shell_index].kind
                != ScenePieceKind::chamber_shell) {
            ++shell_index;
        }
        require(shell_index < result.scene.mesh_pieces.size(),
            "chamber has no rendered shell");
        const SceneMeshPiece& shell{result.scene.mesh_pieces[shell_index++]};
        const auto compiled{std::find_if(result.scene.compiled_chambers.begin(),
            result.scene.compiled_chambers.end(),
            [&](const CompiledChamberTemplate& candidate) {
                return candidate.chamber_id == chamber.node_id;
            })};
        require(compiled != result.scene.compiled_chambers.end(),
            "rendered chamber has no compiled template");
        const std::vector<ChamberStructuralTriangle> triangles{
            chamber_structure_triangles(chamber, result.scene.portals)};
        require(shell.mesh.vertices.size() == triangles.size() * 3U,
            "render shell does not consume shared structural triangles");
        const double center_x{
            static_cast<double>(chamber.center_millimetres.x_millimetres)
            / 1'000.0};
        const double center_z{
            static_cast<double>(chamber.center_millimetres.z_millimetres)
            / 1'000.0};
        for (const Vertex& vertex : shell.mesh.vertices) {
            const double inward_x{center_x - vertex.position[0]};
            const double inward_z{center_z - vertex.position[2]};
            const double inward_y{
                static_cast<double>(chamber.center_millimetres.y_millimetres)
                        / 1'000.0
                    + 2.0 - vertex.position[1]};
            require(inward_x * vertex.normal[0]
                        + inward_y * vertex.normal[1]
                        + inward_z * vertex.normal[2]
                    > 0.0,
                "chamber shell normal does not face inward");
        }
        const auto region = std::find_if(collision.chambers.begin(),
            collision.chambers.end(), [&](const ChamberCollisionRegion& candidate) {
                return candidate.chamber_id == chamber.node_id;
            });
        require(region != collision.chambers.end(),
            "chamber has no analytic collision region");
        const double expected{
            static_cast<double>(chamber.minimum_safe_ring_radius_millimetres
                - 350 - 100) / 1'000.0};
        require(std::abs(region->usable_radius_metres - expected) < 1.0e-9,
            "analytic collision diverged from the conservative ring contract");
    }
}

void tunnel_only_collider_guarantees_hold(const std::filesystem::path&)
{
    for (const std::uint64_t seed : {7U, 11U, 123'456'789U}) {
        const CaveGenerationResult result{generate_cave({seed})};
        require(result.scene.bridge_routes.empty(),
            "fixed layout must not select bridge routes");
        for (const RouteGeometryContract& route : result.scene.routes) {
            require(!route.bridge, "fixed layout route is marked as a bridge");
        }
        require(has_collider(result.scene, ColliderKind::chamber_floor), "missing floor collider");
        require(has_collider(result.scene, ColliderKind::chamber_boundary), "missing boundary collider");
        require(has_collider(result.scene, ColliderKind::tunnel), "missing tunnel collider");
        require(!has_collider(result.scene, ColliderKind::bridge_deck),
            "tunnel-only scene has a bridge deck collider");
        require(!has_collider(result.scene, ColliderKind::bridge_rail),
            "tunnel-only scene has a bridge rail collider");
        require(!has_collider(result.scene, ColliderKind::fall_region),
            "tunnel-only scene has a bridge fall region");
    }
}

void portal_and_route_endpoints_agree(const std::filesystem::path&)
{
    const CaveGenerationResult result{generate_cave({123'456'789U})};
    for (const RouteGeometryContract& route : result.scene.routes) {
        const auto first = std::find_if(
            result.scene.portals.begin(), result.scene.portals.end(), [&](const PortalContract& portal) {
                return portal.chamber_id == route.edge.first && portal.route == route.edge;
            });
        const auto second = std::find_if(
            result.scene.portals.begin(), result.scene.portals.end(), [&](const PortalContract& portal) {
                return portal.chamber_id == route.edge.second && portal.route == route.edge;
            });
        require(first != result.scene.portals.end(), "route has no first portal");
        require(second != result.scene.portals.end(), "route has no second portal");
        require(route.spline.control_points.front() == first->center_millimetres, "first seam drifted");
        require(route.spline.control_points.back() == second->center_millimetres, "second seam drifted");
    }
}

void scene_fingerprint_is_sensitive_to_integer_contract(const std::filesystem::path&)
{
    CaveGenerationResult result{generate_cave({123'456'789U})};
    const std::uint64_t fingerprint{result.scene.fingerprint};
    result.scene.chambers.front().base_radius_millimetres += 1;
    require(
        cave_scene_fingerprint(result.generation.effective_seed, result.scene) != fingerprint,
        "integer chamber change did not change scene fingerprint");
}

void geometry_rejections_reach_checked_fallback(const std::filesystem::path&)
{
    CaveGenerationTestSeams seams;
    seams.reject_attempt = [](const std::uint32_t, const CaveSceneData&) {
        return std::optional<std::string>{"forced Step 5B geometry rejection"};
    };
    const CaveGenerationResult result{generate_cave({123'456'789U}, seams)};
    require(result.generation.used_fallback, "eight geometry rejections did not use fallback");
    require(
        result.generation.diagnostics.size() == topology_limits.normal_attempt_count + 1U,
        "geometry retry diagnostic count changed");
    require(
        validate_cave_scene(result.generation.topology, result.scene).empty(),
        "fallback cave did not validate");
}

void invalid_geometry_fallback_fails_atomically(const std::filesystem::path&)
{
    CaveGenerationTestSeams seams;
    seams.reject_attempt = [](const std::uint32_t, const CaveSceneData&) {
        return std::optional<std::string>{"force fallback"};
    };
    seams.fallback_factory = [] {
        TopologyData fallback{known_good_fallback_topology()};
        fallback.nodes[1].anchor.x_millimetres = fallback.nodes[0].anchor.x_millimetres;
        fallback.nodes[1].anchor.z_millimetres = fallback.nodes[0].anchor.z_millimetres;
        return fallback;
    };
    require_throws<GenerationError>(
        [&] { static_cast<void>(generate_cave({99U}, seams)); },
        "fallback cave failed geometry validation",
        "invalid geometry fallback must fail atomically");
}

void start_camera_pose_is_finite(const std::filesystem::path&)
{
    const CaveGenerationResult result{generate_cave({123'456'789U})};
    const GeometryVector3& position{result.scene.start_camera_position_metres};
    const GeometryVector3& forward{result.scene.start_camera_forward};
    require(
        std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z),
        "start camera position is non-finite");
    const double length{std::sqrt(
        forward.x * forward.x + forward.y * forward.y + forward.z * forward.z)};
    require(std::isfinite(length) && std::abs(length - 1.0) < 1.0e-6, "start forward is not unit");
}

void production_rooms_compile_from_template_data(const std::filesystem::path&)
{
    const CaveGenerationResult result{generate_cave({42U})};
    require(result.scene.compiled_chambers.size()
            == result.generation.topology.nodes.size(),
        "production scene omitted a compiled chamber template");
    require(validate_compiled_chamber_templates(result.generation.topology,
                result.scene.compiled_chambers)
            .empty(),
        "production compiled chamber templates failed validation");
    for (const CompiledChamberTemplate& chamber :
        result.scene.compiled_chambers) {
        require(!chamber.floor_patches.empty() && chamber.sockets.size() == 8U
                && !chamber.anchors.empty() && chamber.fingerprint != 0U,
            "compiled chamber projection is incomplete");
    }
}

void ordinary_routes_use_exact_horseshoe_profile(const std::filesystem::path&)
{
    const CaveGenerationResult result{generate_cave({42U})};
    std::size_t ordinary_count{};
    for (const RouteGeometryContract& route : result.scene.routes) {
        require(route.vestibule_length_millimetres
                == route_junction_depth_millimetres,
            "route lost the exact vestibule length");
        require(route.join_overlap_millimetres == route_join_overlap_millimetres,
            "route lost the exact join overlap");
        if (route.bridge) {
            continue;
        }
        ++ordinary_count;
        require(route.tunnel_clear_width_millimetres
                    == tunnel_clear_width_millimetres
                && route.tunnel_side_height_millimetres
                    == tunnel_side_height_millimetres
                && route.tunnel_crown_height_millimetres
                    == tunnel_crown_height_millimetres,
            "ordinary route lost the exact horseshoe profile");
    }
    require(ordinary_count > 0U, "seed 42 contains no ordinary tunnel");
}

void every_fixed_route_emits_one_tunnel(const std::filesystem::path&)
{
    const CaveGenerationResult result{generate_cave({42U})};
    const std::size_t tunnel_count{static_cast<std::size_t>(std::count_if(
        result.scene.mesh_pieces.begin(), result.scene.mesh_pieces.end(),
        [](const SceneMeshPiece& piece) {
            return piece.kind == ScenePieceKind::tunnel;
        }))};
    const std::size_t tunnel_collider_count{static_cast<std::size_t>(
        std::count_if(result.scene.colliders.begin(), result.scene.colliders.end(),
            [](const SceneCollider& collider) {
                return collider.kind == ColliderKind::tunnel;
            }))};
    require(result.scene.bridge_routes.empty(),
        "fixed layout selected a bridge route");
    require(tunnel_count == result.scene.routes.size() + result.scene.maze_rooms.size(),
        "maze routes did not emit two tightly joined tunnel segments");
    require(tunnel_collider_count == tunnel_count,
        "a fixed route did not emit exactly one tunnel collider");
}

void arch_vestibule_route_seams_are_exact(const std::filesystem::path&)
{
    const CaveGenerationResult result{generate_cave({42U})};
    require(validate_cave_scene(result.generation.topology, result.scene).empty(),
        "canonical vestibule seams do not validate");
    CaveSceneData shifted{result.scene};
    shifted.portals.front().center_millimetres.x_millimetres += 26;
    require(contains_error(validate_cave_scene(result.generation.topology, shifted),
                "Route endpoints"),
        "a 26 mm route seam error did not make validation red");
}

void fixed_routes_form_one_linear_chain(const std::filesystem::path&)
{
    for (std::uint64_t seed{}; seed < 256U; ++seed) {
        const CaveGenerationResult result{generate_cave({seed})};
        require(result.scene.routes.size() == 6U,
            "fixed route chain does not contain six links");
        for (std::size_t index{}; index < result.scene.routes.size(); ++index) {
            const Edge expected{NodeId{static_cast<std::uint32_t>(index)},
                NodeId{static_cast<std::uint32_t>(index + 1U)}};
            require(result.scene.routes[index].edge == expected,
                "fixed route chain changed order");
        }
    }
}

void bridge_selection_rotation_is_exact(const std::filesystem::path&)
{
    require(rotate_left_64(0x8000'0000'0000'0001ULL) == 3ULL,
        "bridge score rotate-left contract changed");
}

void sealed_sockets_block_and_active_sockets_connect(const std::filesystem::path&)
{
    const CaveGenerationResult result{generate_cave({42U})};
    const CollisionWorld world{build_collision_world(result.scene)};
    const auto start{std::find_if(result.scene.compiled_chambers.begin(),
        result.scene.compiled_chambers.end(), [](const CompiledChamberTemplate& chamber) {
            return chamber.role == ChamberTemplateRole::start;
        })};
    require(start != result.scene.compiled_chambers.end(),
        "compiled Start chamber is missing");
    bool checked_active{};
    bool checked_sealed{};
    for (const CompiledTemplateSocket& socket : start->sockets) {
        const GeometryVector3 origin{
            socket.world_vestibule_inner_millimetres.x_millimetres / 1'000.0,
            result.generation.topology.nodes[start->chamber_id.value]
                    .anchor.elevation_millimetres
                / 1'000.0,
            socket.world_vestibule_inner_millimetres.z_millimetres / 1'000.0};
        const CollisionProbe probe{probe_collision_world(
            world, locked_player_capsule(), origin, 0.30)};
        if (socket.active) {
            checked_active = checked_active || probe.supported;
        } else if (!checked_sealed) {
            const GeometryVector3 outside{
                socket.world_origin_millimetres.x_millimetres / 1'000.0
                    + socket.world_outward_direction_million.x_millimetres
                        / 1'000'000.0,
                origin.y,
                socket.world_origin_millimetres.z_millimetres / 1'000.0
                    + socket.world_outward_direction_million.z_millimetres
                        / 1'000'000.0};
            checked_sealed = !probe_collision_world(world, locked_player_capsule(),
                                  outside, 0.30)
                                  .supported;
        }
    }
    require(checked_active, "active socket seam has no support");
    require(checked_sealed, "sealed socket leaked capsule support");
}

void support_arbitration_is_stable(const std::filesystem::path&)
{
    const CaveGenerationResult result{generate_cave({42U})};
    CollisionWorld world{build_collision_world(result.scene)};
    const auto start{std::find_if(result.generation.topology.nodes.begin(),
        result.generation.topology.nodes.end(), [](const ChamberNode& node) {
            return node.role == ChamberRole::start;
        })};
    require(start != result.generation.topology.nodes.end(), "Start is missing");
    const auto base{std::find_if(world.chamber_supports.begin(),
        world.chamber_supports.end(), [&](const ChamberSupportRegion& support) {
            return support.chamber_id == start->id;
        })};
    require(base != world.chamber_supports.end(), "Start template support is missing");
    const GeometryVector3 feet{start->anchor.x_millimetres / 1'000.0,
        start->anchor.elevation_millimetres / 1'000.0,
        start->anchor.z_millimetres / 1'000.0};
    ChamberSupportRegion raised{*base};
    raised.stable_object_id = 2U;
    raised.floor_height_metres = feet.y + 0.20;
    raised.ceiling_height_metres = raised.floor_height_metres + 3.0;
    const std::int32_t center_x{start->anchor.x_millimetres};
    const std::int32_t center_z{start->anchor.z_millimetres};
    constexpr std::int32_t half_extent_millimetres{1'000};
    raised.world_polygon_millimetres = {
        {center_x - half_extent_millimetres,
            center_z - half_extent_millimetres},
        {center_x - half_extent_millimetres,
            center_z + half_extent_millimetres},
        {center_x + half_extent_millimetres,
            center_z + half_extent_millimetres},
        {center_x + half_extent_millimetres,
            center_z - half_extent_millimetres},
    };
    ChamberSupportRegion tied{raised};
    tied.stable_object_id = 1U;
    CollisionWorld arbitration_world;
    arbitration_world.chamber_supports.push_back(raised);
    arbitration_world.chamber_supports.push_back(tied);
    const CollisionProbe probe{
        probe_collision_world(
            arbitration_world, locked_player_capsule(), feet, 0.30)};
    require(probe.supported && probe.stable_object_id == 1U
            && std::abs(probe.floor_height_metres
                    - (feet.y + 0.20))
                < 1.0e-9,
        "support arbitration did not choose highest then smallest stable ID");
}

void template_hazards_and_boundaries_reach_runtime(const std::filesystem::path&)
{
    const CaveGenerationResult result{generate_cave({42U})};
    const CollisionWorld world{build_collision_world(result.scene)};
    bool saw_fire{};
    bool saw_visual_only_aether{};
    bool saw_water_supports{};
    bool checked_air{};
    for (const CompiledChamberTemplate& chamber : result.scene.compiled_chambers) {
        if (chamber.role == ChamberTemplateRole::water) {
            saw_water_supports = chamber.hazards.empty()
                && chamber.floor_patches.size() == 17U;
        }
        if (chamber.role == ChamberTemplateRole::air) {
            const CompiledTemplateFloorPatch& floor{chamber.floor_patches.front()};
            GeometryVector3 center{};
            for (const TemplatePoint2 point : floor.world_polygon_millimetres) {
                center.x += point.x_millimetres / 1'000.0;
                center.z += point.z_millimetres / 1'000.0;
            }
            center.x /= floor.world_polygon_millimetres.size();
            center.y = floor.support_height_millimetres / 1'000.0;
            center.z /= floor.world_polygon_millimetres.size();
            checked_air = !intersects_fall_region(world, center);
        }
        if (chamber.role == ChamberTemplateRole::aether) {
            saw_visual_only_aether = chamber.hazards.empty();
            continue;
        }
        if (chamber.role != ChamberTemplateRole::fire) {
            continue;
        }
        require(!chamber.hazards.empty(),
            "hazard chamber lost its authoritative hazard volumes");
        const CompiledTemplateHazardVolume& hazard{chamber.hazards.front()};
        GeometryVector3 center{};
        for (const TemplatePoint2 point : hazard.world_polygon_millimetres) {
            center.x += point.x_millimetres / 1'000.0;
            center.z += point.z_millimetres / 1'000.0;
        }
        center.x /= hazard.world_polygon_millimetres.size();
        center.y = (hazard.minimum_y_millimetres
            + hazard.maximum_y_millimetres) / 2'000.0;
        center.z /= hazard.world_polygon_millimetres.size();
        require(intersects_fall_region(world, center),
            "template hazard did not reach the runtime respawn query");
        saw_fire = saw_fire || chamber.role == ChamberTemplateRole::fire;
    }
    require(saw_fire, "Fire lava hazard coverage is missing");
    require(saw_visual_only_aether,
        "Aether chamber unexpectedly retained a gameplay hazard");
    require(saw_water_supports,
        "authored Water floor, landings, or stairs are missing");
    require(checked_air, "Air upper fall incorrectly became a respawn hazard");
}

void render_surfaces_agree_with_hazards(const std::filesystem::path&)
{
    const CaveGenerationResult result{generate_cave({42U})};
    require(validate_cave_scene(result.generation.topology, result.scene).empty(),
        "valid template render/hazard projection did not validate");
    CaveSceneData missing_surface{result.scene};
    const auto hazard_piece{std::find_if(missing_surface.mesh_pieces.begin(),
        missing_surface.mesh_pieces.end(), [](const SceneMeshPiece& piece) {
            return piece.kind == ScenePieceKind::chamber_hazard;
        })};
    require(hazard_piece != missing_surface.mesh_pieces.end(),
        "production scene did not render template hazards");
    missing_surface.static_vertex_count -=
        static_cast<std::uint32_t>(hazard_piece->mesh.vertices.size());
    --missing_surface.opaque_draw_call_count;
    missing_surface.mesh_pieces.erase(hazard_piece);
    require(contains_error(validate_cave_scene(result.generation.topology,
                missing_surface), "hazard render surfaces"),
        "missing hazard render surface did not make validation red");
}

void start_camera_targets_canonical_active_socket(const std::filesystem::path&)
{
    const CaveGenerationResult result{generate_cave({42U})};
    const auto start{std::find_if(result.scene.compiled_chambers.begin(),
        result.scene.compiled_chambers.end(), [](const CompiledChamberTemplate& chamber) {
            return chamber.role == ChamberTemplateRole::start;
        })};
    require(start != result.scene.compiled_chambers.end(),
        "compiled Start template is missing");
    const auto socket{std::min_element(start->sockets.begin(), start->sockets.end(),
        [](const CompiledTemplateSocket& left,
            const CompiledTemplateSocket& right) {
            if (!left.active) {
                return false;
            }
            if (!right.active) {
                return true;
            }
            return *left.route < *right.route;
        })};
    require(socket != start->sockets.end() && socket->active,
        "Start has no active canonical socket");
    GeometryVector3 expected{
        socket->world_origin_millimetres.x_millimetres / 1'000.0
            - result.scene.start_camera_position_metres.x,
        0.0,
        socket->world_origin_millimetres.z_millimetres / 1'000.0
            - result.scene.start_camera_position_metres.z};
    const double length{std::hypot(expected.x, expected.z)};
    expected.x /= length;
    expected.z /= length;
    require(expected.x * result.scene.start_camera_forward.x
                + expected.z * result.scene.start_camera_forward.z
            > 0.999999,
        "Start camera does not target its canonical active route socket");
}

void authored_water_routes_are_straight_and_level(
    const std::filesystem::path&)
{
    const CaveGenerationResult result{generate_cave({42U})};
    const auto water{std::find_if(result.generation.topology.nodes.begin(),
        result.generation.topology.nodes.end(), [](const ChamberNode& node) {
            return node.element == std::optional<Element>{Element::water};
        })};
    require(water != result.generation.topology.nodes.end(),
        "seed 42 has no Water chamber");

    std::size_t connection_count{};
    for (const RouteGeometryContract& route : result.scene.routes) {
        if (route.edge.first != water->id && route.edge.second != water->id) {
            continue;
        }
        ++connection_count;
        const IntegerPoint3& first{route.spline.control_points.front()};
        const IntegerPoint3& last{route.spline.control_points.back()};
        const std::int64_t dx{
            static_cast<std::int64_t>(last.x_millimetres)
            - first.x_millimetres};
        const std::int64_t dz{
            static_cast<std::int64_t>(last.z_millimetres)
            - first.z_millimetres};
        for (const IntegerPoint3& point : route.spline.control_points) {
            const std::int64_t point_x{
                static_cast<std::int64_t>(point.x_millimetres)
                - first.x_millimetres};
            const std::int64_t point_z{
                static_cast<std::int64_t>(point.z_millimetres)
                - first.z_millimetres};
            require(point.y_millimetres == first.y_millimetres,
                "Water connection changes elevation");
            require(dx * point_z - dz * point_x == 0,
                "Water connection is not straight");
        }
    }
    require(connection_count == 2U,
        "seed 42 must exercise both Water chamber connections");
}

void earth_water_tunnel_junctions_overlap_the_curved_route(
    const std::filesystem::path&)
{
    const CaveGenerationResult result{generate_cave({42U})};
    const auto element_node = [&](const Element element) -> const ChamberNode& {
        const auto found{std::find_if(result.generation.topology.nodes.begin(),
            result.generation.topology.nodes.end(),
            [element](const ChamberNode& node) {
                return node.element == std::optional<Element>{element};
            })};
        require(found != result.generation.topology.nodes.end(),
            "seed 42 is missing an elemental chamber");
        return *found;
    };
    const Edge earth_water{make_edge(
        element_node(Element::earth).id, element_node(Element::water).id)};
    const auto route{std::find_if(result.scene.routes.begin(),
        result.scene.routes.end(), [earth_water](const RouteGeometryContract& candidate) {
            return candidate.edge == earth_water;
        })};
    require(route != result.scene.routes.end() && !route->bridge,
        "seed 42 must have an ordinary Earth-Water tunnel");

    std::vector<const SceneMeshPiece*> junctions;
    for (const SceneMeshPiece& piece : result.scene.mesh_pieces) {
        if (piece.kind == ScenePieceKind::junction) {
            junctions.push_back(&piece);
        }
    }
    require(junctions.size() == result.scene.portals.size(),
        "every portal must retain one ordered junction mesh");

    std::size_t checked{};
    constexpr double projection_tolerance_metres{0.005};
    for (std::size_t index{}; index < result.scene.portals.size(); ++index) {
        const PortalContract& portal{result.scene.portals[index]};
        if (portal.route != earth_water) {
            continue;
        }
        const double inward_x{
            static_cast<double>(portal.inward_direction_millimetres.x_millimetres)};
        const double inward_z{
            static_cast<double>(portal.inward_direction_millimetres.z_millimetres)};
        const double inward_length{std::hypot(inward_x, inward_z)};
        require(inward_length > 0.0, "Earth-Water portal direction is invalid");
        const double portal_x{portal.center_millimetres.x_millimetres / 1'000.0};
        const double portal_z{portal.center_millimetres.z_millimetres / 1'000.0};
        double maximum_outward_projection{};
        for (const Vertex& vertex : junctions[index]->mesh.vertices) {
            const double dx{static_cast<double>(vertex.position[0]) - portal_x};
            const double dz{static_cast<double>(vertex.position[2]) - portal_z};
            maximum_outward_projection = std::max(maximum_outward_projection,
                -(dx * inward_x + dz * inward_z) / inward_length);
        }
        require(maximum_outward_projection + projection_tolerance_metres
                >= route->join_overlap_millimetres / 1'000.0,
            "Earth-Water junction stops at the curved tunnel endpoint without overlap");
        ++checked;
    }
    require(checked == 2U,
        "Earth-Water route must check both chamber junctions");
}

void exported_layout_uses_six_flat_tunnels_and_no_bridges(
    const std::filesystem::path&)
{
    const CaveGenerationResult result{generate_cave({42U})};
    require(result.scene.routes.size() == 6U,
        "exported layout must contain six temporary connectors");
    require(result.scene.bridge_routes.empty(),
        "exported layout must not select any bridge routes");
    require(!has_piece(result.scene, ScenePieceKind::bridge),
        "exported layout emitted a bridge mesh");
    require(!has_collider(result.scene, ColliderKind::bridge_deck)
            && !has_collider(result.scene, ColliderKind::bridge_rail)
            && !has_collider(result.scene, ColliderKind::fall_region),
        "exported layout emitted bridge-only collision");

    constexpr std::int32_t expected_center_y_millimetres{1'350};
    for (const RouteGeometryContract& route : result.scene.routes) {
        require(!route.bridge, "temporary connector is marked as a bridge");
        require(route.spline.control_points.size() == 4U,
            "temporary straight tunnel must retain four spline control points");
        const IntegerPoint3& first{route.spline.control_points.front()};
        const IntegerPoint3& last{route.spline.control_points.back()};
        const std::int64_t dx{
            static_cast<std::int64_t>(last.x_millimetres)
            - first.x_millimetres};
        const std::int64_t dz{
            static_cast<std::int64_t>(last.z_millimetres)
            - first.z_millimetres};
        for (const IntegerPoint3& point : route.spline.control_points) {
            require(point.y_millimetres == expected_center_y_millimetres,
                "tunnel centerline changed elevation");
            const std::int64_t point_x{
                static_cast<std::int64_t>(point.x_millimetres)
                - first.x_millimetres};
            const std::int64_t point_z{
                static_cast<std::int64_t>(point.z_millimetres)
                - first.z_millimetres};
            require(dx * point_z - dz * point_x == 0,
                "temporary tunnel centerline is not straight");
        }
    }
    for (const PortalContract& portal : result.scene.portals) {
        require(portal.center_millimetres.y_millimetres
                == expected_center_y_millimetres,
            "authored chamber entrance does not meet the shared tunnel height");
    }
}

void fixed_layout_inserts_exactly_five_maze_rooms(
    const std::filesystem::path&)
{
    const CaveGenerationResult result{generate_cave({42U})};
    const CaveGenerationResult repeated{generate_cave({42U})};
    const CaveGenerationResult alternate{generate_cave({43U})};
    require(result.scene.maze_rooms.size() == fixed_maze_room_count,
        "fixed route must insert exactly five maze rooms");
    require(result.scene.routes.size() == fixed_maze_room_count + 1U,
        "maze-room contract requires six fixed route legs");
    for (std::size_t index{}; index < result.scene.maze_rooms.size(); ++index) {
        const MazeRoomContract& room{result.scene.maze_rooms[index]};
        require(room.route == result.scene.routes[index].edge,
            "maze room is not assigned to its ordered route leg");
        require(room.ordinal == index
                && room.columns == maze_grid_columns
                && room.rows == maze_grid_rows,
            "maze room grid contract changed");
        require(!room.walls.empty(), "maze room contains no generated walls");
        require(room.fingerprint == repeated.scene.maze_rooms[index].fingerprint,
            "same seed changed a maze layout");
        require(!room.solution_cells.empty()
                && room.solution_cells.front()
                    == MazeCellCoordinate{maze_grid_columns / 2U, maze_grid_rows - 1U}
                && room.solution_cells.back()
                    == MazeCellCoordinate{maze_grid_columns / 2U, 0U},
            "maze room has no south-door to north-door solution");
        const auto segments{maze_tunnel_segments(result.scene.routes[index], room)};
        const IntegerPoint3 expected_first{maze_local_to_world(room, 0,
            result.scene.routes[index].spline.radius_millimetres,
            maze_room_connector_half_length_millimetres
                - maze_tunnel_overlap_millimetres)};
        const IntegerPoint3 expected_second{maze_local_to_world(room, 0,
            result.scene.routes[index].spline.radius_millimetres,
            -maze_room_connector_half_length_millimetres
                + maze_tunnel_overlap_millimetres)};
        require(segments[0].spline.control_points.back() == expected_first
                && segments[1].spline.control_points.front() == expected_second,
            "maze tunnel does not overlap both connector floors by exactly 100 mm");
        const auto axis_distance = [](const IntegerPoint3& first,
                                       const IntegerPoint3& second) {
            const std::int64_t dx{
                static_cast<std::int64_t>(second.x_millimetres)
                    - first.x_millimetres};
            const std::int64_t dz{
                static_cast<std::int64_t>(second.z_millimetres)
                    - first.z_millimetres};
            return std::max(dx < 0 ? -dx : dx, dz < 0 ? -dz : dz);
        };
        require(axis_distance(segments[0].spline.control_points.front(),
                    segments[0].spline.control_points.back())
                    - maze_tunnel_overlap_millimetres
                >= maze_minimum_tunnel_run_millimetres
                && axis_distance(segments[1].spline.control_points.front(),
                       segments[1].spline.control_points.back())
                        - maze_tunnel_overlap_millimetres
                    >= maze_minimum_tunnel_run_millimetres,
            "maze room leaves too little visible tunnel beside a chamber");
    }
    require(std::any_of(result.scene.maze_rooms.begin(), result.scene.maze_rooms.end(),
                [&](const MazeRoomContract& room) {
                    const MazeRoomContract& other{
                        alternate.scene.maze_rooms[room.ordinal]};
                    return room.fingerprint != other.fingerprint;
                }),
        "different seeds did not vary any maze layout");
    require(std::none_of(result.scene.maze_rooms.begin(),
                result.scene.maze_rooms.end(),
                [&](const MazeRoomContract& room) {
                    return room.route == result.scene.routes.back().edge;
                }),
        "Aether-to-Exit route must not contain a sixth maze room");
}

void generated_maze_authored_instances_follow_walls_and_thresholds(
    const std::filesystem::path&)
{
    const CaveGenerationResult result{generate_cave({42U})};
    const CollisionWorld collision{build_collision_world(result.scene)};
    const PlayerCapsule capsule{locked_player_capsule()};
    for (const MazeRoomContract& room : result.scene.maze_rooms) {
        const std::vector<MazeAuthoredModelInstance> instances{
            maze_authored_model_instances(room)};
        require(!instances.empty(),
            "generated maze has no authored model instances");
        const std::size_t wall_count{static_cast<std::size_t>(std::count_if(
            instances.begin(), instances.end(), [](const auto& instance) {
                return instance.kind == MazeAuthoredModelKind::wall;
            }))};
        const std::size_t pillar_count{static_cast<std::size_t>(std::count_if(
            instances.begin(), instances.end(), [](const auto& instance) {
                return instance.kind == MazeAuthoredModelKind::pillar;
            }))};
        const std::size_t arch_count{static_cast<std::size_t>(std::count_if(
            instances.begin(), instances.end(), [](const auto& instance) {
                return instance.kind == MazeAuthoredModelKind::arch;
            }))};
        require(wall_count == room.walls.size(),
            "every generated maze wall must receive one authored wall model");
        require(pillar_count > 0U,
            "generated maze wall endpoints must receive authored pillars");
        require(arch_count == 2U,
            "each maze room must receive one authored arch at each threshold");

        std::set<std::uint64_t> stable_ids;
        std::set<std::pair<std::int32_t, std::int32_t>> pillar_centers;
        std::set<std::int32_t> arch_thresholds;
        for (const MazeAuthoredModelInstance& instance : instances) {
            require(stable_ids.insert(instance.stable_object_id).second,
                "authored maze model instance IDs must be unique");
            require(instance.local_yaw_quarter_turns < 4U,
                "authored maze model yaw must use a canonical quarter turn");
            if (instance.kind == MazeAuthoredModelKind::wall) {
                const auto wall{std::find_if(room.walls.begin(), room.walls.end(),
                    [&](const MazeWallContract& candidate) {
                        return candidate.stable_object_id
                            == instance.stable_object_id;
                    })};
                require(wall != room.walls.end()
                        && wall->center_x_millimetres
                            == instance.center_x_millimetres
                        && wall->center_z_millimetres
                            == instance.center_z_millimetres
                        && wall->length_millimetres
                            == instance.target_length_millimetres,
                    "authored wall model drifted from its collision wall");
                const MazeAuthoredModelPlacement placement{
                    maze_authored_model_placement(room, instance)};
                const IntegerPoint3 expected{maze_local_to_world(room,
                    instance.center_x_millimetres, 0,
                    instance.center_z_millimetres)};
                require(std::abs(placement.translation_metres.x
                            - expected.x_millimetres / 1'000.0)
                            <= 0.000'001
                        && std::abs(placement.translation_metres.y
                            - expected.y_millimetres / 1'000.0)
                            <= 0.000'001
                        && std::abs(placement.translation_metres.z
                            - expected.z_millimetres / 1'000.0)
                            <= 0.000'001
                        && std::abs(placement.scale.x
                            - instance.target_length_millimetres / 4'000.0)
                            <= 0.000'001,
                    "authored wall world transform drifted from generation");
                require(!probe_collision_world(collision, capsule,
                            placement.translation_metres, 0.30)
                            .supported,
                    "authored maze wall can be walked through");
            } else if (instance.kind == MazeAuthoredModelKind::pillar) {
                require(instance.target_length_millimetres == 0
                        && pillar_centers.insert({instance.center_x_millimetres,
                            instance.center_z_millimetres}).second,
                    "authored maze pillars must be unique unscaled endpoint caps");
                require(std::any_of(collision.chamber_blockers.begin(),
                            collision.chamber_blockers.end(),
                            [&](const ChamberBlockerRegion& blocker) {
                                return blocker.stable_object_id
                                    == instance.stable_object_id;
                            }),
                    "authored maze pillar has no matching solid blocker");
                const MazeAuthoredModelPlacement placement{
                    maze_authored_model_placement(room, instance)};
                require(!probe_collision_world(collision, capsule,
                            placement.translation_metres, 0.30)
                            .supported,
                    "authored maze pillar can be walked through");
            } else {
                require(instance.center_x_millimetres == 0
                        && instance.target_length_millimetres == 0,
                    "authored maze arches must remain centered and unscaled");
                arch_thresholds.insert(instance.center_z_millimetres);
                const MazeAuthoredModelPlacement placement{
                    maze_authored_model_placement(room, instance)};
                require(probe_collision_world(collision, capsule,
                            placement.translation_metres, 0.30)
                            .supported,
                    "authored maze arch threshold contains a floor gap");
                constexpr std::int32_t jamb_center_x_millimetres{1'830};
                constexpr std::int32_t jamb_depth_sample_millimetres{700};
                for (const std::int32_t local_x
                    : {-jamb_center_x_millimetres,
                        jamb_center_x_millimetres}) {
                    for (const std::int32_t local_z_offset
                        : {-jamb_depth_sample_millimetres,
                            jamb_depth_sample_millimetres}) {
                        const IntegerPoint3 sample{maze_local_to_world(room,
                            local_x, 0,
                            instance.center_z_millimetres + local_z_offset)};
                        const GeometryVector3 feet{
                            sample.x_millimetres / 1'000.0,
                            sample.y_millimetres / 1'000.0,
                            sample.z_millimetres / 1'000.0};
                        require(!probe_collision_world(
                                    collision, capsule, feet, 0.30)
                                    .supported,
                            "authored maze arch jamb can be walked through");
                    }
                }
                for (const std::int32_t local_z_offset
                    : {-jamb_depth_sample_millimetres,
                        jamb_depth_sample_millimetres}) {
                    const IntegerPoint3 sample{maze_local_to_world(room,
                        0, 0,
                        instance.center_z_millimetres + local_z_offset)};
                    const GeometryVector3 feet{
                        sample.x_millimetres / 1'000.0,
                        sample.y_millimetres / 1'000.0,
                        sample.z_millimetres / 1'000.0};
                    require(probe_collision_world(
                                collision, capsule, feet, 0.30)
                                .supported,
                        "authored maze arch opening is not walkable");
                }
            }
        }
        require(arch_thresholds == std::set<std::int32_t>{
                    -maze_room_core_half_length_millimetres,
                    maze_room_core_half_length_millimetres},
            "authored maze arches must straddle both connector thresholds");

        const std::vector<MazeAuthoredModelInstance> repeated{
            maze_authored_model_instances(room)};
        require(instances.size() == repeated.size(),
            "authored maze instance count is not deterministic");
        for (std::size_t index{}; index < instances.size(); ++index) {
            const auto& first{instances[index]};
            const auto& second{repeated[index]};
            require(first.kind == second.kind
                    && first.stable_object_id == second.stable_object_id
                    && first.center_x_millimetres == second.center_x_millimetres
                    && first.center_z_millimetres == second.center_z_millimetres
                    && first.local_yaw_quarter_turns
                        == second.local_yaw_quarter_turns
                    && first.target_length_millimetres
                        == second.target_length_millimetres,
                "authored maze instance placement is not deterministic");
        }
    }
}

}  // namespace

std::vector<TestCase> cave_scene_test_cases()
{
    return {
        {"fixed seed builds complete cave scene", fixed_seed_builds_complete_scene},
        {"same seed repeats cave scene contract", same_seed_repeats_scene_contract},
        {"representative seeds vary cave scene", representative_seeds_vary},
        {"scene meshes are valid and budgeted", geometry_pieces_are_valid_and_budgeted},
        {"elemental chambers obey spatial scale contract",
            elemental_chambers_obey_player_relative_spatial_contract},
        {"chamber shells are multi-ring and elementally distinct",
            chamber_shells_are_multi_ring_and_elementally_distinct},
        {"render visibility and collision share chamber contract",
            render_visibility_and_collision_share_chamber_contract},
        {"tunnel-only collider guarantees hold", tunnel_only_collider_guarantees_hold},
        {"ordinary routes use exact horseshoe profile",
            ordinary_routes_use_exact_horseshoe_profile},
        {"every fixed route emits one tunnel mesh and collider",
            every_fixed_route_emits_one_tunnel},
        {"arch vestibule route seams are watertight and supported",
            arch_vestibule_route_seams_are_exact},
        {"fixed routes form one linear chain",
            fixed_routes_form_one_linear_chain},
        {"bridge selection rotate-left is exact",
            bridge_selection_rotation_is_exact},
        {"portal and route endpoints agree", portal_and_route_endpoints_agree},
        {"scene fingerprint is contract-sensitive", scene_fingerprint_is_sensitive_to_integer_contract},
        {"geometry rejections use checked fallback", geometry_rejections_reach_checked_fallback},
        {"invalid geometry fallback fails atomically", invalid_geometry_fallback_fails_atomically},
        {"start camera pose is finite", start_camera_pose_is_finite},
        {"production rooms compile from template data", production_rooms_compile_from_template_data},
        {"sealed sockets block render LOS and capsule", sealed_sockets_block_and_active_sockets_connect},
        {"support arbitration is stable", support_arbitration_is_stable},
        {"template hazards and boundaries reach runtime", template_hazards_and_boundaries_reach_runtime},
        {"render surfaces agree with hazards", render_surfaces_agree_with_hazards},
        {"Start camera targets canonical active route socket", start_camera_targets_canonical_active_socket},
        {"authored Water routes are straight and level",
            authored_water_routes_are_straight_and_level},
        {"Earth-Water tunnel junctions overlap the curved route",
            earth_water_tunnel_junctions_overlap_the_curved_route},
        {"exported layout uses six flat tunnels and no bridges",
            exported_layout_uses_six_flat_tunnels_and_no_bridges},
        {"fixed layout inserts exactly five solvable maze rooms",
            fixed_layout_inserts_exactly_five_maze_rooms},
        {"generated maze authored instances follow walls and thresholds",
            generated_maze_authored_instances_follow_walls_and_thresholds},
    };
}

}  // namespace crystalbound::test
