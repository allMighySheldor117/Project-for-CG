#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <glm/mat4x4.hpp>

#include "crystalbound/AuthoredChamber.hpp"
#include "crystalbound/Camera.hpp"
#include "crystalbound/ChamberTemplates.hpp"
#include "crystalbound/CrystalCollection.hpp"
#include "crystalbound/ExitArch.hpp"
#include "crystalbound/MeshData.hpp"
#include "crystalbound/ObjLoader.hpp"
#include "GeometryTests.hpp"
#include "GameLoopTests.hpp"
#include "CaveSceneTests.hpp"
#include "ElementalVisualsTests.hpp"
#include "FrameProfileTests.hpp"
#include "CrystalCollectionTests.hpp"
#include "GenerationTests.hpp"
#include "PlayerControllerTests.hpp"
#include "ReachabilityTests.hpp"
#include "RenderingTests.hpp"
#include "SeedCorpusTests.hpp"

namespace {

using crystalbound::MeshData;
using crystalbound::MaterialModelLoadResult;
using crystalbound::ModelLoadError;
using crystalbound::ModelLoadResult;
using crystalbound::Vertex;
using crystalbound::test::TestCase;

constexpr float tolerance{1.0e-5F};

constexpr std::array<std::string_view, 9> deferred_golden_tests{{
    "fixed seed matches topology fingerprint",
    "geometry contract fingerprint is stable",
    "fixed seed builds complete cave scene",
    "reference golden seeds hold",
    "fixed-layout seed contracts remain unchanged",
    "structural and collision contracts remain unchanged",
    "structural and seed contracts are preserved",
    "Step 9 preserves structural contracts",
    "complete corpus validates reference results",
}};

[[nodiscard]] bool is_deferred_golden_test(const std::string_view name) noexcept
{
    return std::find(
        deferred_golden_tests.begin(), deferred_golden_tests.end(), name)
        != deferred_golden_tests.end();
}

class TestFailure final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

void require(const bool condition, const std::string_view message)
{
    if (!condition) {
        throw TestFailure{std::string{message}};
    }
}

void require_near(
    const float actual,
    const float expected,
    const std::string_view message,
    const float allowed_error = tolerance)
{
    if (!std::isfinite(actual) || std::abs(actual - expected) > allowed_error) {
        throw TestFailure{std::string{message} + ": expected "
            + std::to_string(expected) + ", got " + std::to_string(actual)};
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
            throw TestFailure{std::string{failure_message} + ": unexpected message: "
                + error.what()};
        }
        return;
    } catch (const std::exception& error) {
        throw TestFailure{std::string{failure_message} + ": wrong exception type: "
            + error.what()};
    }
    throw TestFailure{std::string{failure_message} + ": no exception was thrown"};
}

[[nodiscard]] std::filesystem::path fixture(
    const std::filesystem::path& fixture_root,
    const std::string_view name)
{
    return fixture_root / std::filesystem::path{name};
}

[[nodiscard]] bool finite_vector(const std::array<float, 3>& value)
{
    return std::all_of(value.begin(), value.end(), [](const float component) {
        return std::isfinite(component);
    });
}

[[nodiscard]] float squared_length(const std::array<float, 3>& value)
{
    return value[0] * value[0] + value[1] * value[1] + value[2] * value[2];
}

void require_valid_mesh(const MeshData& mesh)
{
    require(!mesh.vertices.empty(), "mesh must contain vertices");
    require(!mesh.indices.empty(), "mesh must contain indices");
    require(mesh.indices.size() % 3 == 0, "indices must form triangles");
    for (const Vertex& vertex : mesh.vertices) {
        require(finite_vector(vertex.position), "vertex position must be finite");
        require(finite_vector(vertex.normal), "vertex normal must be finite");
        require_near(squared_length(vertex.normal), 1.0F, "normal must be unit length", 1.0e-3F);
    }
    for (const std::uint32_t index : mesh.indices) {
        require(index < mesh.vertices.size(), "mesh index must be in range");
    }
    crystalbound::validate_mesh_data(mesh);
}

void require_centered_and_normalized(const MeshData& mesh)
{
    std::array<float, 3> minimum{
        std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::infinity(),
    };
    std::array<float, 3> maximum{
        -minimum[0],
        -minimum[1],
        -minimum[2],
    };
    for (const Vertex& vertex : mesh.vertices) {
        for (std::size_t axis{}; axis < 3; ++axis) {
            minimum[axis] = std::min(minimum[axis], vertex.position[axis]);
            maximum[axis] = std::max(maximum[axis], vertex.position[axis]);
        }
    }
    float largest_extent{};
    for (std::size_t axis{}; axis < 3; ++axis) {
        require_near(
            (minimum[axis] + maximum[axis]) * 0.5F,
            0.0F,
            "normalized bounds must be centered");
        largest_extent = std::max(largest_extent, maximum[axis] - minimum[axis]);
    }
    require_near(largest_extent, 2.0F, "largest normalized extent must equal two");
}

