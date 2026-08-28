#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "crystalbound/ChamberTemplates.hpp"
#include "crystalbound/Generation.hpp"

namespace crystalbound {

inline constexpr std::uint32_t procedural_texture_width{128U};
inline constexpr std::uint32_t procedural_texture_height{128U};
inline constexpr std::int64_t fixed_point_one{65'536};
inline constexpr std::uint64_t rock_texture_stable_id{0x524F'434B'0000'0001ULL};
inline constexpr std::uint64_t wood_texture_stable_id{0x574F'4F44'0000'0001ULL};
inline constexpr std::uint64_t rock_texture_golden_fingerprint_seed_42{
    0xC960'C475'ACBB'3B70ULL};
inline constexpr std::uint64_t wood_texture_golden_fingerprint_seed_42{
    0x746A'0F29'BF16'61EDULL};
inline constexpr std::array<std::uint32_t, 4> rock_lattice_sizes{8U, 16U, 32U, 64U};
inline constexpr std::array<std::uint32_t, 4> rock_octave_weights{8U, 4U, 2U, 1U};
inline constexpr std::array<std::uint32_t, 3> wood_lattice_sizes{8U, 16U, 32U};
inline constexpr std::array<std::uint32_t, 3> wood_octave_weights{4U, 2U, 1U};

enum class MaterialKind : std::uint8_t {
    rock,
    wood,
    untextured,
    basalt_lava_crust,
    lava,
    wet_rock,
    shallow_water,
    deep_water,
    soil_mineral,
    wood_bark,
    aether_crystal,
    mist,
    water_marble,
};
enum class MaterialProjection : std::uint8_t { triplanar, regular_uv, none };
enum class MaterialEffect : std::uint8_t {
    none,
    lava_flow,
    water_ripple,
    drifting_mist,
    wind,
    aether_pulse,
};
enum class TextureFormat : std::uint8_t { r8_linear, srgb8 };
enum class TextureWrap : std::uint8_t { repeat };
enum class TextureFilter : std::uint8_t { linear };

struct TextureSamplingContract {
    TextureWrap wrap_s{TextureWrap::repeat};
    TextureWrap wrap_t{TextureWrap::repeat};
    TextureFilter minimum_filter{TextureFilter::linear};
    TextureFilter magnification_filter{TextureFilter::linear};
};

inline constexpr TextureSamplingContract procedural_texture_sampling{};

struct TextureImage {
    std::uint32_t width{};
    std::uint32_t height{};
    TextureFormat format{TextureFormat::r8_linear};
    TextureSamplingContract sampling{};
    std::vector<std::uint8_t> bytes{};
};

struct MaterialParameters {
    std::array<float, 3> ambient{};
    std::array<float, 3> diffuse{};
    std::array<float, 3> specular{};
    std::array<float, 3> emission{};
    float shininess{};
    float texture_scale{};
    float triplanar_sharpness{};
};

enum class PointLightRole : std::uint8_t { camera_lantern, crystal, decorative };

struct PointLight {
    std::uint64_t stable_object_id{};
    PointLightRole role{PointLightRole::decorative};
    std::array<float, 3> position_metres{};
    std::array<float, 3> color_linear{};
    float intensity{};
    float attenuation_constant{};
    float attenuation_linear{};
    float attenuation_quadratic{};
    float range_metres{};
};

struct StableLightCandidate {
    PointLight light{};
    bool relevant_chamber_crystal{};
};

struct LightingPolicy {
    std::size_t maximum_point_lights{};
    std::size_t lantern_slots{};
    std::size_t crystal_slots{};
    std::size_t decorative_slots{};
};

inline constexpr LightingPolicy lighting_policy{8U, 1U, 5U, 2U};

struct FogParameters {
    std::array<float, 3> color_linear{};
    float start_distance_metres{};
    float end_distance_metres{};
};

enum class CullMode : std::uint8_t { none, back };
enum class BlendMode : std::uint8_t {
    disabled,
    straight_alpha,
    premultiplied_alpha,
    additive,
};

struct RenderPassState {
    bool framebuffer_srgb{};
    bool depth_test{};
    bool depth_write{};
    CullMode cull_mode{CullMode::none};
    BlendMode blend_mode{BlendMode::disabled};
};

inline constexpr RenderPassState opaque_render_pass{
    true, true, true, CullMode::back, BlendMode::disabled};
inline constexpr RenderPassState emissive_render_pass{
    true, true, true, CullMode::back, BlendMode::disabled};
inline constexpr RenderPassState transparent_effect_render_pass{
    true, true, false, CullMode::none, BlendMode::premultiplied_alpha};
inline constexpr RenderPassState additive_effect_render_pass{
    true, true, false, CullMode::none, BlendMode::additive};
inline constexpr RenderPassState ui_render_pass{
    false, false, false, CullMode::none, BlendMode::straight_alpha};

struct MaterialProfile {
    MaterialKind kind{MaterialKind::rock};
    MaterialProjection projection{MaterialProjection::triplanar};
    MaterialEffect effect{MaterialEffect::none};
    RenderPassState pass{};
    std::uint32_t mask_bytes{};
    bool visual_time_only{};
};

struct MaterialBudgetLimits {
    std::uint32_t maximum_profiles{16U};
    std::uint64_t maximum_mask_bytes{2U * 1024U * 1024U};
};

struct MaterialBudgetUsage {
    std::uint32_t profile_count{};
    std::uint64_t mask_bytes{};
};

inline constexpr std::uint32_t wood_band_period_pixels{32U};
inline constexpr std::array<std::uint8_t, 3> wood_dark_palette{58U, 30U, 13U};
inline constexpr std::array<std::uint8_t, 3> wood_light_palette{166U, 102U, 44U};
inline constexpr std::uint32_t wood_noise_blend_weight{3U};
inline constexpr std::uint32_t wood_band_blend_weight{4U};
inline constexpr std::uint32_t wood_vertical_frequency_multiplier{4U};

[[nodiscard]] std::size_t texture_channel_count(TextureFormat format) noexcept;
void validate_texture_image(const TextureImage& image);
[[nodiscard]] std::uint64_t texture_byte_fingerprint(const TextureImage& image) noexcept;

[[nodiscard]] std::int64_t round_half_up_divide(
    std::int64_t numerator, std::int64_t positive_denominator);
[[nodiscard]] std::int64_t fixed_multiply_round_half_up(
    std::int64_t left, std::int64_t right);
[[nodiscard]] std::int64_t fixed_lerp_round_half_up(
    std::int64_t first, std::int64_t second, std::int64_t amount_fixed);
[[nodiscard]] std::uint32_t quintic_fade_fixed(std::uint32_t amount_fixed);
[[nodiscard]] std::uint32_t wood_triangle_wave_fixed(std::uint32_t pixel) noexcept;
[[nodiscard]] std::array<std::uint8_t, 3> wood_palette_color(
    std::uint32_t tone_fixed) noexcept;
[[nodiscard]] std::uint16_t periodic_value_noise(
    const std::vector<std::uint16_t>& lattice,
    std::uint32_t lattice_size,
    std::int64_t x_fixed,
    std::int64_t y_fixed);

[[nodiscard]] TextureImage generate_rock_texture(Seed seed, std::uint64_t stable_object_id);
[[nodiscard]] TextureImage generate_wood_texture(Seed seed, std::uint64_t stable_object_id);
[[nodiscard]] TextureImage generate_material_mask(
    Seed seed, MaterialKind profile, std::uint64_t stable_object_id);

[[nodiscard]] MaterialParameters material_parameters(MaterialKind material);
[[nodiscard]] MaterialProfile material_profile(MaterialKind material);
[[nodiscard]] MaterialKind material_for_template_surface(
    TemplateSurfaceKind surface);
[[nodiscard]] std::vector<MaterialKind> required_cave_material_profiles();
[[nodiscard]] MaterialBudgetUsage accumulate_material_budget(
    const std::vector<MaterialKind>& profiles,
    MaterialBudgetLimits limits = {});
void validate_visual_effect_time(float elapsed_seconds);
void validate_material_parameters(const MaterialParameters& material);
[[nodiscard]] std::array<float, 3> triplanar_blend_weights(
    const std::array<float, 3>& world_normal, float sharpness);

[[nodiscard]] PointLight camera_lantern(const std::array<float, 3>& position_metres);
void validate_point_light(const PointLight& light);
[[nodiscard]] std::vector<PointLight> select_point_lights(
    const PointLight& lantern,
    const std::vector<StableLightCandidate>& candidates);

[[nodiscard]] FogParameters fog_parameters_for_zone(std::uint64_t stable_zone_id);
void validate_fog_parameters(const FogParameters& fog);
[[nodiscard]] float fog_factor(float distance_metres, const FogParameters& fog);

void validate_render_pass_state(const RenderPassState& state);

}  // namespace crystalbound
