#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "crystalbound/Generation.hpp"
#include "crystalbound/Geometry.hpp"
#include "crystalbound/Rendering.hpp"

namespace crystalbound {

inline constexpr std::array<Element, 5> elemental_order{
    Element::fire, Element::water, Element::earth, Element::air, Element::aether};

enum class CrystalAnimationKind : std::uint8_t {
    flicker,
    wave,
    steady,
    shimmer,
    rhythmic,
};

enum class ElementalPieceKind : std::uint8_t {
    pedestal,
    crystal,
    lava_rock,
    glowing_crack,
    lavafall,
    ember_vent,
    water_surface,
    waterfall,
    water_bank,
    water_plant,
    earth_pillar,
    earth_stalagmite,
    air_wood_spire,
    elemental_arch,
    orbiting_rock,
    fog_ribbon,
    particle_cluster,
    formation_batch,
};

enum class ElementalRenderLayer : std::uint8_t {
    opaque,
    emissive,
    transparent,
    additive,
};

enum class FormationAttachmentSurface : std::uint8_t {
    floor,
    wall,
    ceiling,
    suspended,
};

enum class ElementalMotifFamily : std::uint8_t {
    fire_basalt,
    fire_lava_terrace,
    fire_ember_vent,
    water_wet_stone,
    water_eroded_bank,
    water_reed_spire,
    earth_pillar,
    earth_stalagmite,
    earth_shelf,
    air_timber,
    air_slender_spire,
    air_suspended_frame,
    aether_arch,
    aether_orbit,
    aether_shard,
};

struct ElementalChamberSpatialContract {
    NodeId chamber_id{};
    IntegerPoint3 center_millimetres{};
    std::int32_t usable_radius_millimetres{};
    std::int32_t usable_height_millimetres{};
    std::vector<IntegerPoint3> portal_centers_millimetres{};
};

struct ElementalFormationInstance {
    Element element{Element::fire};
    ElementalMotifFamily motif{ElementalMotifFamily::fire_basalt};
    std::uint64_t stable_object_id{};
    std::uint64_t render_batch_id{};
    std::uint32_t group_id{};
    IntegerPoint3 position_millimetres{};
    std::uint32_t scale_milli{1'000U};
    std::int32_t rotation_millidegrees{};
    FormationAttachmentSurface attachment{FormationAttachmentSurface::floor};
    IntegerPoint3 bounds_minimum_millimetres{};
    IntegerPoint3 bounds_maximum_millimetres{};
    bool keep_clear_verified{};
    bool dominant_landmark{};
};

struct LinearColorMilli {
    std::uint16_t red{};
    std::uint16_t green{};
    std::uint16_t blue{};
};

constexpr bool operator==(
    const LinearColorMilli& left,
    const LinearColorMilli& right) noexcept
{
    return left.red == right.red && left.green == right.green
        && left.blue == right.blue;
}

struct CrystalShapeContract {
    std::uint32_t side_count{};
    std::int32_t radius_millimetres{};
    std::int32_t height_millimetres{};
    std::int32_t shoulder_height_millimetres{};
    std::vector<std::int32_t> radial_offsets_millimetres{};
};

struct ElementalAnimationParameters {
    CrystalAnimationKind kind{CrystalAnimationKind::steady};
    std::uint32_t base_emission_milli{};
    std::uint32_t amplitude_milli{};
    std::uint32_t frequency_millihertz{};
    std::uint32_t phase_millidegrees{};
};

constexpr bool operator==(
    const ElementalAnimationParameters& left,
    const ElementalAnimationParameters& right) noexcept
{
    return left.kind == right.kind
        && left.base_emission_milli == right.base_emission_milli
        && left.amplitude_milli == right.amplitude_milli
        && left.frequency_millihertz == right.frequency_millihertz
        && left.phase_millidegrees == right.phase_millidegrees;
}

struct ElementalPersona {
    Element element{Element::fire};
    CrystalAnimationKind animation{CrystalAnimationKind::flicker};
    LinearColorMilli albedo{};
    LinearColorMilli emission{};
    LinearColorMilli light_color{};
    std::uint32_t crystal_side_count{};
    std::int32_t crystal_radius_millimetres{};
    std::int32_t crystal_height_millimetres{};
    std::uint32_t socket_scale_milli{};
};

struct ElementalVisualPiece {
    ElementalPieceKind kind{ElementalPieceKind::pedestal};
    ElementalRenderLayer layer{ElementalRenderLayer::opaque};
    std::uint64_t stable_object_id{};
    NodeId chamber_id{};
    Element element{Element::fire};
    MaterialKind material{MaterialKind::untextured};
    MeshData mesh{};
    IntegerPoint3 base_position_millimetres{};
    std::uint32_t base_scale_milli{1'000U};
    std::int32_t orbit_radius_millimetres{};
    std::int32_t orbit_height_millimetres{};
    std::uint16_t alpha_milli{1'000U};
    LinearColorMilli albedo{};
    LinearColorMilli emission{};
    ElementalAnimationParameters animation{};
    std::uint32_t particle_count{};
};

struct ElementalChamberVisual {
    NodeId chamber_id{};
    Element element{Element::fire};
    std::uint64_t stable_object_id{};
    ElementalPersona persona{};
    CrystalShapeContract crystal_shape{};
    ElementalVisualPiece pedestal{};
    ElementalVisualPiece crystal{};
    MeshData socket_crystal_mesh{};
    std::vector<ElementalFormationInstance> formations{};
    std::vector<ElementalVisualPiece> decorations{};
    std::uint64_t fingerprint{};
};

struct ElementalSceneData {
    std::vector<ElementalChamberVisual> chambers{};
    std::vector<ElementalChamberSpatialContract> spatial_contracts{};
    std::uint32_t generated_vertex_count{};
    std::uint32_t opaque_draw_call_count{};
    std::uint32_t transparent_effect_draw_count{};
    std::uint32_t particle_count{};
    std::uint64_t fingerprint{};
};

struct ElementalAnimationSample {
    float emission_multiplier{};
    float scale_multiplier{};
    float vertical_offset_metres{};
    float orbit_angle_radians{};
};

struct ElementalTransformSample {
    GeometryVector3 position_metres{};
    float uniform_scale{};
    float rotation_y_radians{};
    float emission_multiplier{};
};

[[nodiscard]] std::string_view element_name(Element element) noexcept;
[[nodiscard]] const ElementalPersona& elemental_persona(Element element);
[[nodiscard]] std::array<float, 3> linear_color(const LinearColorMilli& color) noexcept;
[[nodiscard]] std::optional<Element> element_for_chamber(
    const TopologyData& topology,
    NodeId chamber_id) noexcept;
[[nodiscard]] GeometryVector3 crystal_visible_body_aim_point(
    const ElementalChamberVisual& chamber) noexcept;
[[nodiscard]] ElementalSceneData build_elemental_scene(
    const TopologyData& topology,
    Seed effective_seed,
    const std::vector<ElementalChamberSpatialContract>& spatial_contracts);
[[nodiscard]] std::vector<std::string> validate_elemental_scene(
    const TopologyData& topology,
    const ElementalSceneData& visuals);
[[nodiscard]] std::uint64_t elemental_scene_fingerprint(
    Seed effective_seed,
    const ElementalSceneData& visuals);
[[nodiscard]] ElementalAnimationSample sample_elemental_animation(
    const ElementalAnimationParameters& parameters,
    float elapsed_seconds);
[[nodiscard]] ElementalTransformSample sample_elemental_transform(
    const ElementalVisualPiece& piece,
    float elapsed_seconds);
[[nodiscard]] PointLight crystal_point_light(
    const ElementalChamberVisual& chamber,
    float elapsed_seconds);
[[nodiscard]] std::vector<std::size_t> sorted_transparent_piece_indices(
    const ElementalSceneData& visuals,
    const GeometryVector3& camera_position_metres);

}  // namespace crystalbound
