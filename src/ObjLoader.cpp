#include "crystalbound/ObjLoader.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <limits>
#include <optional>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <utility>

#include <tiny_obj_loader.h>

namespace crystalbound {
namespace {

struct Vector3d {
    double x{};
    double y{};
    double z{};
};

struct Triangle {
    std::array<tinyobj::index_t, 3> corners{};
    Vector3d face_normal{};
};

struct VertexKey {
    int position{};
    int normal{};

    [[nodiscard]] bool operator==(const VertexKey& other) const noexcept
    {
        return position == other.position && normal == other.normal;
    }
};

struct VertexKeyHash {
    [[nodiscard]] std::size_t operator()(const VertexKey& key) const noexcept
    {
        const std::size_t position = std::hash<int>{}(key.position);
        const std::size_t normal = std::hash<int>{}(key.normal);
        return position ^ (normal + 0x9e3779b9U + (position << 6U) + (position >> 2U));
    }
};

struct MaterialVertexKey {
    int position{};
    int normal{};
    int texture_coordinate{};

    [[nodiscard]] bool operator==(const MaterialVertexKey& other) const noexcept
    {
        return position == other.position && normal == other.normal
            && texture_coordinate == other.texture_coordinate;
    }
};

struct MaterialVertexKeyHash {
    [[nodiscard]] std::size_t operator()(const MaterialVertexKey& key) const noexcept
    {
        std::size_t value = std::hash<int>{}(key.position);
        const auto combine = [&value](const int component) {
            const std::size_t hashed = std::hash<int>{}(component);
            value ^= hashed + 0x9e3779b9U + (value << 6U) + (value >> 2U);
        };
        combine(key.normal);
        combine(key.texture_coordinate);
        return value;
    }
};

struct MaterialBatchBuilder {
    MaterialMeshBatch batch{};
    std::unordered_map<MaterialVertexKey, std::uint32_t, MaterialVertexKeyHash>
        vertex_lookup{};
};

constexpr double supplied_normal_squared_epsilon{1.0e-20};
constexpr double relative_geometry_squared_epsilon{1.0e-24};

[[nodiscard]] Vector3d subtract(const Vector3d& left, const Vector3d& right)
{
    return {left.x - right.x, left.y - right.y, left.z - right.z};
}

[[nodiscard]] Vector3d cross(const Vector3d& left, const Vector3d& right)
{
    return {
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x,
    };
}

[[nodiscard]] double squared_length(const Vector3d& value)
{
    return value.x * value.x + value.y * value.y + value.z * value.z;
}

[[nodiscard]] bool finite(const Vector3d& value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] Vector3d normalized(
    const Vector3d& value,
    const std::string_view description,
    const double minimum_squared_length)
{
    const double length_squared = squared_length(value);
    if (!finite(value) || !std::isfinite(length_squared)
        || length_squared <= minimum_squared_length) {
        throw ModelLoadError(std::string{description} + " is zero-length or non-finite.");
    }
    const double inverse_length = 1.0 / std::sqrt(length_squared);
    return {value.x * inverse_length, value.y * inverse_length, value.z * inverse_length};
}

[[nodiscard]] std::string trim(std::string value)
{
    const auto first = value.find_first_not_of(" \t\r");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r");
    return value.substr(first, last - first + 1);
}

[[nodiscard]] int parse_index(
    const std::string_view token,
    const std::size_t line_number,
    const std::string_view description)
{
    if (token.empty()) {
        throw ModelLoadError("OBJ line " + std::to_string(line_number)
            + " has a missing " + std::string{description} + ".");
    }
    int value{};
    const char* begin = token.data();
    const char* end = token.data() + token.size();
    const auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{} || result.ptr != end || value == 0) {
        throw ModelLoadError("OBJ line " + std::to_string(line_number)
            + " has an invalid " + std::string{description} + ": " + std::string{token});
    }
    return value;
}

[[nodiscard]] bool valid_float(const std::string& token)
{
    if (token.empty()) {
        return false;
    }
    errno = 0;
    char* end{};
    const double value = std::strtod(token.c_str(), &end);
    return errno != ERANGE && end == token.c_str() + token.size() && std::isfinite(value);
}

void validate_relative_index(
    const int index,
    const std::size_t available_count,
    const std::size_t line_number,
    const std::string_view description)
{
    if (index >= 0) {
        return;
    }
    const auto magnitude = static_cast<std::uint64_t>(-static_cast<std::int64_t>(index));
    if (magnitude > available_count) {
        throw ModelLoadError("OBJ line " + std::to_string(line_number)
            + " has an out-of-range relative " + std::string{description} + ".");
    }
}

void validate_face_reference(
    const std::string& token,
    const std::size_t line_number,
    const std::size_t position_count,
    const std::size_t texcoord_count,
    const std::size_t normal_count)
{
    const std::string_view reference{token};
    const std::size_t first_slash = reference.find('/');
    const std::string_view position_field = reference.substr(0, first_slash);
    const int position_index = parse_index(position_field, line_number, "position index");
    validate_relative_index(position_index, position_count, line_number, "position index");
    if (first_slash == std::string_view::npos) {
        return;
    }

    const std::size_t second_slash = reference.find('/', first_slash + 1);
    if (second_slash == std::string_view::npos) {
        const std::string_view texcoord_field = reference.substr(first_slash + 1);
        const int texcoord_index = parse_index(
            texcoord_field, line_number, "texture-coordinate index");
        validate_relative_index(
            texcoord_index, texcoord_count, line_number, "texture-coordinate index");
        return;
    }
    if (reference.find('/', second_slash + 1) != std::string_view::npos) {
        throw ModelLoadError("OBJ line " + std::to_string(line_number)
            + " has too many fields in face reference: " + token);
    }

    const std::string_view texcoord_field =
        reference.substr(first_slash + 1, second_slash - first_slash - 1);
    if (!texcoord_field.empty()) {
        const int texcoord_index = parse_index(
            texcoord_field, line_number, "texture-coordinate index");
        validate_relative_index(
            texcoord_index, texcoord_count, line_number, "texture-coordinate index");
    }
    const std::string_view normal_field = reference.substr(second_slash + 1);
    const int normal_index = parse_index(normal_field, line_number, "normal index");
    validate_relative_index(normal_index, normal_count, line_number, "normal index");
}

void prevalidate_obj(const std::string& source)
{
    std::istringstream lines{source};
    std::string line;
    std::size_t line_number{};
    std::size_t position_count{};
    std::size_t texcoord_count{};
    std::size_t normal_count{};
    while (std::getline(lines, line)) {
        ++line_number;
        std::istringstream tokens{line};
        std::string directive;
        if (!(tokens >> directive) || directive.front() == '#') {
            continue;
        }
        if (directive == "v" || directive == "vn") {
            for (int component{}; component < 3; ++component) {
                std::string token;
                if (!(tokens >> token) || !valid_float(token)) {
                    throw ModelLoadError("OBJ line " + std::to_string(line_number)
                        + " contains an invalid " + directive + " component.");
                }
            }
            if (directive == "v") {
                ++position_count;
            } else {
                ++normal_count;
            }
        } else if (directive == "vt") {
            ++texcoord_count;
        } else if (directive == "f") {
            std::size_t corner_count{};
            std::string token;
            while (tokens >> token) {
                if (token.front() == '#') {
                    break;
                }
                validate_face_reference(
                    token, line_number, position_count, texcoord_count, normal_count);
                ++corner_count;
            }
            if (corner_count < 3) {
                throw ModelLoadError("OBJ line " + std::to_string(line_number)
                    + " has fewer than three face corners.");
            }
        }
    }
}

[[nodiscard]] Vector3d position_at(const tinyobj::attrib_t& attributes, const int index)
{
    if (index < 0 || static_cast<std::size_t>(index) >= attributes.vertices.size() / 3) {
        throw ModelLoadError("OBJ contains an out-of-range position index.");
    }
    const std::size_t offset = static_cast<std::size_t>(index) * 3;
    const Vector3d value{
        attributes.vertices[offset],
        attributes.vertices[offset + 1],
        attributes.vertices[offset + 2],
    };
    if (!finite(value)) {
        throw ModelLoadError("OBJ contains a non-finite position.");
    }
    return value;
}

[[nodiscard]] Vector3d normal_at(const tinyobj::attrib_t& attributes, const int index)
{
    if (index < 0 || static_cast<std::size_t>(index) >= attributes.normals.size() / 3) {
        throw ModelLoadError("OBJ contains an out-of-range normal index.");
    }
    const std::size_t offset = static_cast<std::size_t>(index) * 3;
    return normalized(
        {attributes.normals[offset], attributes.normals[offset + 1], attributes.normals[offset + 2]},
        "OBJ normal",
        supplied_normal_squared_epsilon);
}

[[nodiscard]] bool normal_is_usable(
    const tinyobj::attrib_t& attributes,
    const int index)
{
    if (index < 0 || static_cast<std::size_t>(index) >= attributes.normals.size() / 3) {
        throw ModelLoadError("OBJ contains an out-of-range normal index.");
    }
    const std::size_t offset = static_cast<std::size_t>(index) * 3;
    const Vector3d value{
        attributes.normals[offset],
        attributes.normals[offset + 1],
        attributes.normals[offset + 2],
    };
    return finite(value) && squared_length(value) > supplied_normal_squared_epsilon;
}

[[nodiscard]] std::array<float, 2> texture_coordinate_at(
    const tinyobj::attrib_t& attributes,
    const int index)
{
    if (index < 0) {
        return {};
    }
    if (static_cast<std::size_t>(index) >= attributes.texcoords.size() / 2) {
        throw ModelLoadError("OBJ contains an out-of-range texture-coordinate index.");
    }
    const std::size_t offset = static_cast<std::size_t>(index) * 2;
    const float u = attributes.texcoords[offset];
    const float v = attributes.texcoords[offset + 1];
    if (!std::isfinite(u) || !std::isfinite(v)) {
        throw ModelLoadError("OBJ contains a non-finite texture coordinate.");
    }
    return {u, v};
}

void append_diagnostics(const std::string& diagnostics, std::vector<std::string>& warnings)
{
    std::istringstream lines{diagnostics};
    std::string line;
    while (std::getline(lines, line)) {
        line = trim(std::move(line));
        if (line.empty()) {
            continue;
        }
        constexpr std::string_view warning_prefix{"WARN:"};
        if (line.compare(0, warning_prefix.size(), warning_prefix) == 0) {
            line = trim(line.substr(warning_prefix.size()));
        }
        warnings.push_back(std::move(line));
    }
}

[[nodiscard]] std::string path_text(const std::filesystem::path& path)
{
    return path.u8string();
}

}  // namespace

