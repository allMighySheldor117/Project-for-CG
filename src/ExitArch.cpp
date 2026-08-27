#include "crystalbound/ExitArch.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace crystalbound {
namespace {

constexpr double pi{3.14159265358979323846};
constexpr std::uint64_t exit_arch_domain{0x45584954'41524348ULL};
constexpr std::uint64_t exit_socket_domain{0x534F434B'45540000ULL};
constexpr std::uint64_t fnv_offset_basis{14695981039346656037ULL};
constexpr std::uint64_t fnv_prime{1099511628211ULL};

struct ArchBasis {
    GeometryVector3 origin{};
    GeometryVector3 right{};
    GeometryVector3 forward{};
};

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

[[nodiscard]] GeometryVector3 cross(
    const GeometryVector3 left,
    const GeometryVector3 right) noexcept
{
    return {left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x};
}

[[nodiscard]] GeometryVector3 normalized(const GeometryVector3 value)
{
    const double length{std::sqrt(
        value.x * value.x + value.y * value.y + value.z * value.z)};
    if (!std::isfinite(length) || length <= 1.0e-10) {
        throw GeometryError{"Exit arch basis is degenerate."};
    }
    return {value.x / length, value.y / length, value.z / length};
}

[[nodiscard]] GeometryVector3 transform(
    const ArchBasis& basis,
    const GeometryVector3 local) noexcept
{
    return {
        basis.origin.x + basis.right.x * local.x + basis.forward.x * local.z,
        basis.origin.y + local.y,
        basis.origin.z + basis.right.z * local.x + basis.forward.z * local.z,
    };
}

[[nodiscard]] Vertex vertex(
    const GeometryVector3 position,
    const GeometryVector3 normal,
    const float u,
    const float v) noexcept
{
    return {{static_cast<float>(position.x), static_cast<float>(position.y),
                static_cast<float>(position.z)},
        {static_cast<float>(normal.x), static_cast<float>(normal.y),
            static_cast<float>(normal.z)},
        {u, v}};
}

void append_flat_triangle(
    MeshBuilder& builder,
    const GeometryVector3 first,
    const GeometryVector3 second,
    const GeometryVector3 third)
{
    const GeometryVector3 normal{normalized(
        cross(subtract(second, first), subtract(third, first)))};
    const std::uint32_t first_index{
        builder.append_vertex(vertex(first, normal, 0.0F, 0.0F))};
    const std::uint32_t second_index{
        builder.append_vertex(vertex(second, normal, 1.0F, 0.0F))};
    const std::uint32_t third_index{
        builder.append_vertex(vertex(third, normal, 0.5F, 1.0F))};
    builder.append_triangle(first_index, second_index, third_index);
}

void append_quad(
    MeshBuilder& builder,
    const GeometryVector3 first,
    const GeometryVector3 second,
    const GeometryVector3 third,
    const GeometryVector3 fourth)
{
    append_flat_triangle(builder, first, second, third);
    append_flat_triangle(builder, first, third, fourth);
}

void append_box(
    MeshBuilder& builder,
    const ArchBasis& basis,
    const GeometryVector3 center,
    const GeometryVector3 half_extent)
{
    const double x0{center.x - half_extent.x};
    const double x1{center.x + half_extent.x};
    const double y0{center.y - half_extent.y};
    const double y1{center.y + half_extent.y};
    const double z0{center.z - half_extent.z};
    const double z1{center.z + half_extent.z};
    const std::array<std::array<GeometryVector3, 4>, 6> faces{{
        {{{x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}, {x1, y0, z1}}},
        {{{x0, y0, z1}, {x0, y1, z1}, {x0, y1, z0}, {x0, y0, z0}}},
        {{{x0, y1, z0}, {x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0}}},
        {{{x0, y0, z1}, {x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}}},
        {{{x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}, {x0, y0, z1}}},
        {{{x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0}, {x1, y0, z0}}},
    }};
    for (const auto& face : faces) {
        append_quad(builder, transform(basis, face[0]), transform(basis, face[1]),
            transform(basis, face[2]), transform(basis, face[3]));
    }
}

void append_arch_segment(
    MeshBuilder& builder,
    const ArchBasis& basis,
    const double first_angle,
    const double second_angle)
{
    constexpr double spring_height{2.05};
    constexpr double inner_radius{1.08};
    constexpr double outer_radius{1.68};
    constexpr double half_depth{0.30};
    const auto point = [](const double angle, const double radius, const double z) {
        return GeometryVector3{
            std::cos(angle) * radius,
            spring_height + std::sin(angle) * radius,
            z};
    };
    const GeometryVector3 outer_first_front{point(first_angle, outer_radius, half_depth)};
    const GeometryVector3 outer_second_front{point(second_angle, outer_radius, half_depth)};
    const GeometryVector3 inner_second_front{point(second_angle, inner_radius, half_depth)};
    const GeometryVector3 inner_first_front{point(first_angle, inner_radius, half_depth)};
    const GeometryVector3 outer_first_back{point(first_angle, outer_radius, -half_depth)};
    const GeometryVector3 outer_second_back{point(second_angle, outer_radius, -half_depth)};
    const GeometryVector3 inner_second_back{point(second_angle, inner_radius, -half_depth)};
    const GeometryVector3 inner_first_back{point(first_angle, inner_radius, -half_depth)};
    const auto world = [&](const GeometryVector3 value) { return transform(basis, value); };
    append_quad(builder, world(outer_first_front), world(outer_second_front),
        world(inner_second_front), world(inner_first_front));
    append_quad(builder, world(inner_first_back), world(inner_second_back),
        world(outer_second_back), world(outer_first_back));
    append_quad(builder, world(outer_first_back), world(outer_second_back),
        world(outer_second_front), world(outer_first_front));
    append_quad(builder, world(inner_first_front), world(inner_second_front),
        world(inner_second_back), world(inner_first_back));
    append_quad(builder, world(outer_second_back), world(inner_second_back),
        world(inner_second_front), world(outer_second_front));
    append_quad(builder, world(outer_first_front), world(inner_first_front),
        world(inner_first_back), world(outer_first_back));
}

[[nodiscard]] GeometryVector3 socket_local_position(const std::size_t index)
{
    constexpr std::array<double, 5> angles_degrees{15.0, 52.5, 90.0, 127.5, 165.0};
    constexpr double spring_height{2.05};
    constexpr double radius{1.39};
    const double angle{angles_degrees[index] * pi / 180.0};
    return {std::cos(angle) * radius,
        spring_height + std::sin(angle) * radius - 0.22, 0.39};
}

void append_socket_mount(
    MeshBuilder& builder,
    const ArchBasis& basis,
    const GeometryVector3 center)
{
    constexpr std::uint32_t sides{8U};
    constexpr double radius{0.24};
    constexpr double half_depth{0.08};
    for (std::uint32_t side{}; side < sides; ++side) {
        const double first_angle{2.0 * pi * side / sides};
        const double second_angle{2.0 * pi * (side + 1U) / sides};
        const GeometryVector3 front_first{center.x + std::cos(first_angle) * radius,
            center.y + std::sin(first_angle) * radius, center.z + half_depth};
        const GeometryVector3 front_second{center.x + std::cos(second_angle) * radius,
            center.y + std::sin(second_angle) * radius, center.z + half_depth};
        const GeometryVector3 back_first{front_first.x, front_first.y, center.z - half_depth};
        const GeometryVector3 back_second{front_second.x, front_second.y, center.z - half_depth};
        append_flat_triangle(builder, transform(basis, {center.x, center.y, center.z + half_depth}),
            transform(basis, front_first), transform(basis, front_second));
        append_quad(builder, transform(basis, back_first), transform(basis, front_first),
            transform(basis, front_second), transform(basis, back_second));
    }
}

[[nodiscard]] MeshData build_stone_mesh(const ArchBasis& basis)
{
    MeshBuilder builder;
    append_box(builder, basis, {-1.38, 1.025, 0.0}, {0.30, 1.025, 0.30});
    append_box(builder, basis, {1.38, 1.025, 0.0}, {0.30, 1.025, 0.30});
    constexpr std::uint32_t segment_count{9U};
    for (std::uint32_t segment{}; segment < segment_count; ++segment) {
        const double first{pi * segment / segment_count};
        const double second{pi * (segment + 1U) / segment_count};
        append_arch_segment(builder, basis, first, second);
    }
    for (std::size_t index{}; index < elemental_order.size(); ++index) {
        append_socket_mount(builder, basis, socket_local_position(index));
    }
    return builder.finish();
}

[[nodiscard]] MeshData build_portal_mesh(const ArchBasis& basis)
{
    MeshBuilder builder;
    constexpr std::uint32_t side_count{18U};
    const GeometryVector3 center{0.0, 1.38, -0.32};
    for (std::uint32_t side{}; side < side_count; ++side) {
        const double first_angle{2.0 * pi * side / side_count};
        const double second_angle{2.0 * pi * (side + 1U) / side_count};
        const GeometryVector3 first{std::cos(first_angle) * 0.98,
            center.y + std::sin(first_angle) * 1.30, center.z};
        const GeometryVector3 second{std::cos(second_angle) * 0.98,
            center.y + std::sin(second_angle) * 1.30, center.z};
        append_flat_triangle(builder, transform(basis, center),
            transform(basis, first), transform(basis, second));
        append_flat_triangle(builder, transform(basis, center),
            transform(basis, second), transform(basis, first));
    }
    return builder.finish();
}

[[nodiscard]] const ChamberNode& exit_node(const TopologyData& topology)
{
    const auto found{std::find_if(topology.nodes.begin(), topology.nodes.end(),
        [](const ChamberNode& node) { return node.role == ChamberRole::exit; })};
    if (found == topology.nodes.end()) {
        throw GeometryError{"Exit arch requires exactly one Exit chamber."};
    }
    return *found;
}

[[nodiscard]] const ChamberGeometryContract& exit_chamber(
    const CaveSceneData& scene,
    const NodeId id)
{
    const auto found{std::find_if(scene.chambers.begin(), scene.chambers.end(),
        [id](const ChamberGeometryContract& chamber) {
            return chamber.node_id == id;
        })};
    if (found == scene.chambers.end()) {
        throw GeometryError{"Exit arch could not find its chamber geometry."};
    }
    return *found;
}

[[nodiscard]] const PortalContract& exit_portal(
    const CaveSceneData& scene,
    const NodeId id)
{
    const PortalContract* best{};
    for (const PortalContract& portal : scene.portals) {
        if (portal.chamber_id == id
            && (best == nullptr
                || stable_edge_id(portal.route) < stable_edge_id(best->route))) {
            best = &portal;
        }
    }
    if (best == nullptr) {
        throw GeometryError{"Exit arch requires an entrance portal."};
    }
    return *best;
}

[[nodiscard]] ArchBasis build_basis(
    const ChamberGeometryContract& chamber,
    const PortalContract& portal)
{
    const GeometryVector3 inward{normalized({
        static_cast<double>(portal.inward_direction_millimetres.x_millimetres),
        0.0,
        static_cast<double>(portal.inward_direction_millimetres.z_millimetres)})};
    const GeometryVector3 front{-inward.x, 0.0, -inward.z};
    const GeometryVector3 right{front.z, 0.0, -front.x};
    const GeometryVector3 chamber_center{
        chamber.center_millimetres.x_millimetres / 1'000.0,
        chamber.center_millimetres.y_millimetres / 1'000.0,
        chamber.center_millimetres.z_millimetres / 1'000.0};
    return {{chamber_center.x + inward.x * 2.0, chamber_center.y,
                chamber_center.z + inward.z * 2.0},
        right, front};
}

[[nodiscard]] const ElementalChamberVisual& elemental_chamber(
    const ElementalSceneData& visuals,
    const Element element)
{
    const auto found{std::find_if(visuals.chambers.begin(), visuals.chambers.end(),
        [element](const ElementalChamberVisual& chamber) {
            return chamber.element == element;
        })};
    if (found == visuals.chambers.end()) {
        throw GeometryError{"Exit socket is missing its elemental crystal contract."};
    }
    return *found;
}

void hash_byte(std::uint64_t& hash, const std::uint8_t value) noexcept
{
    hash ^= value;
    hash *= fnv_prime;
}

template <typename Integer>
void hash_integer(std::uint64_t& hash, const Integer value) noexcept
{
    using Unsigned = std::make_unsigned_t<Integer>;
    const Unsigned converted{static_cast<Unsigned>(value)};
    for (std::size_t byte{}; byte < sizeof(Integer); ++byte) {
        hash_byte(hash, static_cast<std::uint8_t>(converted >> (byte * 8U)));
    }
}

[[nodiscard]] std::uint64_t arch_fingerprint(
    const CaveGenerationResult& generation,
    const ChamberGeometryContract& chamber,
    const PortalContract& portal) noexcept
{
    std::uint64_t hash{fnv_offset_basis};
    hash_integer(hash, generation.generation.generator_version.value);
    hash_integer(hash, generation.generation.effective_seed.value);
    hash_integer(hash, chamber.node_id.value);
    hash_integer(hash, chamber.center_millimetres.x_millimetres);
    hash_integer(hash, chamber.center_millimetres.y_millimetres);
    hash_integer(hash, chamber.center_millimetres.z_millimetres);
    hash_integer(hash, portal.inward_direction_millimetres.x_millimetres);
    hash_integer(hash, portal.inward_direction_millimetres.z_millimetres);
    for (const Element element : elemental_order) {
        hash_integer(hash, static_cast<std::uint8_t>(element));
    }
    return hash;
}

[[nodiscard]] ExitRejectionReason map_rejection(
    const InteractionRejectionReason reason) noexcept
{
    switch (reason) {
    case InteractionRejectionReason::none:
        return ExitRejectionReason::none;
    case InteractionRejectionReason::invalid_query:
        return ExitRejectionReason::invalid_query;
    case InteractionRejectionReason::out_of_range:
        return ExitRejectionReason::out_of_range;
    case InteractionRejectionReason::outside_focus:
        return ExitRejectionReason::outside_focus;
    case InteractionRejectionReason::occluded:
        return ExitRejectionReason::occluded;
    case InteractionRejectionReason::no_press_edge:
        return ExitRejectionReason::no_press_edge;
    case InteractionRejectionReason::no_target:
    case InteractionRejectionReason::already_collected:
        return ExitRejectionReason::invalid_query;
    }
    return ExitRejectionReason::invalid_query;
}

}  // namespace

ExitArchData build_exit_arch(const CaveGenerationResult& generation)
{
    const ChamberNode& node{exit_node(generation.generation.topology)};
    const ChamberGeometryContract& chamber{exit_chamber(generation.scene, node.id)};
    const PortalContract& portal{exit_portal(generation.scene, node.id)};
    const ArchBasis basis{build_basis(chamber, portal)};
    ExitArchData arch;
    arch.chamber_id = node.id;
    arch.stable_object_id = exit_arch_domain ^ node.id.value;
    arch.interaction_position_metres = transform(basis, {0.0, 1.42, 0.42});
    arch.stone_mesh = build_stone_mesh(basis);
    arch.portal_mesh = build_portal_mesh(basis);
    for (std::size_t index{}; index < elemental_order.size(); ++index) {
        const Element element{elemental_order[index]};
        const ElementalChamberVisual& elemental{
            elemental_chamber(generation.scene.elemental_visuals, element)};
        arch.sockets[index] = {element,
            exit_socket_domain ^ (static_cast<std::uint64_t>(node.id.value) << 16U)
                ^ static_cast<std::uint64_t>(index),
            transform(basis, socket_local_position(index)),
            elemental.socket_crystal_mesh,
            elemental.persona.albedo,
            elemental.persona.emission,
            elemental.crystal.animation};
    }
    arch.fingerprint = arch_fingerprint(generation, chamber, portal);
    const std::vector<std::string> issues{validate_exit_arch(generation, arch)};
    if (!issues.empty()) {
        throw GeometryError{"Generated exit arch is invalid: " + issues.front()};
    }
    return arch;
}

std::vector<std::string> validate_exit_arch(
    const CaveGenerationResult& generation,
    const ExitArchData& arch)
{
    std::vector<std::string> issues;
    const auto expected_exit{std::find_if(
        generation.generation.topology.nodes.begin(),
        generation.generation.topology.nodes.end(),
        [](const ChamberNode& node) { return node.role == ChamberRole::exit; })};
    if (expected_exit == generation.generation.topology.nodes.end()
        || expected_exit->id != arch.chamber_id) {
        issues.emplace_back("arch chamber is not the generated Exit chamber");
    }
    if (arch.stable_object_id == 0U || arch.fingerprint == 0U) {
        issues.emplace_back("arch stable identity is invalid");
    }
    if (!finite(arch.interaction_position_metres)) {
        issues.emplace_back("arch interaction position is non-finite");
    }
    try {
        validate_procedural_mesh(arch.stone_mesh);
        validate_procedural_mesh(arch.portal_mesh);
    } catch (const std::exception& error) {
        issues.push_back(std::string{"arch mesh is invalid: "} + error.what());
    }
    std::uint64_t vertex_count{arch.stone_mesh.vertices.size()
        + arch.portal_mesh.vertices.size()};
    for (std::size_t index{}; index < arch.sockets.size(); ++index) {
        const ExitSocketContract& socket{arch.sockets[index]};
        if (socket.element != elemental_order[index]
            || socket.stable_object_id == 0U || !finite(socket.position_metres)) {
            issues.emplace_back("exit socket ordering or identity is invalid");
        }
        try {
            validate_procedural_mesh(socket.crystal_mesh);
        } catch (const std::exception& error) {
            issues.push_back(std::string{"socket crystal mesh is invalid: "} + error.what());
        }
        vertex_count += socket.crystal_mesh.vertices.size();
    }
    if (vertex_count + generation.scene.static_vertex_count
        > geometry_budgets.maximum_static_vertices) {
        issues.emplace_back("exit arch exceeds the static vertex budget");
    }
    return issues;
}

ExitArchDisplayState exit_arch_display_state(
    const ExitArchData& arch,
    const CrystalCollectionState& collection) noexcept
{
    static_cast<void>(arch);
    return {collection.socket_display_state(), collection.all_collected()};
}

ExitFocusResult focus_exit_arch(
    const ExitArchData& arch,
    const CameraInteractionQuery& query,
    const VisibilityWorld& visibility) noexcept
{
    const CrystalInteractionTarget target{
        Element::fire, arch.stable_object_id, arch.interaction_position_metres};
    const CrystalCollectionState empty_collection;
    const FocusedCrystalResult focused{
        focus_crystal({target}, query, visibility, empty_collection)};
    if (!focused.focused.has_value()) {
        return {{}, map_rejection(focused.rejection)};
    }
    return {FocusedExitArch{focused.focused->distance_metres,
                focused.focused->angle_degrees},
        ExitRejectionReason::none};
}

ExitAttemptResult attempt_exit_arch(
    const ExitArchData& arch,
    const CameraInteractionQuery& query,
    const VisibilityWorld& visibility,
    const bool playing,
    const bool interaction_pressed_edge,
    const CrystalCollectionState& collection) noexcept
{
    if (!playing) {
        return {false, ExitRejectionReason::not_playing};
    }
    if (!collection.all_collected()) {
        return {false, ExitRejectionReason::crystals_missing};
    }
    if (!interaction_pressed_edge) {
        return {false, ExitRejectionReason::no_press_edge};
    }
    const ExitFocusResult focused{focus_exit_arch(arch, query, visibility)};
    if (!focused.focused.has_value()) {
        return {false, focused.rejection};
    }
    return {true, ExitRejectionReason::none};
}

}  // namespace crystalbound
