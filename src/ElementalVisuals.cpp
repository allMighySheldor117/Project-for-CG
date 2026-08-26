#include "crystalbound/ElementalVisuals.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "crystalbound/DeterministicRandom.hpp"

namespace crystalbound {
namespace {

constexpr double pi{3.14159265358979323846};
constexpr std::uint64_t fnv_offset_basis{14695981039346656037ULL};
constexpr std::uint64_t fnv_prime{1099511628211ULL};
constexpr std::uint32_t elemental_contract_version{1U};
constexpr std::uint64_t elemental_object_domain{0x454C454D'00000000ULL};

constexpr std::array<ElementalPersona, 5> personas{{
    {Element::fire, CrystalAnimationKind::flicker,
        {720U, 105U, 35U}, {1'000U, 180U, 35U}, {1'000U, 270U, 70U},
        5U, 680, 1'850, 350U},
    {Element::water, CrystalAnimationKind::wave,
        {70U, 370U, 820U}, {85U, 560U, 1'000U}, {110U, 650U, 1'000U},
        6U, 720, 1'650, 350U},
    {Element::earth, CrystalAnimationKind::steady,
        {255U, 570U, 125U}, {330U, 900U, 210U}, {390U, 920U, 260U},
        8U, 790, 1'500, 350U},
    {Element::air, CrystalAnimationKind::shimmer,
        {650U, 890U, 950U}, {760U, 1'000U, 1'000U}, {710U, 970U, 1'000U},
        7U, 610, 1'950, 350U},
    {Element::aether, CrystalAnimationKind::rhythmic,
        {500U, 135U, 810U}, {820U, 260U, 1'000U}, {790U, 350U, 1'000U},
        9U, 740, 1'780, 350U},
}};

[[nodiscard]] std::size_t element_index(const Element element)
{
    const auto found{std::find(elemental_order.begin(), elemental_order.end(), element)};
    if (found == elemental_order.end()) {
        throw std::invalid_argument("Element is not supported by the visual contract.");
    }
    return static_cast<std::size_t>(std::distance(elemental_order.begin(), found));
}

[[nodiscard]] std::uint64_t object_id(
    const Element element,
    const ElementalPieceKind kind,
    const std::uint32_t ordinal = 0U) noexcept
{
    return elemental_object_domain
        ^ (static_cast<std::uint64_t>(element) << 40U)
        ^ (static_cast<std::uint64_t>(kind) << 32U)
        ^ ordinal;
}

[[nodiscard]] GeometryVector3 subtract(
    const GeometryVector3& left,
    const GeometryVector3& right) noexcept
{
    return {left.x - right.x, left.y - right.y, left.z - right.z};
}

[[nodiscard]] GeometryVector3 cross(
    const GeometryVector3& left,
    const GeometryVector3& right) noexcept
{
    return {left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x};
}

[[nodiscard]] GeometryVector3 normalized(const GeometryVector3& value)
{
    const double length{std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z)};
    if (!std::isfinite(length) || length <= 1.0e-10) {
        throw GeometryError{"Elemental mesh contains a degenerate face."};
    }
    return {value.x / length, value.y / length, value.z / length};
}

[[nodiscard]] Vertex vertex(
    const GeometryVector3& position,
    const GeometryVector3& normal,
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
    const GeometryVector3& first,
    const GeometryVector3& second,
    const GeometryVector3& third)
{
    const GeometryVector3 normal{
        normalized(cross(subtract(second, first), subtract(third, first)))};
    const std::uint32_t first_index{builder.append_vertex(vertex(first, normal, 0.0F, 0.0F))};
    const std::uint32_t second_index{builder.append_vertex(vertex(second, normal, 1.0F, 0.0F))};
    const std::uint32_t third_index{builder.append_vertex(vertex(third, normal, 0.5F, 1.0F))};
    builder.append_triangle(first_index, second_index, third_index);
}

void append_box(
    MeshBuilder& builder,
    const GeometryVector3& center,
    const GeometryVector3& half_extent)
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
        append_flat_triangle(builder, face[0], face[1], face[2]);
        append_flat_triangle(builder, face[0], face[2], face[3]);
    }
}

[[nodiscard]] MeshData build_box_mesh(
    const GeometryVector3& center,
    const GeometryVector3& half_extent)
{
    MeshBuilder builder;
    append_box(builder, center, half_extent);
    return builder.finish();
}

[[nodiscard]] MeshData build_frustum(
    const std::uint32_t sides,
    const double lower_radius,
    const double upper_radius,
    const double height)
{
    MeshBuilder builder;
    const GeometryVector3 lower_center{0.0, 0.0, 0.0};
    const GeometryVector3 upper_center{0.0, height, 0.0};
    for (std::uint32_t side{}; side < sides; ++side) {
        const double first_angle{2.0 * pi * side / sides};
        const double second_angle{2.0 * pi * (side + 1U) / sides};
        const GeometryVector3 lower_first{
            std::cos(first_angle) * lower_radius, 0.0, std::sin(first_angle) * lower_radius};
        const GeometryVector3 lower_second{
            std::cos(second_angle) * lower_radius, 0.0, std::sin(second_angle) * lower_radius};
        const GeometryVector3 upper_first{
            std::cos(first_angle) * upper_radius, height, std::sin(first_angle) * upper_radius};
        const GeometryVector3 upper_second{
            std::cos(second_angle) * upper_radius, height, std::sin(second_angle) * upper_radius};
        append_flat_triangle(builder, lower_first, upper_second, lower_second);
        append_flat_triangle(builder, lower_first, upper_first, upper_second);
        append_flat_triangle(builder, lower_center, lower_second, lower_first);
        append_flat_triangle(builder, upper_center, upper_first, upper_second);
    }
    return builder.finish();
}

[[nodiscard]] MeshData build_crystal(const CrystalShapeContract& shape)
{
    const double radius{shape.radius_millimetres / 1'000.0};
    const double height{shape.height_millimetres / 1'000.0};
    const double shoulder{shape.shoulder_height_millimetres / 1'000.0};
    MeshBuilder builder;
    const GeometryVector3 lower_center{0.0, 0.0, 0.0};
    const GeometryVector3 tip{0.0, height, 0.0};
    for (std::uint32_t side{}; side < shape.side_count; ++side) {
        const double first_angle{2.0 * pi * side / shape.side_count};
        const double second_angle{2.0 * pi * (side + 1U) / shape.side_count};
        const double first_radius{radius
            + shape.radial_offsets_millimetres[side] / 1'000.0};
        const double second_radius{radius
            + shape.radial_offsets_millimetres[(side + 1U) % shape.side_count] / 1'000.0};
        const GeometryVector3 lower_first{std::cos(first_angle) * first_radius * 0.58,
            0.0, std::sin(first_angle) * first_radius * 0.58};
        const GeometryVector3 lower_second{std::cos(second_angle) * second_radius * 0.58,
            0.0, std::sin(second_angle) * second_radius * 0.58};
        const GeometryVector3 shoulder_first{std::cos(first_angle) * first_radius,
            shoulder, std::sin(first_angle) * first_radius};
        const GeometryVector3 shoulder_second{std::cos(second_angle) * second_radius,
            shoulder, std::sin(second_angle) * second_radius};
        append_flat_triangle(builder, lower_first, shoulder_second, lower_second);
        append_flat_triangle(builder, lower_first, shoulder_first, shoulder_second);
        append_flat_triangle(builder, shoulder_first, tip, shoulder_second);
        append_flat_triangle(builder, lower_center, lower_second, lower_first);
    }
    return builder.finish();
}

[[nodiscard]] CrystalShapeContract scaled_shape(
    const CrystalShapeContract& shape,
    const std::uint32_t scale_milli)
{
    CrystalShapeContract scaled{shape.side_count,
        static_cast<std::int32_t>(
            static_cast<std::int64_t>(shape.radius_millimetres) * scale_milli / 1'000),
        static_cast<std::int32_t>(
            static_cast<std::int64_t>(shape.height_millimetres) * scale_milli / 1'000),
        static_cast<std::int32_t>(
            static_cast<std::int64_t>(shape.shoulder_height_millimetres)
                * scale_milli / 1'000),
        {}};
    scaled.radial_offsets_millimetres.reserve(shape.radial_offsets_millimetres.size());
    for (const std::int32_t offset : shape.radial_offsets_millimetres) {
        scaled.radial_offsets_millimetres.push_back(
            static_cast<std::int32_t>(
                static_cast<std::int64_t>(offset) * scale_milli / 1'000));
    }
    return scaled;
}

[[nodiscard]] ElementalAnimationParameters animation_parameters(
    const Element element,
    const std::uint64_t stable_id)
{
    const std::size_t index{element_index(element)};
    return {personas[index].animation, 1'000U,
        std::array<std::uint32_t, 5>{220U, 170U, 35U, 130U, 260U}[index],
        std::array<std::uint32_t, 5>{1'700U, 550U, 180U, 1'250U, 720U}[index],
        static_cast<std::uint32_t>((stable_id % 360U) * 1'000U)};
}

[[nodiscard]] ElementalVisualPiece make_piece(
    const Element element,
    const NodeId chamber_id,
    const ElementalPieceKind kind,
    const ElementalRenderLayer layer,
    const std::uint32_t ordinal,
    MeshData mesh,
    const IntegerPoint3 position,
    const LinearColorMilli albedo,
    const LinearColorMilli emission = {},
    const MaterialKind material = MaterialKind::untextured,
    const std::uint16_t alpha_milli = 1'000U)
{
    const std::uint64_t stable_id{object_id(element, kind, ordinal)};
    return {kind, layer, stable_id, chamber_id, element, material, std::move(mesh),
        position, 1'000U, 0, 0, alpha_milli, albedo, emission,
        animation_parameters(element, stable_id), 0U};
}

[[nodiscard]] MeshData build_arch_mesh()
{
    MeshBuilder builder;
    append_box(builder, {-1.15, 1.25, 0.0}, {0.24, 1.25, 0.28});
    append_box(builder, {1.15, 1.25, 0.0}, {0.24, 1.25, 0.28});
    append_box(builder, {0.0, 2.45, 0.0}, {1.15, 0.24, 0.28});
    return builder.finish();
}

[[nodiscard]] MeshData build_particle_cluster(const std::uint32_t count, SplitMix64& random)
{
    MeshBuilder builder;
    for (std::uint32_t index{}; index < count; ++index) {
        const double x{(static_cast<double>(random.bounded(2'401U)) - 1'200.0) / 1'000.0};
        const double y{0.35 + static_cast<double>(random.bounded(1'801U)) / 1'000.0};
        const double z{(static_cast<double>(random.bounded(2'401U)) - 1'200.0) / 1'000.0};
        const double size{0.025 + static_cast<double>(random.bounded(31U)) / 1'000.0};
        append_flat_triangle(builder,
            {x - size, y, z}, {x + size, y, z}, {x, y + size * 2.0, z});
    }
    return builder.finish();
}

void add_decorations(
    ElementalChamberVisual& chamber,
    const IntegerPoint3 center,
    SplitMix64& random)
{
    const Element element{chamber.element};
    const auto position = [&](const std::int32_t x, const std::int32_t y, const std::int32_t z) {
        return IntegerPoint3{center.x_millimetres + x, center.y_millimetres + y,
            center.z_millimetres + z};
    };
    switch (element) {
    case Element::fire: {
        chamber.decorations.push_back(make_piece(element, chamber.chamber_id,
            ElementalPieceKind::lava_rock, ElementalRenderLayer::opaque, 0U,
            build_frustum(6U, 1.25, 0.65, 0.75), position(-1'650, 0, 500),
            {230U, 70U, 25U}, {}, MaterialKind::rock));
        chamber.decorations.push_back(make_piece(element, chamber.chamber_id,
            ElementalPieceKind::glowing_crack, ElementalRenderLayer::emissive, 1U,
            build_box_mesh({0.0, 0.015, 0.0}, {1.65, 0.015, 0.055}),
            position(0, 5, 0), {900U, 90U, 15U}, {1'000U, 130U, 20U}));
        ElementalVisualPiece sparks{make_piece(element, chamber.chamber_id,
            ElementalPieceKind::particle_cluster, ElementalRenderLayer::additive, 2U,
            build_particle_cluster(18U, random), position(0, 0, 0),
            {1'000U, 250U, 35U}, {1'000U, 180U, 25U},
            MaterialKind::untextured, 720U)};
        sparks.particle_count = 18U;
        chamber.decorations.push_back(std::move(sparks));
        break;
    }
    case Element::water:
        chamber.decorations.push_back(make_piece(element, chamber.chamber_id,
            ElementalPieceKind::water_surface, ElementalRenderLayer::transparent, 0U,
            build_frustum(20U, 2.45, 2.45, 0.035), position(0, 30, 0),
            {55U, 310U, 720U}, {20U, 110U, 230U}, MaterialKind::untextured, 440U));
        chamber.decorations.push_back(make_piece(element, chamber.chamber_id,
            ElementalPieceKind::fog_ribbon, ElementalRenderLayer::transparent, 1U,
            build_box_mesh({0.0, 0.0, 0.0}, {2.2, 0.16, 0.035}),
            position(0, 760, 700), {250U, 690U, 900U}, {60U, 220U, 350U},
            MaterialKind::untextured, 260U));
        break;
    case Element::earth:
        chamber.decorations.push_back(make_piece(element, chamber.chamber_id,
            ElementalPieceKind::earth_pillar, ElementalRenderLayer::opaque, 0U,
            build_frustum(7U, 0.65, 0.48, 2.35), position(-1'850, 0, -650),
            {290U, 390U, 120U}, {}, MaterialKind::rock));
        chamber.decorations.push_back(make_piece(element, chamber.chamber_id,
            ElementalPieceKind::earth_stalagmite, ElementalRenderLayer::opaque, 1U,
            build_frustum(6U, 0.72, 0.08, 1.65), position(1'650, 0, 850),
            {350U, 430U, 140U}, {}, MaterialKind::rock));
        break;
    case Element::air: {
        chamber.decorations.push_back(make_piece(element, chamber.chamber_id,
            ElementalPieceKind::air_wood_spire, ElementalRenderLayer::opaque, 0U,
            build_frustum(5U, 0.42, 0.15, 2.7), position(-1'700, 0, 650),
            {420U, 270U, 120U}, {}, MaterialKind::wood));
        chamber.decorations.push_back(make_piece(element, chamber.chamber_id,
            ElementalPieceKind::fog_ribbon, ElementalRenderLayer::transparent, 1U,
            build_box_mesh({0.0, 0.0, 0.0}, {2.4, 0.10, 0.035}),
            position(0, 1'150, 0), {650U, 900U, 930U}, {160U, 310U, 330U},
            MaterialKind::untextured, 210U));
        ElementalVisualPiece motes{make_piece(element, chamber.chamber_id,
            ElementalPieceKind::particle_cluster, ElementalRenderLayer::additive, 2U,
            build_particle_cluster(14U, random), position(0, 0, 0),
            {720U, 1'000U, 1'000U}, {650U, 1'000U, 1'000U},
            MaterialKind::untextured, 570U)};
        motes.particle_count = 14U;
        chamber.decorations.push_back(std::move(motes));
        break;
    }
    case Element::aether: {
        chamber.decorations.push_back(make_piece(element, chamber.chamber_id,
            ElementalPieceKind::elemental_arch, ElementalRenderLayer::opaque, 0U,
            build_arch_mesh(), position(0, 0, 1'850), {300U, 115U, 470U},
            {90U, 20U, 140U}, MaterialKind::rock));
        ElementalVisualPiece orbit{make_piece(element, chamber.chamber_id,
            ElementalPieceKind::orbiting_rock, ElementalRenderLayer::opaque, 1U,
            build_frustum(5U, 0.28, 0.16, 0.48), position(0, 1'050, 0),
            {340U, 180U, 500U}, {90U, 30U, 160U}, MaterialKind::rock)};
        orbit.orbit_radius_millimetres = 1'450;
        orbit.orbit_height_millimetres = 240;
        chamber.decorations.push_back(std::move(orbit));
        chamber.decorations.push_back(make_piece(element, chamber.chamber_id,
            ElementalPieceKind::fog_ribbon, ElementalRenderLayer::transparent, 2U,
            build_box_mesh({0.0, 0.0, 0.0}, {2.05, 0.14, 0.035}),
            position(0, 950, -500), {500U, 180U, 780U}, {190U, 45U, 320U},
            MaterialKind::untextured, 240U));
        break;
    }
    }
}

template <typename Integer>
void append_little_endian(std::vector<std::uint8_t>& bytes, const Integer value)
{
    static_assert(std::is_integral_v<Integer>);
    using Unsigned = std::make_unsigned_t<Integer>;
    Unsigned bits{static_cast<Unsigned>(value)};
    for (std::size_t index{}; index < sizeof(Integer); ++index) {
        bytes.push_back(static_cast<std::uint8_t>(bits & Unsigned{0xFFU}));
        bits >>= 8U;
    }
}

void append_piece_contract(
    std::vector<std::uint8_t>& bytes,
    const ElementalVisualPiece& piece)
{
    append_little_endian(bytes, static_cast<std::uint8_t>(piece.kind));
    append_little_endian(bytes, static_cast<std::uint8_t>(piece.layer));
    append_little_endian(bytes, piece.stable_object_id);
    append_little_endian(bytes, piece.chamber_id.value);
    append_little_endian(bytes, static_cast<std::uint8_t>(piece.element));
    append_little_endian(bytes, static_cast<std::uint8_t>(piece.material));
    append_little_endian(bytes, piece.base_position_millimetres.x_millimetres);
    append_little_endian(bytes, piece.base_position_millimetres.y_millimetres);
    append_little_endian(bytes, piece.base_position_millimetres.z_millimetres);
    append_little_endian(bytes, piece.base_scale_milli);
    append_little_endian(bytes, piece.orbit_radius_millimetres);
    append_little_endian(bytes, piece.orbit_height_millimetres);
    append_little_endian(bytes, piece.alpha_milli);
    append_little_endian(bytes, piece.particle_count);
    append_little_endian(bytes, static_cast<std::uint32_t>(piece.mesh.vertices.size()));
    append_little_endian(bytes, static_cast<std::uint32_t>(piece.mesh.indices.size()));
}

[[nodiscard]] std::uint64_t fnv1a(const std::vector<std::uint8_t>& bytes) noexcept
{
    std::uint64_t hash{fnv_offset_basis};
    for (const std::uint8_t byte : bytes) {
        hash ^= byte;
        hash *= fnv_prime;
    }
    return hash;
}

[[nodiscard]] GeometryVector3 position_metres(const IntegerPoint3& position) noexcept
{
    return {position.x_millimetres / 1'000.0,
        position.y_millimetres / 1'000.0,
        position.z_millimetres / 1'000.0};
}

[[nodiscard]] double distance_squared(
    const GeometryVector3& first,
    const GeometryVector3& second) noexcept
{
    const GeometryVector3 delta{subtract(first, second)};
    return delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
}

}  // namespace

std::string_view element_name(const Element element) noexcept
{
    switch (element) {
    case Element::fire: return "Fire";
    case Element::water: return "Water";
    case Element::earth: return "Earth";
    case Element::air: return "Air";
    case Element::aether: return "Aether";
    }
    return "Unknown";
}

const ElementalPersona& elemental_persona(const Element element)
{
    return personas[element_index(element)];
}

std::array<float, 3> linear_color(const LinearColorMilli& color) noexcept
{
    return {color.red / 1'000.0F, color.green / 1'000.0F, color.blue / 1'000.0F};
}

std::optional<Element> element_for_chamber(
    const TopologyData& topology,
    const NodeId chamber_id) noexcept
{
    const auto found{std::find_if(topology.nodes.begin(), topology.nodes.end(),
        [chamber_id](const ChamberNode& node) { return node.id == chamber_id; })};
    if (found == topology.nodes.end() || found->role != ChamberRole::elemental) {
        return std::nullopt;
    }
    return found->element;
}

ElementalSceneData build_elemental_scene(
    const TopologyData& topology,
    const Seed effective_seed)
{
    ElementalSceneData visuals;
    visuals.chambers.reserve(elemental_order.size());
    for (const Element element : elemental_order) {
        const auto node{std::find_if(topology.nodes.begin(), topology.nodes.end(),
            [element](const ChamberNode& candidate) {
                return candidate.role == ChamberRole::elemental
                    && candidate.element == element;
            })};
        if (node == topology.nodes.end()) {
            throw GeometryError{"Topology is missing the " + std::string{element_name(element)}
                + " chamber required by elemental visuals."};
        }
        const ElementalPersona& persona{elemental_persona(element)};
        const std::uint64_t chamber_stable_id{
            object_id(element, ElementalPieceKind::pedestal, 0xFFFFU)};
        SplitMix64 random{make_substream(
            effective_seed.value, random_domain::decoration, chamber_stable_id)};
        CrystalShapeContract shape{persona.crystal_side_count,
            persona.crystal_radius_millimetres, persona.crystal_height_millimetres,
            static_cast<std::int32_t>(persona.crystal_height_millimetres * 58 / 100), {}};
        shape.radial_offsets_millimetres.reserve(shape.side_count);
        for (std::uint32_t side{}; side < shape.side_count; ++side) {
            shape.radial_offsets_millimetres.push_back(
                static_cast<std::int32_t>(random.bounded(101U)) - 50);
        }
        const IntegerPoint3 center{node->anchor.x_millimetres,
            node->anchor.elevation_millimetres, node->anchor.z_millimetres};
        ElementalChamberVisual chamber;
        chamber.chamber_id = node->id;
        chamber.element = element;
        chamber.stable_object_id = chamber_stable_id;
        chamber.persona = persona;
        chamber.crystal_shape = shape;
        chamber.pedestal = make_piece(element, node->id, ElementalPieceKind::pedestal,
            ElementalRenderLayer::opaque, 0U, build_frustum(10U, 1.15, 0.86, 0.58),
            center, {280U, 285U, 300U}, {}, MaterialKind::rock);
        chamber.crystal = make_piece(element, node->id, ElementalPieceKind::crystal,
            ElementalRenderLayer::emissive, 0U, build_crystal(shape),
            {center.x_millimetres, center.y_millimetres + 520,
                center.z_millimetres}, persona.albedo, persona.emission);
        chamber.socket_crystal_mesh =
            build_crystal(scaled_shape(shape, persona.socket_scale_milli));
        add_decorations(chamber, center, random);
        chamber.fingerprint = elemental_scene_fingerprint(
            effective_seed, ElementalSceneData{{chamber}});
        visuals.chambers.push_back(std::move(chamber));
    }

    for (const ElementalChamberVisual& chamber : visuals.chambers) {
        const auto count_piece = [&](const ElementalVisualPiece& piece) {
            visuals.generated_vertex_count +=
                static_cast<std::uint32_t>(piece.mesh.vertices.size());
            if (piece.layer == ElementalRenderLayer::opaque
                || piece.layer == ElementalRenderLayer::emissive) {
                ++visuals.opaque_draw_call_count;
            } else {
                ++visuals.transparent_effect_draw_count;
            }
            visuals.particle_count += piece.particle_count;
        };
        count_piece(chamber.pedestal);
        count_piece(chamber.crystal);
        for (const ElementalVisualPiece& decoration : chamber.decorations) {
            count_piece(decoration);
        }
    }
    visuals.fingerprint = elemental_scene_fingerprint(effective_seed, visuals);
    const std::vector<std::string> errors{validate_elemental_scene(topology, visuals)};
    if (!errors.empty()) {
        throw GeometryError{errors.front()};
    }
    return visuals;
}

std::vector<std::string> validate_elemental_scene(
    const TopologyData& topology,
    const ElementalSceneData& visuals)
{
    std::vector<std::string> errors;
    if (visuals.chambers.size() != elemental_order.size()) {
        errors.push_back("Elemental visuals require exactly five chamber identities.");
    }
    std::set<Element> seen_elements;
    std::set<std::uint64_t> stable_ids;
    std::uint64_t vertex_count{};
    std::uint32_t opaque_count{};
    std::uint32_t transparent_count{};
    std::uint32_t particle_count{};
    for (const ElementalChamberVisual& chamber : visuals.chambers) {
        if (!seen_elements.insert(chamber.element).second
            || element_for_chamber(topology, chamber.chamber_id) != chamber.element) {
            errors.push_back("Elemental visual chamber mapping is missing or duplicated.");
        }
        if (chamber.persona.element != chamber.element
            || chamber.crystal_shape.side_count != chamber.persona.crystal_side_count
            || chamber.crystal_shape.radial_offsets_millimetres.size()
                != chamber.crystal_shape.side_count) {
            errors.push_back("Elemental crystal shape does not match its persona.");
        }
        try {
            validate_procedural_mesh(chamber.socket_crystal_mesh);
        } catch (const GeometryError& error) {
            errors.push_back(std::string{"Socket crystal mesh is invalid: "} + error.what());
        }
        std::vector<const ElementalVisualPiece*> pieces{
            &chamber.pedestal, &chamber.crystal};
        for (const ElementalVisualPiece& piece : chamber.decorations) {
            pieces.push_back(&piece);
        }
        for (const ElementalVisualPiece* piece : pieces) {
            if (!stable_ids.insert(piece->stable_object_id).second
                || piece->chamber_id != chamber.chamber_id
                || piece->element != chamber.element) {
                errors.push_back("Elemental visual stable IDs or ownership are invalid.");
            }
            try {
                validate_procedural_mesh(piece->mesh);
            } catch (const GeometryError& error) {
                errors.push_back(std::string{"Elemental mesh is invalid: "} + error.what());
            }
            vertex_count += piece->mesh.vertices.size();
            if (piece->layer == ElementalRenderLayer::opaque
                || piece->layer == ElementalRenderLayer::emissive) {
                ++opaque_count;
            } else {
                ++transparent_count;
            }
            particle_count += piece->particle_count;
        }
    }
    if (vertex_count != visuals.generated_vertex_count
        || vertex_count > geometry_budgets.maximum_static_vertices) {
        errors.push_back("Elemental vertex count violates its declared budget.");
    }
    if (opaque_count != visuals.opaque_draw_call_count
        || opaque_count > geometry_budgets.maximum_opaque_draw_calls) {
        errors.push_back("Elemental opaque draw count violates its declared budget.");
    }
    if (transparent_count != visuals.transparent_effect_draw_count
        || transparent_count > geometry_budgets.maximum_transparent_draw_calls) {
        errors.push_back("Elemental transparent draw count violates its declared budget.");
    }
    if (particle_count != visuals.particle_count
        || particle_count > geometry_budgets.maximum_particles) {
        errors.push_back("Elemental particle count violates its declared budget.");
    }
    if (visuals.fingerprint == 0U) {
        errors.push_back("Elemental scene fingerprint must be nonzero.");
    }
    return errors;
}

std::uint64_t elemental_scene_fingerprint(
    const Seed effective_seed,
    const ElementalSceneData& visuals)
{
    std::vector<std::uint8_t> bytes;
    append_little_endian(bytes, elemental_contract_version);
    append_little_endian(bytes, current_generator_version.value);
    append_little_endian(bytes, random_domain::decoration);
    append_little_endian(bytes, effective_seed.value);
    append_little_endian(bytes, static_cast<std::uint32_t>(visuals.chambers.size()));
    for (const ElementalChamberVisual& chamber : visuals.chambers) {
        append_little_endian(bytes, chamber.chamber_id.value);
        append_little_endian(bytes, static_cast<std::uint8_t>(chamber.element));
        append_little_endian(bytes, chamber.stable_object_id);
        append_little_endian(bytes, chamber.crystal_shape.side_count);
        append_little_endian(bytes, chamber.crystal_shape.radius_millimetres);
        append_little_endian(bytes, chamber.crystal_shape.height_millimetres);
        append_little_endian(bytes, chamber.crystal_shape.shoulder_height_millimetres);
        for (const std::int32_t offset : chamber.crystal_shape.radial_offsets_millimetres) {
            append_little_endian(bytes, offset);
        }
        append_piece_contract(bytes, chamber.pedestal);
        append_piece_contract(bytes, chamber.crystal);
        append_little_endian(bytes, static_cast<std::uint32_t>(chamber.decorations.size()));
        for (const ElementalVisualPiece& decoration : chamber.decorations) {
            append_piece_contract(bytes, decoration);
        }
    }
    return fnv1a(bytes);
}

ElementalAnimationSample sample_elemental_animation(
    const ElementalAnimationParameters& parameters,
    const float elapsed_seconds)
{
    const float time{
        std::isfinite(elapsed_seconds) ? std::max(elapsed_seconds, 0.0F) : 0.0F};
    const float phase{parameters.phase_millidegrees / 1'000.0F
        * static_cast<float>(pi / 180.0)};
    const float angle{time * parameters.frequency_millihertz / 1'000.0F
        * static_cast<float>(2.0 * pi) + phase};
    float wave{};
    switch (parameters.kind) {
    case CrystalAnimationKind::flicker:
        wave = 0.55F * std::sin(angle) + 0.30F * std::sin(angle * 2.37F + 0.7F)
            + 0.15F * std::sin(angle * 4.91F + 1.4F);
        break;
    case CrystalAnimationKind::wave:
        wave = std::sin(angle);
        break;
    case CrystalAnimationKind::steady:
        wave = 0.08F * std::sin(angle);
        break;
    case CrystalAnimationKind::shimmer:
        wave = 2.0F * std::abs(std::sin(angle)) - 1.0F;
        break;
    case CrystalAnimationKind::rhythmic:
        wave = std::sin(angle) * std::sin(angle);
        wave = wave * 2.0F - 1.0F;
        break;
    }
    const float base{parameters.base_emission_milli / 1'000.0F};
    const float amplitude{parameters.amplitude_milli / 1'000.0F};
    return {std::clamp(base + amplitude * wave, 0.35F, 2.5F),
        std::clamp(1.0F + wave * 0.025F, 0.94F, 1.08F),
        std::sin(angle * 0.5F) * 0.055F,
        angle * 0.23F};
}

ElementalTransformSample sample_elemental_transform(
    const ElementalVisualPiece& piece,
    const float elapsed_seconds)
{
    const ElementalAnimationSample animation{
        sample_elemental_animation(piece.animation, elapsed_seconds)};
    GeometryVector3 position{position_metres(piece.base_position_millimetres)};
    const bool floats{piece.kind == ElementalPieceKind::crystal
        || piece.kind == ElementalPieceKind::water_surface
        || piece.kind == ElementalPieceKind::fog_ribbon
        || piece.kind == ElementalPieceKind::particle_cluster
        || piece.kind == ElementalPieceKind::orbiting_rock};
    if (floats) {
        position.y += animation.vertical_offset_metres;
    }
    if (piece.orbit_radius_millimetres != 0) {
        const double radius{piece.orbit_radius_millimetres / 1'000.0};
        position.x += std::cos(animation.orbit_angle_radians) * radius;
        position.y += piece.orbit_height_millimetres / 1'000.0
            * std::sin(animation.orbit_angle_radians);
        position.z += std::sin(animation.orbit_angle_radians) * radius;
    }
    const bool pulses{piece.kind == ElementalPieceKind::crystal
        || piece.kind == ElementalPieceKind::orbiting_rock};
    const bool glows{piece.layer == ElementalRenderLayer::emissive
        || piece.layer == ElementalRenderLayer::additive
        || piece.kind == ElementalPieceKind::crystal};
    return {position,
        piece.base_scale_milli / 1'000.0F
            * (pulses ? animation.scale_multiplier : 1.0F),
        piece.orbit_radius_millimetres != 0 ? animation.orbit_angle_radians : 0.0F,
        glows ? animation.emission_multiplier : 1.0F};
}

PointLight crystal_point_light(
    const ElementalChamberVisual& chamber,
    const float elapsed_seconds)
{
    const ElementalTransformSample transform{
        sample_elemental_transform(chamber.crystal, elapsed_seconds)};
    const std::array<float, 3> position{
        static_cast<float>(transform.position_metres.x),
        static_cast<float>(transform.position_metres.y
            + chamber.crystal_shape.height_millimetres / 2'000.0),
        static_cast<float>(transform.position_metres.z)};
    PointLight light{chamber.crystal.stable_object_id, PointLightRole::crystal,
        position, linear_color(chamber.persona.light_color),
        2.4F * transform.emission_multiplier, 1.0F, 0.34F, 0.22F, 8.5F};
    validate_point_light(light);
    return light;
}

std::vector<std::size_t> sorted_transparent_piece_indices(
    const ElementalSceneData& visuals,
    const GeometryVector3& camera_position_metres)
{
    struct Entry {
        std::size_t flattened_index{};
        double squared_distance{};
        std::uint64_t stable_id{};
    };
    std::vector<Entry> entries;
    std::size_t flattened_index{};
    for (const ElementalChamberVisual& chamber : visuals.chambers) {
        const std::array<const ElementalVisualPiece*, 2> fixed{
            &chamber.pedestal, &chamber.crystal};
        for (const ElementalVisualPiece* piece : fixed) {
            if (piece->layer == ElementalRenderLayer::transparent) {
                entries.push_back({flattened_index,
                    distance_squared(position_metres(piece->base_position_millimetres),
                        camera_position_metres),
                    piece->stable_object_id});
            }
            ++flattened_index;
        }
        for (const ElementalVisualPiece& piece : chamber.decorations) {
            if (piece.layer == ElementalRenderLayer::transparent) {
                entries.push_back({flattened_index,
                    distance_squared(position_metres(piece.base_position_millimetres),
                        camera_position_metres),
                    piece.stable_object_id});
            }
            ++flattened_index;
        }
    }
    std::stable_sort(entries.begin(), entries.end(), [](const Entry& left, const Entry& right) {
        if (left.squared_distance != right.squared_distance) {
            return left.squared_distance > right.squared_distance;
        }
        return left.stable_id < right.stable_id;
    });
    std::vector<std::size_t> indices;
    indices.reserve(entries.size());
    for (const Entry& entry : entries) {
        indices.push_back(entry.flattened_index);
    }
    return indices;
}

}  // namespace crystalbound