ModelLoadResult load_obj(const std::filesystem::path& path)
{
    std::ifstream input{path, std::ios::binary};
    if (!input) {
        throw ModelLoadError("Unable to open OBJ file: " + path_text(path));
    }
    const std::string source{
        std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    if (input.bad()) {
        throw ModelLoadError("Unable to read OBJ file: " + path_text(path));
    }
    prevalidate_obj(source);

    tinyobj::attrib_t attributes;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string diagnostics;
    std::istringstream obj_stream{source};
    const bool loaded = tinyobj::LoadObj(
        &attributes, &shapes, &materials, &diagnostics, &obj_stream, nullptr, true);
    if (!loaded) {
        const std::string detail = trim(diagnostics);
        throw ModelLoadError("Unable to parse OBJ file " + path_text(path)
            + (detail.empty() ? std::string{"."} : ": " + detail));
    }
    if (attributes.vertices.size() % 3 != 0 || attributes.normals.size() % 3 != 0) {
        throw ModelLoadError("OBJ contains incomplete position or normal data.");
    }

    ModelLoadResult result;
    append_diagnostics(diagnostics, result.warnings);

    std::vector<std::array<tinyobj::index_t, 3>> candidates;
    for (const tinyobj::shape_t& shape : shapes) {
        std::size_t index_offset{};
        for (const unsigned char corner_count : shape.mesh.num_face_vertices) {
            if (corner_count != 3 || index_offset + 3 > shape.mesh.indices.size()) {
                throw ModelLoadError("OBJ triangulation produced an invalid face.");
            }
            candidates.push_back({
                shape.mesh.indices[index_offset],
                shape.mesh.indices[index_offset + 1],
                shape.mesh.indices[index_offset + 2],
            });
            index_offset += 3;
        }
        if (index_offset != shape.mesh.indices.size()) {
            throw ModelLoadError("OBJ shape contains unmatched face indices.");
        }
    }
    if (candidates.empty()) {
        throw ModelLoadError("OBJ contains no faces.");
    }

    Vector3d candidate_minimum{
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),
    };
    Vector3d candidate_maximum{
        -candidate_minimum.x, -candidate_minimum.y, -candidate_minimum.z};
    for (const auto& triangle : candidates) {
        for (const tinyobj::index_t& corner : triangle) {
            const Vector3d position = position_at(attributes, corner.vertex_index);
            if (corner.normal_index >= 0) {
                static_cast<void>(normal_at(attributes, corner.normal_index));
            }
            candidate_minimum.x = std::min(candidate_minimum.x, position.x);
            candidate_minimum.y = std::min(candidate_minimum.y, position.y);
            candidate_minimum.z = std::min(candidate_minimum.z, position.z);
            candidate_maximum.x = std::max(candidate_maximum.x, position.x);
            candidate_maximum.y = std::max(candidate_maximum.y, position.y);
            candidate_maximum.z = std::max(candidate_maximum.z, position.z);
        }
    }
    const double candidate_extent = std::max({
        candidate_maximum.x - candidate_minimum.x,
        candidate_maximum.y - candidate_minimum.y,
        candidate_maximum.z - candidate_minimum.z});
    if (!std::isfinite(candidate_extent) || candidate_extent <= 0.0) {
        throw ModelLoadError("OBJ has zero or non-finite spatial extent.");
    }

    std::vector<Triangle> triangles;
    for (const auto& candidate : candidates) {
        const Vector3d first = position_at(attributes, candidate[0].vertex_index);
        const Vector3d second = position_at(attributes, candidate[1].vertex_index);
        const Vector3d third = position_at(attributes, candidate[2].vertex_index);
        const Vector3d first_edge = subtract(second, first);
        const Vector3d second_edge = subtract(third, first);
        const Vector3d third_edge = subtract(third, second);
        const Vector3d face_normal = cross(first_edge, second_edge);
        const double maximum_edge_squared = std::max({
            squared_length(first_edge),
            squared_length(second_edge),
            squared_length(third_edge)});
        const double face_normal_squared = squared_length(face_normal);
        if (!finite(first_edge) || !finite(second_edge) || !finite(third_edge)
            || !finite(face_normal) || !std::isfinite(maximum_edge_squared)
            || !std::isfinite(face_normal_squared)) {
            throw ModelLoadError("OBJ triangle arithmetic produced a non-finite value.");
        }

        // Cross products have length units squared. Comparing their squared
        // magnitude against the longest local edge to the fourth power makes
        // degeneracy classification independent of source units and outliers.
        const double degenerate_threshold = maximum_edge_squared * maximum_edge_squared
            * relative_geometry_squared_epsilon;
        if (face_normal_squared <= degenerate_threshold) {
            result.warnings.push_back("Skipped a degenerate triangle.");
            continue;
        }
        triangles.push_back({candidate, face_normal});
    }
    if (triangles.empty()) {
        throw ModelLoadError("OBJ contains no non-degenerate triangles.");
    }

    Vector3d minimum{
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),
    };
    Vector3d maximum{-minimum.x, -minimum.y, -minimum.z};
    for (const Triangle& triangle : triangles) {
        for (const tinyobj::index_t& corner : triangle.corners) {
            const Vector3d position = position_at(attributes, corner.vertex_index);
            minimum.x = std::min(minimum.x, position.x);
            minimum.y = std::min(minimum.y, position.y);
            minimum.z = std::min(minimum.z, position.z);
            maximum.x = std::max(maximum.x, position.x);
            maximum.y = std::max(maximum.y, position.y);
            maximum.z = std::max(maximum.z, position.z);
        }
    }
    const double maximum_extent = std::max({
        maximum.x - minimum.x, maximum.y - minimum.y, maximum.z - minimum.z});
    if (!std::isfinite(maximum_extent) || maximum_extent <= 0.0) {
        throw ModelLoadError("Retained OBJ triangles have zero or non-finite spatial extent.");
    }

    bool has_normals{};
    bool missing_normals{};
    for (const Triangle& triangle : triangles) {
        for (const tinyobj::index_t& corner : triangle.corners) {
            has_normals = has_normals || corner.normal_index >= 0;
            missing_normals = missing_normals || corner.normal_index < 0;
        }
    }
    if (has_normals && missing_normals) {
        throw ModelLoadError(
            "OBJ mixes corners with normals and corners without normals; partial normal data is unsupported.");
    }

    std::vector<Vector3d> generated_normals(attributes.vertices.size() / 3);
    if (!has_normals) {
        const double inverse_extent_squared = 1.0 / (maximum_extent * maximum_extent);
        for (const Triangle& triangle : triangles) {
            for (const tinyobj::index_t& corner : triangle.corners) {
                Vector3d& sum = generated_normals[static_cast<std::size_t>(corner.vertex_index)];
                sum.x += triangle.face_normal.x * inverse_extent_squared;
                sum.y += triangle.face_normal.y * inverse_extent_squared;
                sum.z += triangle.face_normal.z * inverse_extent_squared;
            }
        }
    }

    const Vector3d center{
        (minimum.x + maximum.x) * 0.5,
        (minimum.y + maximum.y) * 0.5,
        (minimum.z + maximum.z) * 0.5,
    };
    const double scale = 2.0 / maximum_extent;
    std::unordered_map<VertexKey, std::uint32_t, VertexKeyHash> vertex_lookup;
    for (const Triangle& triangle : triangles) {
        for (const tinyobj::index_t& corner : triangle.corners) {
            const VertexKey key{corner.vertex_index, has_normals ? corner.normal_index : -1};
            const auto existing = vertex_lookup.find(key);
            if (existing != vertex_lookup.end()) {
                result.mesh.indices.push_back(existing->second);
                continue;
            }
            if (result.mesh.vertices.size() >= std::numeric_limits<std::uint32_t>::max()) {
                throw ModelLoadError("OBJ expands beyond the 32-bit vertex-index limit.");
            }
            const Vector3d source_position = position_at(attributes, corner.vertex_index);
            const Vector3d source_normal = has_normals
                ? normal_at(attributes, corner.normal_index)
                : normalized(
                    generated_normals[static_cast<std::size_t>(corner.vertex_index)],
                    "Generated vertex normal",
                    relative_geometry_squared_epsilon);
            const Vector3d normalized_position{
                (source_position.x - center.x) * scale,
                (source_position.y - center.y) * scale,
                (source_position.z - center.z) * scale,
            };
            Vertex vertex{
                {
                    static_cast<float>(normalized_position.x),
                    static_cast<float>(normalized_position.y),
                    static_cast<float>(normalized_position.z),
                },
                {
                    static_cast<float>(source_normal.x),
                    static_cast<float>(source_normal.y),
                    static_cast<float>(source_normal.z),
                },
            };
            const auto new_index = static_cast<std::uint32_t>(result.mesh.vertices.size());
            result.mesh.vertices.push_back(vertex);
            vertex_lookup.emplace(key, new_index);
            result.mesh.indices.push_back(new_index);
        }
    }

    try {
        validate_mesh_data(result.mesh);
    } catch (const std::invalid_argument& error) {
        throw ModelLoadError(std::string{"OBJ produced invalid mesh data: "} + error.what());
    }
    return result;
}