void valid_obj_loads(const std::filesystem::path& root)
{
    const ModelLoadResult result = crystalbound::load_obj(fixture(root, "valid.obj"));
    require(result.mesh.vertices.size() == 3, "valid triangle must have three vertices");
    require(result.mesh.indices.size() == 3, "valid triangle must have three indices");
    require(result.warnings.empty(), "valid triangle must not produce warnings");
    require_valid_mesh(result.mesh);
    require_centered_and_normalized(result.mesh);
}

void material_batches_preserve_authored_scale(const std::filesystem::path& root)
{
    const MaterialModelLoadResult result{
        crystalbound::load_obj_material_batches(
            fixture(root, "material-batches.obj"))};
    require(result.batches.size() == 2, "two authored materials must produce two batches");
    require(result.objects.size() == 2, "two named objects must retain two object records");
    require(result.objects[0].name == "RedTriangle", "first object name must be retained");
    require_near(
        result.objects[1].maximum_bounds[0],
        14.0F,
        "per-object authored bounds must be retained");
    require(result.batches[0].material_name == "Red", "first material name must be retained");
    require(result.batches[1].material_name == "Blue", "second material name must be retained");
    require_near(result.minimum_bounds[0], 10.0F, "authored minimum x must be preserved");
    require_near(result.minimum_bounds[1], 0.0F, "authored minimum y must be preserved");
    require_near(result.maximum_bounds[0], 14.0F, "authored maximum x must be preserved");
    require_near(result.maximum_bounds[1], 2.0F, "authored maximum y must be preserved");
    require_near(result.batches[0].diffuse[0], 0.8F, "material diffuse color must load");
    require_near(result.batches[1].emission[2], 0.3F, "material emission must load");
    for (const auto& batch : result.batches) {
        require_valid_mesh(batch.mesh);
        require(batch.mesh.indices.size() == 3, "each material batch must retain one triangle");
    }
    require(
        std::any_of(result.warnings.begin(), result.warnings.end(),
            [](const std::string& warning) {
                return warning.find("unusable supplied normals") != std::string::npos;
            }),
        "zero-length Blender normals must be replaced and reported");
    require_near(
        result.batches[0].mesh.vertices[0].texture_coordinates[0],
        0.0F,
        "authored texture coordinates must be retained");

    const MaterialModelLoadResult filtered{
        crystalbound::load_obj_material_batches(
            fixture(root, "material-batches.obj"), {{"BlueTriangle"}})};
    require(filtered.batches.size() == 1, "excluded object must not create a render batch");
    require(filtered.batches[0].material_name == "Red", "wrong render object was excluded");
    require(filtered.objects.size() == 2, "excluded render objects must retain collision metadata");
}

void supplied_elemental_crystal_isolated_from_scenery(
    const std::filesystem::path& root)
{
    const MaterialModelLoadResult result{
        crystalbound::load_obj_material_batches(
            fixture(root, "ElementalCrystal.obj"),
            {{"Plane.001", "SpaceBackground", "Cylinder.001"}})};
    require(result.batches.size() == 1U,
        "elemental collectibles must retain exactly one crystal material batch");
    const auto crystal{std::find_if(result.objects.begin(), result.objects.end(),
        [](const crystalbound::MaterialModelObject& object) {
            return object.name == "Cylinder.002";
        })};
    require(crystal != result.objects.end(),
        "elemental collectible source object is missing");
    require_near(crystal->maximum_bounds[1] - crystal->minimum_bounds[1],
        6.67933F, "elemental collectible source height changed", 1.0e-4F);
    require_valid_mesh(result.batches.front().mesh);
}

void complete_normals_are_normalized(const std::filesystem::path& root)
{
    const ModelLoadResult result =
        crystalbound::load_obj(fixture(root, "unnormalized-normals.obj"));
    for (const Vertex& vertex : result.mesh.vertices) {
        require_near(vertex.normal[0], 0.0F, "normal x must be preserved");
        require_near(vertex.normal[1], 0.0F, "normal y must be preserved");
        require_near(vertex.normal[2], 1.0F, "normal z must be normalized");
    }
}

void split_indices_are_preserved(const std::filesystem::path& root)
{
    const ModelLoadResult result =
        crystalbound::load_obj(fixture(root, "split-indices.obj"));
    require(result.mesh.vertices.size() == 4, "split-index quad must deduplicate four tuples");
    require(result.mesh.indices.size() == 6, "split-index quad must contain two triangles");
    require_valid_mesh(result.mesh);
}

