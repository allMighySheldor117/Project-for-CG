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

#include "crystalbound/Camera.hpp"
#include "crystalbound/MeshData.hpp"
#include "crystalbound/ObjLoader.hpp"
#include "GeometryTests.hpp"
#include "CaveSceneTests.hpp"
#include "GenerationTests.hpp"
#include "PlayerControllerTests.hpp"
#include "ReachabilityTests.hpp"

namespace {

using crystalbound::MeshData;
using crystalbound::ModelLoadError;
using crystalbound::ModelLoadResult;
using crystalbound::Vertex;
using crystalbound::test::TestCase;

constexpr float tolerance{1.0e-5F};

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

}  // namespace

int main(const int argument_count, char* arguments[])
{
    if (argument_count != 2) {
        std::cerr << "Usage: crystalbound_tests <testdata-directory>\n";
        return 2;
    }

    const std::filesystem::path fixture_root{arguments[1]};
    if (!std::filesystem::is_directory(fixture_root)) {
        std::cerr << "Test fixture directory does not exist: " << fixture_root << '\n';
        return 2;
    }

    std::vector<TestCase> tests{
        {"valid OBJ loads", valid_obj_loads},
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

    std::size_t failures{};
    for (const TestCase& test : tests) {
        try {
            test.function(fixture_root);
            std::cout << "[PASS] " << test.name << '\n';
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
    std::cout << "All " << tests.size() << " Crystalbound CPU tests passed.\n";
    return 0;
}