MaterialModelLoadResult load_obj_material_batches(
    const std::filesystem::path& path,
    const MaterialModelLoadOptions& options)
{
    std::ifstream input{path, std::ios::binary};
    if (!input) {
        throw ModelLoadError("Unable to open OBJ file: " + path_text(path));
    }
    const std::string source{
        std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    if (input.bad()) {
        throw ModelLoadError("Unable to read OBJ file: " + path_text(path));
    }
    prevalidate_obj(source);

    tinyobj::attrib_t attributes;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string diagnostics;
    std::istringstream obj_stream{source};
    std::string material_directory{path.parent_path().u8string()};
    if (!material_directory.empty()
        && material_directory.back() != std::filesystem::path::preferred_separator) {
        material_directory.push_back(std::filesystem::path::preferred_separator);
    }
    tinyobj::MaterialFileReader material_reader{material_directory};
    const bool loaded = tinyobj::LoadObj(
        &attributes, &shapes, &materials, &diagnostics, &obj_stream,
        &material_reader, true);
    if (!loaded) {
        const std::string detail = trim(diagnostics);
        throw ModelLoadError("Unable to parse material-batched OBJ file " + path_text(path)
            + (detail.empty() ? std::string{"."} : ": " + detail));
    }
    if (attributes.vertices.size() % 3 != 0
        || attributes.normals.size() % 3 != 0
        || attributes.texcoords.size() % 2 != 0) {
        throw ModelLoadError(
            "Material-batched OBJ contains incomplete vertex attribute data.");
    }

    MaterialModelLoadResult result;
    append_diagnostics(diagnostics, result.warnings);
    std::vector<MaterialBatchBuilder> builders;
    std::unordered_map<int, std::size_t> builder_indices;
    Vector3d minimum{
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),
    };
    Vector3d maximum{-minimum.x, -minimum.y, -minimum.z};
    std::size_t retained_triangle_count{};
    std::size_t replaced_normal_count{};

    const auto batch_for_material = [&](const int material_id) -> MaterialBatchBuilder& {
        const auto existing = builder_indices.find(material_id);
        if (existing != builder_indices.end()) {
            return builders[existing->second];
        }
        if (material_id < -1
            || (material_id >= 0
                && static_cast<std::size_t>(material_id) >= materials.size())) {
            throw ModelLoadError("OBJ face refers to an out-of-range material.");
        }
        MaterialMeshBatch batch;
        if (material_id < 0) {
            batch.material_name = "__default";
            batch.diffuse = {0.7F, 0.7F, 0.7F};
        } else {
            const tinyobj::material_t& material{
                materials[static_cast<std::size_t>(material_id)]};
            batch.material_name = material.name;
            batch.diffuse = {
                static_cast<float>(material.diffuse[0]),
                static_cast<float>(material.diffuse[1]),
                static_cast<float>(material.diffuse[2]),
            };
            batch.emission = {
                static_cast<float>(material.emission[0]),
                static_cast<float>(material.emission[1]),
                static_cast<float>(material.emission[2]),
            };
        }
        const std::size_t index = builders.size();
        builders.push_back({std::move(batch), {}});
        builder_indices.emplace(material_id, index);
        return builders.back();
    };

    for (const tinyobj::shape_t& shape : shapes) {
        const bool include_render_mesh{
            std::find(options.excluded_object_names.begin(),
                options.excluded_object_names.end(), shape.name)
            == options.excluded_object_names.end()};
        Vector3d object_minimum{
            std::numeric_limits<double>::infinity(),
            std::numeric_limits<double>::infinity(),
            std::numeric_limits<double>::infinity(),
        };
        Vector3d object_maximum{
            -object_minimum.x, -object_minimum.y, -object_minimum.z};
        std::optional<int> object_material_id;
        bool object_has_triangles{};
        bool object_has_mixed_materials{};
        std::size_t index_offset{};
        for (std::size_t face_index{};
             face_index < shape.mesh.num_face_vertices.size();
             ++face_index) {
            const unsigned char corner_count{shape.mesh.num_face_vertices[face_index]};
            if (corner_count != 3 || index_offset + 3 > shape.mesh.indices.size()) {
                throw ModelLoadError("OBJ triangulation produced an invalid face.");
            }
            const std::array<tinyobj::index_t, 3> corners{
                shape.mesh.indices[index_offset],
                shape.mesh.indices[index_offset + 1],
                shape.mesh.indices[index_offset + 2],
            };
            index_offset += 3;

            const Vector3d first = position_at(attributes, corners[0].vertex_index);
            const Vector3d second = position_at(attributes, corners[1].vertex_index);
            const Vector3d third = position_at(attributes, corners[2].vertex_index);
            const Vector3d first_edge = subtract(second, first);
            const Vector3d second_edge = subtract(third, first);
            const Vector3d third_edge = subtract(third, second);
            const Vector3d face_normal = cross(first_edge, second_edge);
            const double maximum_edge_squared = std::max({
                squared_length(first_edge),
                squared_length(second_edge),
                squared_length(third_edge)});
            const double face_normal_squared = squared_length(face_normal);
            if (!finite(face_normal) || !std::isfinite(maximum_edge_squared)
                || !std::isfinite(face_normal_squared)) {
                throw ModelLoadError(
                    "Material-batched OBJ triangle arithmetic produced a non-finite value.");
            }
            const double degenerate_threshold = maximum_edge_squared * maximum_edge_squared
                * relative_geometry_squared_epsilon;
            if (face_normal_squared <= degenerate_threshold) {
                result.warnings.push_back("Skipped a degenerate triangle.");
                continue;
            }
            const Vector3d geometric_normal{normalized(
                face_normal,
                "Material-batched OBJ geometric normal",
                relative_geometry_squared_epsilon)};

            const int material_id = face_index < shape.mesh.material_ids.size()
                ? shape.mesh.material_ids[face_index]
                : -1;
            object_has_mixed_materials = object_has_mixed_materials
                || (object_material_id.has_value()
                    && *object_material_id != material_id);
            if (!object_material_id.has_value()) {
                object_material_id = material_id;
            }
            for (const Vector3d& position : {first, second, third}) {
                object_minimum.x = std::min(object_minimum.x, position.x);
                object_minimum.y = std::min(object_minimum.y, position.y);
                object_minimum.z = std::min(object_minimum.z, position.z);
                object_maximum.x = std::max(object_maximum.x, position.x);
                object_maximum.y = std::max(object_maximum.y, position.y);
                object_maximum.z = std::max(object_maximum.z, position.z);
                minimum.x = std::min(minimum.x, position.x);
                minimum.y = std::min(minimum.y, position.y);
                minimum.z = std::min(minimum.z, position.z);
                maximum.x = std::max(maximum.x, position.x);
                maximum.y = std::max(maximum.y, position.y);
                maximum.z = std::max(maximum.z, position.z);
            }
            object_has_triangles = true;
            if (!include_render_mesh) {
                ++retained_triangle_count;
                continue;
            }
            MaterialBatchBuilder& builder{batch_for_material(material_id)};
            for (const tinyobj::index_t& corner : corners) {
                if (corner.normal_index < 0) {
                    throw ModelLoadError(
                        "Material-batched OBJ requires a normal for every face corner.");
                }
                const bool use_supplied_normal{
                    normal_is_usable(attributes, corner.normal_index)};
                const int normal_key = use_supplied_normal
                    ? corner.normal_index
                    : -2 - static_cast<int>(retained_triangle_count);
                const MaterialVertexKey key{
                    corner.vertex_index, normal_key, corner.texcoord_index};
                const auto existing = builder.vertex_lookup.find(key);
                if (existing != builder.vertex_lookup.end()) {
                    builder.batch.mesh.indices.push_back(existing->second);
                    continue;
                }
                if (builder.batch.mesh.vertices.size()
                    >= std::numeric_limits<std::uint32_t>::max()) {
                    throw ModelLoadError(
                        "Material-batched OBJ exceeds the 32-bit vertex-index limit.");
                }
                const Vector3d position = position_at(attributes, corner.vertex_index);
                const Vector3d normal = use_supplied_normal
                    ? normal_at(attributes, corner.normal_index)
                    : geometric_normal;
                replaced_normal_count += use_supplied_normal ? 0U : 1U;
                const std::array<float, 2> texture_coordinate{
                    texture_coordinate_at(attributes, corner.texcoord_index)};
                const Vertex vertex{
                    {
                        static_cast<float>(position.x),
                        static_cast<float>(position.y),
                        static_cast<float>(position.z),
                    },
                    {
                        static_cast<float>(normal.x),
                        static_cast<float>(normal.y),
                        static_cast<float>(normal.z),
                    },
                    texture_coordinate,
                };
                const auto new_index{
                    static_cast<std::uint32_t>(builder.batch.mesh.vertices.size())};
                builder.batch.mesh.vertices.push_back(vertex);
                builder.vertex_lookup.emplace(key, new_index);
                builder.batch.mesh.indices.push_back(new_index);
            }
            ++retained_triangle_count;
        }
        if (index_offset != shape.mesh.indices.size()) {
            throw ModelLoadError("OBJ shape contains unmatched face indices.");
        }
        if (object_has_triangles) {
            std::string material_name{"__mixed"};
            if (!object_has_mixed_materials && object_material_id.has_value()) {
                if (*object_material_id < 0) {
                    material_name = "__default";
                } else if (static_cast<std::size_t>(*object_material_id)
                    < materials.size()) {
                    material_name =
                        materials[static_cast<std::size_t>(*object_material_id)].name;
                } else {
                    throw ModelLoadError(
                        "OBJ object refers to an out-of-range material.");
                }
            }
            result.objects.push_back({
                shape.name.empty() ? "__unnamed" : shape.name,
                std::move(material_name),
                {
                    static_cast<float>(object_minimum.x),
                    static_cast<float>(object_minimum.y),
                    static_cast<float>(object_minimum.z),
                },
                {
                    static_cast<float>(object_maximum.x),
                    static_cast<float>(object_maximum.y),
                    static_cast<float>(object_maximum.z),
                },
            });
        }
    }
    if (retained_triangle_count == 0 || builders.empty()) {
        throw ModelLoadError(
            "Material-batched OBJ contains no non-degenerate triangles.");
    }
    if (replaced_normal_count != 0) {
        result.warnings.push_back(
            "Replaced " + std::to_string(replaced_normal_count)
            + " unusable supplied normals with geometric face normals.");
    }

    result.minimum_bounds = {
        static_cast<float>(minimum.x),
        static_cast<float>(minimum.y),
        static_cast<float>(minimum.z),
    };
    result.maximum_bounds = {
        static_cast<float>(maximum.x),
        static_cast<float>(maximum.y),
        static_cast<float>(maximum.z),
    };
    result.batches.reserve(builders.size());
    for (MaterialBatchBuilder& builder : builders) {
        try {
            validate_mesh_data(builder.batch.mesh);
        } catch (const std::invalid_argument& error) {
            throw ModelLoadError(
                std::string{"Material-batched OBJ produced invalid mesh data: "}
                + error.what());
        }
        result.batches.push_back(std::move(builder.batch));
    }
    return result;
}

}  // namespace crystalbound