void hard_edges_duplicate_positions(const std::filesystem::path& root)
{
    const ModelLoadResult result = crystalbound::load_obj(fixture(root, "hard-edges.obj"));
    require(result.mesh.vertices.size() == 6, "hard edges must duplicate shared positions");
    require(result.mesh.indices.size() == 6, "hard-edge fixture must retain two triangles");

    bool found_position_with_different_normals{};
    for (std::size_t left{}; left < result.mesh.vertices.size(); ++left) {
        for (std::size_t right{left + 1}; right < result.mesh.vertices.size(); ++right) {
            const Vertex& first = result.mesh.vertices[left];
            const Vertex& second = result.mesh.vertices[right];
            if (first.position == second.position && first.normal != second.normal) {
                found_position_with_different_normals = true;
            }
        }
    }
    require(found_position_with_different_normals, "hard edge must retain distinct normals");
    require_valid_mesh(result.mesh);
}

void missing_normals_are_generated(const std::filesystem::path& root)
{
    const ModelLoadResult result = crystalbound::load_obj(fixture(root, "no-normals.obj"));
    require(result.mesh.vertices.size() == 4, "tetrahedron must share four generated vertices");
    require(result.mesh.indices.size() == 12, "tetrahedron must retain four triangles");
    require_valid_mesh(result.mesh);
    require_centered_and_normalized(result.mesh);
}

void multiple_shapes_are_combined(const std::filesystem::path& root)
{
    const ModelLoadResult result =
        crystalbound::load_obj(fixture(root, "multiple-shapes.obj"));
    require(result.mesh.vertices.size() == 6, "two shapes must contribute six vertices");
    require(result.mesh.indices.size() == 6, "two shapes must contribute two triangles");
    require_valid_mesh(result.mesh);
    require_centered_and_normalized(result.mesh);
}

void partial_normals_are_rejected(const std::filesystem::path& root)
{
    require_throws<ModelLoadError>(
        [&] { static_cast<void>(crystalbound::load_obj(fixture(root, "mixed-normals.obj"))); },
        "partial normal data",
        "mixed normal coverage must fail");
}

void malformed_indices_are_rejected(const std::filesystem::path& root)
{
    require_throws<ModelLoadError>(
        [&] {
            static_cast<void>(crystalbound::load_obj(fixture(root, "malformed-indices.obj")));
        },
        "out-of-range position index",
        "out-of-range OBJ index must fail");
}

void degenerate_face_is_skipped(const std::filesystem::path& root)
{
    const ModelLoadResult result =
        crystalbound::load_obj(fixture(root, "degenerate-face.obj"));
    require(result.mesh.indices.size() == 3, "only the valid triangle must remain");
    require(
        std::any_of(result.warnings.begin(), result.warnings.end(), [](const std::string& warning) {
            return warning.find("Skipped a degenerate triangle") != std::string::npos;
        }),
        "degenerate triangle skip must produce a warning");
    require_valid_mesh(result.mesh);
}

void degenerate_only_is_rejected(const std::filesystem::path& root)
{
    require_throws<ModelLoadError>(
        [&] {
            static_cast<void>(crystalbound::load_obj(fixture(root, "degenerate-only.obj")));
        },
        "no non-degenerate triangles",
        "all-degenerate OBJ must fail");
}

void zero_extent_is_rejected(const std::filesystem::path& root)
{
    require_throws<ModelLoadError>(
        [&] { static_cast<void>(crystalbound::load_obj(fixture(root, "zero-extent.obj"))); },
        "zero or non-finite spatial extent",
        "zero-extent OBJ must fail");
}

void mesh_validation_rejects_invalid_data(const std::filesystem::path&)
{
    require_throws<std::invalid_argument>(
        [] { crystalbound::validate_mesh_data({}); },
        "no vertices",
        "empty mesh must fail");

    const Vertex valid_vertex{{0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F}};
    require_throws<std::invalid_argument>(
        [&] { crystalbound::validate_mesh_data({{valid_vertex}, {0, 0}}); },
        "complete triangles",
        "incomplete triangle must fail");

    Vertex non_finite = valid_vertex;
    non_finite.position[0] = std::numeric_limits<float>::infinity();
    require_throws<std::invalid_argument>(
        [&] { crystalbound::validate_mesh_data({{non_finite}, {0, 0, 0}}); },
        "non-finite",
        "non-finite position must fail");

    Vertex non_unit = valid_vertex;
    non_unit.normal = {0.0F, 0.0F, 2.0F};
    require_throws<std::invalid_argument>(
        [&] { crystalbound::validate_mesh_data({{non_unit}, {0, 0, 0}}); },
        "not unit length",
        "non-unit normal must fail");

    require_throws<std::invalid_argument>(
        [&] { crystalbound::validate_mesh_data({{valid_vertex}, {0, 0, 1}}); },
        "out-of-range index",
        "out-of-range mesh index must fail");
}

void camera_rejects_invalid_projection(const std::filesystem::path&)
{
    crystalbound::Camera camera;
    require_throws<std::invalid_argument>(
        [&] { static_cast<void>(camera.projection_matrix(0.0F)); },
        "finite and positive",
        "zero aspect ratio must fail");
    require_throws<std::invalid_argument>(
        [&] {
            static_cast<void>(
                camera.projection_matrix(std::numeric_limits<float>::quiet_NaN()));
        },
        "finite and positive",
        "non-finite aspect ratio must fail");
}

void camera_movement_is_delta_time_based(const std::filesystem::path&)
{
    crystalbound::Camera one_step;
    crystalbound::Camera two_steps;
    const crystalbound::CameraMovementInput input{1.0F, 0.0F, 0.0F, false};
    one_step.move(input, 1.0F);
    two_steps.move(input, 0.5F);
    two_steps.move(input, 0.5F);

    const glm::mat4 first = one_step.view_matrix();
    const glm::mat4 second = two_steps.view_matrix();
    for (int column{}; column < 4; ++column) {
        for (int row{}; row < 4; ++row) {
            require_near(
                first[column][row],
                second[column][row],
                "equal elapsed movement must produce equal view matrices");
        }
    }
}

void air_authored_render_excludes_dark_doorway_caps(const std::filesystem::path&)
{
    const std::vector<std::string>& excluded{
        crystalbound::air_render_excluded_object_names()};
    const auto excludes = [&excluded](const std::string_view name) {
        return std::find(excluded.begin(), excluded.end(), name) != excluded.end();
    };
    require(excludes("DOOR_A_DarkPassage"),
        "Air entrance A dark passage cap must not be rendered");
    require(excludes("DOOR_B_DarkPassage"),
        "Air entrance B dark passage cap must not be rendered");
}

void authored_endpoint_templates_match_supplied_doorways(const std::filesystem::path&)
{
    const crystalbound::ChamberTemplate& start{
        crystalbound::chamber_template(crystalbound::ChamberTemplateRole::start)};
    const crystalbound::ChamberTemplate& aether{
        crystalbound::chamber_template(crystalbound::ChamberTemplateRole::aether)};
    const crystalbound::ChamberTemplate& exit{
        crystalbound::chamber_template(crystalbound::ChamberTemplateRole::exit)};

    require(start.outer_width_millimetres == 16'300
            && start.outer_depth_millimetres == 20'150,
        "Start template must preserve the supplied 1:1 footprint");
    require(start.sockets[6].vestibule_outer_millimetres
            == crystalbound::TemplatePoint2{0, -10'500},
        "Start route must terminate at the authored tunnel collar");
    require(aether.outer_width_millimetres == 50'000
            && aether.outer_depth_millimetres == 45'400,
        "Aether template must preserve the supplied 1:1 footprint");
    require(aether.sockets[0].vestibule_outer_millimetres
                == crystalbound::TemplatePoint2{25'000, 0}
            && aether.sockets[4].vestibule_outer_millimetres
                == crystalbound::TemplatePoint2{-25'000, 0},
        "Aether routes must terminate at both opposed authored entrances");
    require(exit.outer_width_millimetres == 29'100
            && exit.outer_depth_millimetres == 46'100,
        "Exit template must preserve the supplied 1:1 footprint");
    require(exit.sockets[2].vestibule_outer_millimetres
            == crystalbound::TemplatePoint2{0, 24'550},
        "Exit route must terminate at the authored entrance corridor");
}

void authored_fixed_layout_aligns_every_route_at_one_to_one_scale(
    const std::filesystem::path&)
{
    const crystalbound::CaveGenerationResult generation{
        crystalbound::generate_cave({42U})};
    const std::array<std::pair<crystalbound::AuthoredChamberPlacement,
        std::vector<crystalbound::TemplatePoint2>>, 4U> contracts{{
        {crystalbound::start_chamber_placement(generation.scene), {{0, -10'500}}},
        {crystalbound::fire_chamber_placement(generation.scene),
            {{31'500, 0}, {0, 31'500}}},
        {crystalbound::aether_chamber_placement(generation.scene),
            {{25'000, 0}, {-25'000, 0}}},
        {crystalbound::exit_chamber_placement(generation.scene), {{0, 24'550}}},
    }};
    for (const auto& [placement, local_endpoints] : contracts) {
        require(placement.scale.x == 1.0 && placement.scale.y == 1.0
                && placement.scale.z == 1.0,
            "Authored chambers must retain exact 1:1 scale");
        std::size_t matched{};
        for (const crystalbound::TemplatePoint2 local : local_endpoints) {
            const double local_x{local.x_millimetres / 1'000.0};
            const double local_z{local.z_millimetres / 1'000.0};
            const double cosine{std::cos(placement.yaw_radians)};
            const double sine{std::sin(placement.yaw_radians)};
            const double expected_x{placement.translation_metres.x
                + local_x * cosine + local_z * sine};
            const double expected_z{placement.translation_metres.z
                - local_x * sine + local_z * cosine};
            const auto portal{std::find_if(generation.scene.portals.begin(),
                generation.scene.portals.end(), [&](const crystalbound::PortalContract& candidate) {
                    return candidate.chamber_id == placement.chamber_id
                        && std::abs(candidate.center_millimetres.x_millimetres / 1'000.0
                                - expected_x) <= 0.001
                        && std::abs(candidate.center_millimetres.z_millimetres / 1'000.0
                                - expected_z) <= 0.001;
                })};
            require(portal != generation.scene.portals.end(),
                "Generated tunnel endpoint must equal the transformed authored doorway");
            require(portal->center_millimetres.y_millimetres == 1'350,
                "Every tunnel centerline must share the level zero-metre floor datum");
            ++matched;
        }
        require(matched == local_endpoints.size(),
            "Every authored doorway must receive exactly one route endpoint");
    }
    require(std::none_of(generation.scene.routes.begin(), generation.scene.routes.end(),
                [](const crystalbound::RouteGeometryContract& route) {
                    return route.bridge;
                }),
        "The fixed authored layout must use tunnels only");
}

void authored_collision_supports_both_sides_of_every_tunnel_seam(
    const std::filesystem::path& root)
{
    const crystalbound::CaveGenerationResult generation{
        crystalbound::generate_cave({42U})};
    crystalbound::CollisionWorld world{
        crystalbound::build_collision_world(generation.scene)};
    const auto attach = [&](const crystalbound::MaterialModelLoadResult& model,
                            const crystalbound::AuthoredChamberPlacement& placement,
                            const auto& builder) {
        crystalbound::append_authored_chamber_collision(
            world, builder(model, placement));
    };
    const auto load = [&root](const std::string_view filename) {
        return crystalbound::load_obj_material_batches(
            root / std::filesystem::path{filename});
    };
    attach(load("FireChamber.obj"),
        crystalbound::fire_chamber_placement(generation.scene),
        crystalbound::build_fire_chamber_collision);
    attach(crystalbound::load_water_chamber_render_asset(root / "WaterChamber.obj"),
        crystalbound::water_chamber_placement(generation.scene),
        crystalbound::build_water_chamber_collision);
    attach(load("EarthChamber.obj"),
        crystalbound::earth_chamber_placement(generation.scene),
        crystalbound::build_earth_chamber_collision);
    attach(load("AirChamber.obj"),
        crystalbound::air_chamber_placement(generation.scene),
        crystalbound::build_air_chamber_collision);
    attach(load("AetherChamber.obj"),
        crystalbound::aether_chamber_placement(generation.scene),
        crystalbound::build_aether_chamber_collision);
    attach(load("StartChamber.obj"),
        crystalbound::start_chamber_placement(generation.scene),
        crystalbound::build_start_chamber_collision);
    attach(load("ExitChamber.obj"),
        crystalbound::exit_chamber_placement(generation.scene),
        crystalbound::build_exit_chamber_collision);

    crystalbound::GroundedController controller{
        world, crystalbound::find_start_spawn(generation)};
    require(controller.state().grounded,
        "The authored Start collision must preserve a grounded player spawn");

    const crystalbound::PlayerCapsule capsule{crystalbound::locked_player_capsule()};
    constexpr double sample_offset_metres{0.45};
    constexpr double maximum_step_metres{0.30};
    for (const crystalbound::PortalContract& portal : generation.scene.portals) {
        const double center_x{portal.center_millimetres.x_millimetres / 1'000.0};
        const double center_z{portal.center_millimetres.z_millimetres / 1'000.0};
        const double inward_x{
            portal.inward_direction_millimetres.x_millimetres / 1'000.0};
        const double inward_z{
            portal.inward_direction_millimetres.z_millimetres / 1'000.0};
        for (const double direction : {-1.0, 1.0}) {
            const crystalbound::GeometryVector3 feet{
                center_x + inward_x * sample_offset_metres * direction,
                0.0,
                center_z + inward_z * sample_offset_metres * direction};
            const crystalbound::CollisionProbe probe{crystalbound::probe_collision_world(
                world, capsule, feet, maximum_step_metres)};
            require(probe.supported,
                "Both the tunnel and chamber side of every authored seam must be supported");
            require(!crystalbound::intersects_fall_region(world, feet),
                "No authored doorway seam may overlap a fall region");
        }
    }
}

[[nodiscard]] crystalbound::GeometryVector3 transform_authored_point(
    const crystalbound::AuthoredChamberPlacement& placement,
    const crystalbound::GeometryVector3& local) noexcept
{
    const double cosine{std::cos(placement.yaw_radians)};
    const double sine{std::sin(placement.yaw_radians)};
    return {
        placement.translation_metres.x
            + local.x * placement.scale.x * cosine
            + local.z * placement.scale.z * sine,
        placement.translation_metres.y + local.y * placement.scale.y,
        placement.translation_metres.z
            - local.x * placement.scale.x * sine
            + local.z * placement.scale.z * cosine,
    };
}

void authored_terminal_tunnels_stop_at_the_doorway(const std::filesystem::path&)
{
    const crystalbound::CaveGenerationResult generation{
        crystalbound::generate_cave({42U})};
    for (const crystalbound::CompiledChamberTemplate& chamber
        : generation.scene.compiled_chambers) {
        if (chamber.role != crystalbound::ChamberTemplateRole::aether
            && chamber.role != crystalbound::ChamberTemplateRole::exit) {
            continue;
        }
        for (const crystalbound::PortalContract& portal : generation.scene.portals) {
            if (portal.chamber_id == chamber.chamber_id) {
                require(portal.approach_depth_millimetres
                        == crystalbound::authored_terminal_junction_depth_millimetres,
                    "Aether and Exit tunnel junctions must stop at their authored doorways");
            }
        }
    }
}

void authored_exit_display_matches_named_obj_markers(const std::filesystem::path&)
{
    const crystalbound::CaveGenerationResult generation{
        crystalbound::generate_cave({42U})};
    const crystalbound::ExitArchData arch{crystalbound::build_exit_arch(generation)};
    const crystalbound::AuthoredChamberPlacement placement{
        crystalbound::exit_chamber_placement(generation.scene)};
    constexpr std::array<crystalbound::GeometryVector3, 5> socket_markers{{
        {3.328698, 9.081559, -18.790001},
        {-1.854717, 10.968168, -18.790001},
        {-3.328698, 9.081559, -18.790001},
        {1.854717, 10.968168, -18.790001},
        {0.0, 11.5, -18.790001},
    }};
    for (std::size_t index{}; index < arch.sockets.size(); ++index) {
        const crystalbound::GeometryVector3 expected{
            transform_authored_point(placement, socket_markers[index])};
        const crystalbound::GeometryVector3 actual{arch.sockets[index].position_metres};
        require(std::abs(actual.x - expected.x) <= 0.001
                && std::abs(actual.y - expected.y) <= 0.001
                && std::abs(actual.z - expected.z) <= 0.001,
            "Exit socket crystal must occupy its named authored arch marker");
    }

    const crystalbound::GeometryVector3 portal_minimum_local{-2.95, 5.18, -19.68};
    const crystalbound::GeometryVector3 portal_maximum_local{2.95, 10.95, -19.68};
    const crystalbound::GeometryVector3 expected_first{
        transform_authored_point(placement, portal_minimum_local)};
    const crystalbound::GeometryVector3 expected_second{
        transform_authored_point(placement, portal_maximum_local)};
    crystalbound::GeometryVector3 actual_minimum{
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity()};
    crystalbound::GeometryVector3 actual_maximum{
        -actual_minimum.x, -actual_minimum.y, -actual_minimum.z};
    for (const crystalbound::Vertex& vertex : arch.portal_mesh.vertices) {
        actual_minimum.x = std::min(actual_minimum.x,
            static_cast<double>(vertex.position[0]));
        actual_minimum.y = std::min(actual_minimum.y,
            static_cast<double>(vertex.position[1]));
        actual_minimum.z = std::min(actual_minimum.z,
            static_cast<double>(vertex.position[2]));
        actual_maximum.x = std::max(actual_maximum.x,
            static_cast<double>(vertex.position[0]));
        actual_maximum.y = std::max(actual_maximum.y,
            static_cast<double>(vertex.position[1]));
        actual_maximum.z = std::max(actual_maximum.z,
            static_cast<double>(vertex.position[2]));
    }
    const crystalbound::GeometryVector3 expected_minimum{
        std::min(expected_first.x, expected_second.x),
        std::min(expected_first.y, expected_second.y),
        std::min(expected_first.z, expected_second.z)};
    const crystalbound::GeometryVector3 expected_maximum{
        std::max(expected_first.x, expected_second.x),
        std::max(expected_first.y, expected_second.y),
        std::max(expected_first.z, expected_second.z)};
    require(std::abs(actual_minimum.x - expected_minimum.x) <= 0.001
            && std::abs(actual_minimum.y - expected_minimum.y) <= 0.001
            && std::abs(actual_minimum.z - expected_minimum.z) <= 0.001
            && std::abs(actual_maximum.x - expected_maximum.x) <= 0.001
            && std::abs(actual_maximum.y - expected_maximum.y) <= 0.001
            && std::abs(actual_maximum.z - expected_maximum.z) <= 0.001,
        "Active exit gate must exactly occupy the authored arch opening");
}

void authored_exit_interaction_completes_from_portal_landing(
    const std::filesystem::path&)
{
    const crystalbound::CaveGenerationResult generation{
        crystalbound::generate_cave({42U})};
    const crystalbound::ExitArchData arch{crystalbound::build_exit_arch(generation)};
    const crystalbound::AuthoredChamberPlacement placement{
        crystalbound::exit_chamber_placement(generation.scene)};
    const crystalbound::GeometryVector3 camera{
        transform_authored_point(placement, {0.0, 6.62, -17.60})};
    const crystalbound::GeometryVector3 direction{
        arch.interaction_position_metres.x - camera.x,
        arch.interaction_position_metres.y - camera.y,
        arch.interaction_position_metres.z - camera.z};
    const double length{std::sqrt(direction.x * direction.x
        + direction.y * direction.y + direction.z * direction.z)};
    const crystalbound::CameraInteractionQuery query{
        camera, {direction.x / length, direction.y / length, direction.z / length}};
    crystalbound::CrystalCollectionState collection;
    for (const crystalbound::Element element : crystalbound::elemental_order) {
        require(collection.collect(element), "Test setup must collect every crystal");
    }
    const crystalbound::ExitAttemptResult attempt{crystalbound::attempt_exit_arch(
        arch, query, crystalbound::build_crystal_visibility_world(generation.scene),
        true, true, collection)};
    require(attempt.completed,
        "Pressing E from the authored portal landing must complete the game");
}

}  // namespace

int main(const int argument_count, char* arguments[])
{
    bool exclude_golden{};
    std::string_view filter{};
    for (int index{2}; index < argument_count; ++index) {
        const std::string_view argument{arguments[index]};
        if (argument == "--exclude-golden") {
            exclude_golden = true;
        } else if (argument == "--filter" && index + 1 < argument_count) {
            filter = arguments[++index];
        } else {
            std::cerr << "Usage: crystalbound_tests <testdata-directory> "
                         "[--exclude-golden] [--filter <substring>]\n";
            return 2;
        }
    }
    if (argument_count < 2) {
        std::cerr << "Usage: crystalbound_tests <testdata-directory> "
                     "[--exclude-golden] [--filter <substring>]\n";
        return 2;
    }

    const std::filesystem::path fixture_root{arguments[1]};
    if (!std::filesystem::is_directory(fixture_root)) {
        std::cerr << "Test fixture directory does not exist: " << fixture_root << '\n';
        return 2;
    }

    std::vector<TestCase> tests{
        {"valid OBJ loads", valid_obj_loads},
        {"material OBJ batches preserve authored scale", material_batches_preserve_authored_scale},
        {"supplied elemental crystal is isolated from scenery",
            supplied_elemental_crystal_isolated_from_scenery},
        {"complete supplied normals are normalized", complete_normals_are_normalized},
        {"split OBJ indices are preserved", split_indices_are_preserved},
        {"hard edges duplicate position/normal tuples", hard_edges_duplicate_positions},
        {"missing normals are generated", missing_normals_are_generated},
        {"multiple OBJ shapes are combined", multiple_shapes_are_combined},
        {"partial normals are rejected", partial_normals_are_rejected},
        {"malformed indices are rejected", malformed_indices_are_rejected},
        {"one degenerate face is skipped", degenerate_face_is_skipped},
        {"all-degenerate OBJ is rejected", degenerate_only_is_rejected},
        {"zero-extent OBJ is rejected", zero_extent_is_rejected},
        {"mesh validation rejects invalid data", mesh_validation_rejects_invalid_data},
        {"camera rejects invalid projection", camera_rejects_invalid_projection},
        {"camera movement is delta-time based", camera_movement_is_delta_time_based},
        {"Air authored render excludes dark doorway caps",
            air_authored_render_excludes_dark_doorway_caps},
        {"authored endpoint templates match supplied doorways",
            authored_endpoint_templates_match_supplied_doorways},
        {"authored fixed layout aligns every route at one-to-one scale",
            authored_fixed_layout_aligns_every_route_at_one_to_one_scale},
        {"authored collision supports both sides of every tunnel seam",
            authored_collision_supports_both_sides_of_every_tunnel_seam},
        {"authored terminal tunnels stop at the doorway",
            authored_terminal_tunnels_stop_at_the_doorway},
        {"authored exit display matches named OBJ markers",
            authored_exit_display_matches_named_obj_markers},
        {"authored exit interaction completes from portal landing",
            authored_exit_interaction_completes_from_portal_landing},
    };
    const std::vector<TestCase> generation_tests{
        crystalbound::test::generation_test_cases()};
    tests.insert(tests.end(), generation_tests.begin(), generation_tests.end());
    const std::vector<TestCase> geometry_tests{
        crystalbound::test::geometry_test_cases()};
    tests.insert(tests.end(), geometry_tests.begin(), geometry_tests.end());
    const std::vector<TestCase> cave_scene_tests{
        crystalbound::test::cave_scene_test_cases()};
    tests.insert(tests.end(), cave_scene_tests.begin(), cave_scene_tests.end());
    const std::vector<TestCase> player_controller_tests{
        crystalbound::test::player_controller_test_cases()};
    tests.insert(
        tests.end(), player_controller_tests.begin(), player_controller_tests.end());
    const std::vector<TestCase> reachability_tests{
        crystalbound::test::reachability_test_cases()};
    tests.insert(tests.end(), reachability_tests.begin(), reachability_tests.end());
    const std::vector<TestCase> rendering_tests{
        crystalbound::test::rendering_test_cases()};
    tests.insert(tests.end(), rendering_tests.begin(), rendering_tests.end());
    const std::vector<TestCase> elemental_visuals_tests{
        crystalbound::test::elemental_visuals_test_cases()};
    tests.insert(
        tests.end(), elemental_visuals_tests.begin(), elemental_visuals_tests.end());
    const std::vector<TestCase> crystal_collection_tests{
        crystalbound::test::crystal_collection_test_cases()};
    tests.insert(
        tests.end(), crystal_collection_tests.begin(), crystal_collection_tests.end());
    const std::vector<TestCase> game_loop_tests{
        crystalbound::test::game_loop_test_cases()};
    tests.insert(tests.end(), game_loop_tests.begin(), game_loop_tests.end());
    const std::vector<TestCase> seed_corpus_tests{
        crystalbound::test::seed_corpus_test_cases()};
    tests.insert(tests.end(), seed_corpus_tests.begin(), seed_corpus_tests.end());
    const std::vector<TestCase> frame_profile_tests{
        crystalbound::test::frame_profile_test_cases()};
    tests.insert(tests.end(), frame_profile_tests.begin(), frame_profile_tests.end());

    if (exclude_golden) {
        for (const std::string_view golden_name : deferred_golden_tests) {
            const auto count{std::count_if(
                tests.begin(), tests.end(),
                [golden_name](const TestCase& test) {
                    return test.name == golden_name;
                })};
            if (count != 1) {
                std::cerr << "Deferred golden allowlist entry must match exactly one test: "
                          << golden_name << " (matches=" << count << ")\n";
                return 2;
            }
        }
    }

    std::size_t failures{};
    std::size_t skipped_golden{};
    std::size_t filtered_out{};
    for (const TestCase& test : tests) {
        if (!filter.empty()
            && std::string_view{test.name}.find(filter) == std::string_view::npos) {
            ++filtered_out;
            continue;
        }
        if (exclude_golden && is_deferred_golden_test(test.name)) {
            ++skipped_golden;
            std::cout << "[SKIP-GOLDEN] " << test.name << std::endl;
            continue;
        }
        try {
            test.function(fixture_root);
            std::cout << "[PASS] " << test.name << std::endl;
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "[FAIL] " << test.name << ": " << error.what() << '\n';
        } catch (...) {
            ++failures;
            std::cerr << "[FAIL] " << test.name << ": unknown exception\n";
        }
    }

    if (failures != 0) {
        std::cerr << failures << " of " << tests.size() << " tests failed.\n";
        return 1;
    }
    const std::size_t executed{tests.size() - skipped_golden - filtered_out};
    if (executed == 0U) {
        std::cerr << "No tests matched filter: " << filter << '\n';
        return 2;
    }
    std::cout << "All " << executed
              << " executed Crystalbound CPU tests passed";
    if (exclude_golden) {
        std::cout << "; skipped " << skipped_golden
                  << " exact deferred golden tests";
    }
    std::cout << ".\n";
    return 0;
}
