#include "crystalbound/Rendering.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <tuple>
#include <utility>

#include "crystalbound/DeterministicRandom.hpp"

namespace crystalbound {
namespace {

constexpr std::uint64_t fnv_offset_basis{14'695'981'039'346'656'037ULL};
constexpr std::uint64_t fnv_prime{1'099'511'628'211ULL};
constexpr std::uint64_t lantern_stable_id{0x4C41'4E54'4552'4E01ULL};

[[nodiscard]] bool finite_vector(const std::array<float, 3>& value) noexcept
{
    return std::isfinite(value[0]) && std::isfinite(value[1])
        && std::isfinite(value[2]);
}

[[nodiscard]] std::int64_t floor_divide(
    const std::int64_t numerator,
    const std::int64_t positive_denominator)
{
    if (positive_denominator <= 0) {
        throw std::invalid_argument("Floor division requires a positive denominator.");
    }
    std::int64_t quotient{numerator / positive_denominator};
    if (numerator % positive_denominator < 0) {
        --quotient;
    }
    return quotient;
}

[[nodiscard]] std::int64_t positive_modulo(
    const std::int64_t value,
    const std::int64_t positive_modulus)
{
    const std::int64_t remainder{value % positive_modulus};
    return remainder < 0 ? remainder + positive_modulus : remainder;
}

[[nodiscard]] std::vector<std::uint16_t> generate_lattice(
    SplitMix64& random,
    const std::uint32_t lattice_size)
{
    const std::size_t value_count{
        static_cast<std::size_t>(lattice_size) * static_cast<std::size_t>(lattice_size)};
    std::vector<std::uint16_t> lattice;
    lattice.reserve(value_count);
    for (std::size_t index{}; index < value_count; ++index) {
        lattice.push_back(static_cast<std::uint16_t>(random.next() >> 48U));
    }
    return lattice;
}

[[nodiscard]] std::uint16_t lattice_value(
    const std::vector<std::uint16_t>& lattice,
    const std::uint32_t lattice_size,
    const std::int64_t x,
    const std::int64_t y)
{
    const auto wrapped_x{static_cast<std::uint32_t>(positive_modulo(x, lattice_size))};
    const auto wrapped_y{static_cast<std::uint32_t>(positive_modulo(y, lattice_size))};
    const std::size_t index{static_cast<std::size_t>(wrapped_y) * lattice_size + wrapped_x};
    return lattice[index];
}

[[nodiscard]] std::int64_t pixel_coordinate_fixed(
    const std::uint32_t pixel,
    const std::uint32_t lattice_size,
    const std::uint32_t frequency_multiplier = 1U)
{
    const std::uint64_t numerator{static_cast<std::uint64_t>(pixel)
        * lattice_size * frequency_multiplier * static_cast<std::uint64_t>(fixed_point_one)};
    return static_cast<std::int64_t>(numerator / procedural_texture_width);
}

[[nodiscard]] std::uint8_t quantize_noise(const std::uint32_t value) noexcept
{
    return static_cast<std::uint8_t>(
        (static_cast<std::uint64_t>(value) * 255U + 32'767U) / 65'535U);
}

[[nodiscard]] double squared_distance(
    const std::array<float, 3>& left,
    const std::array<float, 3>& right) noexcept
{
    const double x{static_cast<double>(left[0]) - static_cast<double>(right[0])};
    const double y{static_cast<double>(left[1]) - static_cast<double>(right[1])};
    const double z{static_cast<double>(left[2]) - static_cast<double>(right[2])};
    return x * x + y * y + z * z;
}

}  // namespace

std::size_t texture_channel_count(const TextureFormat format) noexcept
{
    switch (format) {
    case TextureFormat::r8_linear:
        return 1U;
    case TextureFormat::srgb8:
        return 3U;
    }
    return 0U;
}

void validate_texture_image(const TextureImage& image)
{
    if (image.width == 0U || image.height == 0U) {
        throw std::invalid_argument("Texture dimensions must be positive.");
    }
    const std::size_t channels{texture_channel_count(image.format)};
    if (channels == 0U) {
        throw std::invalid_argument("Texture format is not supported.");
    }
    const std::size_t maximum_size{std::numeric_limits<std::size_t>::max()};
    if (image.width > maximum_size / image.height
        || static_cast<std::size_t>(image.width) * image.height > maximum_size / channels) {
        throw std::invalid_argument("Texture dimensions overflow the byte count.");
    }
    const std::size_t expected{static_cast<std::size_t>(image.width) * image.height * channels};
    if (image.bytes.size() != expected) {
        throw std::invalid_argument("Texture byte count does not match its dimensions and format.");
    }
    if (image.sampling.wrap_s != TextureWrap::repeat
        || image.sampling.wrap_t != TextureWrap::repeat
        || image.sampling.minimum_filter != TextureFilter::linear
        || image.sampling.magnification_filter != TextureFilter::linear) {
        throw std::invalid_argument("Procedural textures require explicit repeat and linear sampling.");
    }
}

std::uint64_t texture_byte_fingerprint(const TextureImage& image) noexcept
{
    std::uint64_t fingerprint{fnv_offset_basis};
    for (const std::uint8_t byte : image.bytes) {
        fingerprint ^= byte;
        fingerprint *= fnv_prime;
    }
    return fingerprint;
}

std::int64_t round_half_up_divide(
    const std::int64_t numerator,
    const std::int64_t positive_denominator)
{
    if (positive_denominator <= 0) {
        throw std::invalid_argument("Round-half-up division requires a positive denominator.");
    }
    const std::int64_t floor{floor_divide(numerator, positive_denominator)};
    const std::int64_t remainder{positive_modulo(numerator, positive_denominator)};
    return remainder >= (positive_denominator + 1) / 2 ? floor + 1 : floor;
}

std::int64_t fixed_multiply_round_half_up(
    const std::int64_t left,
    const std::int64_t right)
{
    constexpr std::int64_t minimum{std::numeric_limits<std::int64_t>::min()};
    constexpr std::int64_t maximum{std::numeric_limits<std::int64_t>::max()};
    const bool overflow{
        (left == -1 && right == minimum) || (right == -1 && left == minimum)
        || (left > 0 && right > 0 && left > maximum / right)
        || (left > 0 && right < 0 && right < minimum / left)
        || (left < 0 && right > 0 && left < minimum / right)
        || (left < 0 && right < 0 && left < maximum / right)};
    if (overflow) {
        throw std::overflow_error("Fixed-point multiplication overflowed signed 64-bit storage.");
    }
    return round_half_up_divide(left * right, fixed_point_one);
}

std::int64_t fixed_lerp_round_half_up(
    const std::int64_t first,
    const std::int64_t second,
    const std::int64_t amount_fixed)
{
    if (amount_fixed < 0 || amount_fixed > fixed_point_one) {
        throw std::invalid_argument("Fixed-point interpolation amount must be in [0, 1].");
    }
    return first + fixed_multiply_round_half_up(second - first, amount_fixed);
}

std::uint32_t quintic_fade_fixed(const std::uint32_t amount_fixed)
{
    if (amount_fixed > fixed_point_one) {
        throw std::invalid_argument("Quintic fade input must be in [0, 1].");
    }
    const std::int64_t t{amount_fixed};
    const std::int64_t t2{fixed_multiply_round_half_up(t, t)};
    const std::int64_t t3{fixed_multiply_round_half_up(t2, t)};
    const std::int64_t polynomial{6 * t2 - 15 * t + 10 * fixed_point_one};
    const std::int64_t result{fixed_multiply_round_half_up(t3, polynomial)};
    return static_cast<std::uint32_t>(std::clamp<std::int64_t>(result, 0, fixed_point_one));
}

std::uint32_t wood_triangle_wave_fixed(const std::uint32_t pixel) noexcept
{
    const std::uint32_t phase{pixel % wood_band_period_pixels};
    const std::uint32_t half_period{wood_band_period_pixels / 2U};
    const std::uint32_t height{phase <= half_period ? phase : wood_band_period_pixels - phase};
    return static_cast<std::uint32_t>(
        (static_cast<std::uint64_t>(height) * 65'535U + half_period / 2U)
        / half_period);
}

std::array<std::uint8_t, 3> wood_palette_color(const std::uint32_t tone_fixed) noexcept
{
    const std::uint32_t bounded_tone{std::min(tone_fixed, 65'535U)};
    std::array<std::uint8_t, 3> color{};
    for (std::size_t channel{}; channel < color.size(); ++channel) {
        const std::uint64_t blended{
            static_cast<std::uint64_t>(wood_dark_palette[channel]) * (65'535U - bounded_tone)
            + static_cast<std::uint64_t>(wood_light_palette[channel]) * bounded_tone};
        color[channel] = static_cast<std::uint8_t>((blended + 32'767U) / 65'535U);
    }
    return color;
}

std::uint16_t periodic_value_noise(
    const std::vector<std::uint16_t>& lattice,
    const std::uint32_t lattice_size,
    const std::int64_t x_fixed,
    const std::int64_t y_fixed)
{
    if (lattice_size == 0U
        || lattice.size() != static_cast<std::size_t>(lattice_size) * lattice_size) {
        throw std::invalid_argument("Value-noise lattice dimensions are invalid.");
    }
    const std::int64_t x_cell{floor_divide(x_fixed, fixed_point_one)};
    const std::int64_t y_cell{floor_divide(y_fixed, fixed_point_one)};
    const auto x_fraction{static_cast<std::uint32_t>(positive_modulo(x_fixed, fixed_point_one))};
    const auto y_fraction{static_cast<std::uint32_t>(positive_modulo(y_fixed, fixed_point_one))};
    const std::int64_t fade_x{quintic_fade_fixed(x_fraction)};
    const std::int64_t fade_y{quintic_fade_fixed(y_fraction)};
    const std::int64_t lower{fixed_lerp_round_half_up(
        lattice_value(lattice, lattice_size, x_cell, y_cell),
        lattice_value(lattice, lattice_size, x_cell + 1, y_cell), fade_x)};
    const std::int64_t upper{fixed_lerp_round_half_up(
        lattice_value(lattice, lattice_size, x_cell, y_cell + 1),
        lattice_value(lattice, lattice_size, x_cell + 1, y_cell + 1), fade_x)};
    return static_cast<std::uint16_t>(fixed_lerp_round_half_up(lower, upper, fade_y));
}

TextureImage generate_rock_texture(const Seed seed, const std::uint64_t stable_object_id)
{
    SplitMix64 random{make_substream(seed.value, random_domain::materials, stable_object_id)};
    std::array<std::vector<std::uint16_t>, rock_lattice_sizes.size()> lattices{};
    for (std::size_t octave{}; octave < lattices.size(); ++octave) {
        lattices[octave] = generate_lattice(random, rock_lattice_sizes[octave]);
    }

    TextureImage image{procedural_texture_width, procedural_texture_height,
        TextureFormat::r8_linear, procedural_texture_sampling, {}};
    image.bytes.reserve(static_cast<std::size_t>(image.width) * image.height);
    for (std::uint32_t y{}; y < image.height; ++y) {
        for (std::uint32_t x{}; x < image.width; ++x) {
            std::uint64_t weighted{};
            for (std::size_t octave{}; octave < lattices.size(); ++octave) {
                const std::uint32_t size{rock_lattice_sizes[octave]};
                const std::uint16_t sample{periodic_value_noise(
                    lattices[octave], size,
                    pixel_coordinate_fixed(x, size), pixel_coordinate_fixed(y, size))};
                weighted += static_cast<std::uint64_t>(sample) * rock_octave_weights[octave];
            }
            const std::uint32_t value{static_cast<std::uint32_t>((weighted + 7U) / 15U)};
            image.bytes.push_back(quantize_noise(std::min<std::uint32_t>(value, 65'535U)));
        }
    }
    validate_texture_image(image);
    return image;
}

TextureImage generate_wood_texture(const Seed seed, const std::uint64_t stable_object_id)
{
    SplitMix64 random{make_substream(seed.value, random_domain::materials, stable_object_id)};
    std::array<std::vector<std::uint16_t>, wood_lattice_sizes.size()> lattices{};
    for (std::size_t octave{}; octave < lattices.size(); ++octave) {
        lattices[octave] = generate_lattice(random, wood_lattice_sizes[octave]);
    }

    TextureImage image{procedural_texture_width, procedural_texture_height,
        TextureFormat::srgb8, procedural_texture_sampling, {}};
    image.bytes.reserve(static_cast<std::size_t>(image.width) * image.height * 3U);
    for (std::uint32_t y{}; y < image.height; ++y) {
        for (std::uint32_t x{}; x < image.width; ++x) {
            std::uint64_t weighted_noise{};
            for (std::size_t octave{}; octave < lattices.size(); ++octave) {
                const std::uint32_t size{wood_lattice_sizes[octave]};
                const std::uint16_t sample{periodic_value_noise(
                    lattices[octave], size,
                    pixel_coordinate_fixed(x, size),
                    pixel_coordinate_fixed(y, size, wood_vertical_frequency_multiplier))};
                weighted_noise += static_cast<std::uint64_t>(sample) * wood_octave_weights[octave];
            }
            const std::uint32_t noise{static_cast<std::uint32_t>((weighted_noise + 3U) / 7U)};
            const std::uint32_t band{wood_triangle_wave_fixed(y)};
            constexpr std::uint32_t total_weight{
                wood_noise_blend_weight + wood_band_blend_weight};
            const std::uint32_t tone{static_cast<std::uint32_t>(
                (static_cast<std::uint64_t>(noise) * wood_noise_blend_weight
                    + static_cast<std::uint64_t>(band) * wood_band_blend_weight
                    + total_weight / 2U)
                / total_weight)};
            const std::array<std::uint8_t, 3> color{wood_palette_color(tone)};
            for (const std::uint8_t channel : color) {
                image.bytes.push_back(channel);
            }
        }
    }
    validate_texture_image(image);
    return image;
}

MaterialParameters material_parameters(const MaterialKind material)
{
    switch (material) {
    case MaterialKind::wood:
        return {{0.045F, 0.032F, 0.020F}, {0.95F, 0.90F, 0.82F},
            {0.20F, 0.16F, 0.12F}, {0.0F, 0.0F, 0.0F}, 28.0F, 3.0F, 1.0F};
    case MaterialKind::rock:
        return {{0.035F, 0.040F, 0.050F}, {0.88F, 0.92F, 1.0F},
            {0.14F, 0.16F, 0.20F}, {0.0F, 0.0F, 0.0F}, 22.0F, 0.32F, 4.0F};
    case MaterialKind::untextured:
        return {{0.030F, 0.030F, 0.035F}, {1.0F, 1.0F, 1.0F},
            {0.35F, 0.35F, 0.40F}, {0.0F, 0.0F, 0.0F}, 42.0F, 1.0F, 1.0F};
    }
    throw std::invalid_argument("Material kind is not supported.");
}

void validate_material_parameters(const MaterialParameters& material)
{
    const auto contains_negative = [](const std::array<float, 3>& value) {
        return std::any_of(value.begin(), value.end(), [](const float component) {
            return component < 0.0F;
        });
    };
    if (!finite_vector(material.ambient) || !finite_vector(material.diffuse)
        || !finite_vector(material.specular) || !finite_vector(material.emission)
        || contains_negative(material.ambient) || contains_negative(material.diffuse)
        || contains_negative(material.specular) || contains_negative(material.emission)
        || !std::isfinite(material.shininess) || material.shininess <= 0.0F
        || !std::isfinite(material.texture_scale) || material.texture_scale <= 0.0F
        || !std::isfinite(material.triplanar_sharpness)
        || material.triplanar_sharpness <= 0.0F) {
        throw std::invalid_argument("Material parameters must be finite and positive where required.");
    }
}

std::array<float, 3> triplanar_blend_weights(
    const std::array<float, 3>& world_normal,
    const float sharpness)
{
    if (!finite_vector(world_normal) || !std::isfinite(sharpness) || sharpness <= 0.0F) {
        throw std::invalid_argument("Triplanar inputs must be finite with positive sharpness.");
    }
    std::array<float, 3> weights{std::pow(std::abs(world_normal[0]), sharpness),
        std::pow(std::abs(world_normal[1]), sharpness),
        std::pow(std::abs(world_normal[2]), sharpness)};
    const float total{weights[0] + weights[1] + weights[2]};
    if (!std::isfinite(total) || total <= 1.0e-8F) {
        return {1.0F / 3.0F, 1.0F / 3.0F, 1.0F / 3.0F};
    }
    for (float& weight : weights) {
        weight /= total;
    }
    return weights;
}

PointLight camera_lantern(const std::array<float, 3>& position_metres)
{
    PointLight lantern{lantern_stable_id, PointLightRole::camera_lantern,
        position_metres, {1.0F, 0.56F, 0.26F}, 2.2F, 1.0F, 0.20F, 0.14F, 10.5F};
    validate_point_light(lantern);
    return lantern;
}

void validate_point_light(const PointLight& light)
{
    const bool valid_role{light.role == PointLightRole::camera_lantern
        || light.role == PointLightRole::crystal
        || light.role == PointLightRole::decorative};
    if (!valid_role || !finite_vector(light.position_metres) || !finite_vector(light.color_linear)
        || !std::isfinite(light.intensity) || light.intensity < 0.0F
        || !std::isfinite(light.attenuation_constant) || light.attenuation_constant <= 0.0F
        || !std::isfinite(light.attenuation_linear) || light.attenuation_linear < 0.0F
        || !std::isfinite(light.attenuation_quadratic) || light.attenuation_quadratic < 0.0F
        || !std::isfinite(light.range_metres) || light.range_metres <= 0.0F
        || std::any_of(light.color_linear.begin(), light.color_linear.end(), [](const float value) {
               return value < 0.0F;
           })) {
        throw std::invalid_argument("Point light contains invalid or non-finite parameters.");
    }
}

std::vector<PointLight> select_point_lights(
    const PointLight& lantern,
    const std::vector<StableLightCandidate>& candidates)
{
    validate_point_light(lantern);
    if (lantern.role != PointLightRole::camera_lantern) {
        throw std::invalid_argument("The reserved lantern light must use the camera-lantern role.");
    }
    std::vector<StableLightCandidate> crystals;
    std::vector<StableLightCandidate> decorative;
    for (const StableLightCandidate& candidate : candidates) {
        validate_point_light(candidate.light);
        if (candidate.light.role == PointLightRole::camera_lantern) {
            throw std::invalid_argument("Light candidates must not contain another camera lantern.");
        }
        (candidate.light.role == PointLightRole::crystal ? crystals : decorative)
            .push_back(candidate);
    }
    const auto nearest_then_id = [&](const StableLightCandidate& left,
                                     const StableLightCandidate& right) {
        const double left_distance{squared_distance(left.light.position_metres, lantern.position_metres)};
        const double right_distance{squared_distance(right.light.position_metres, lantern.position_metres)};
        return std::tie(left_distance, left.light.stable_object_id)
            < std::tie(right_distance, right.light.stable_object_id);
    };
    std::stable_sort(crystals.begin(), crystals.end(), [&](const auto& left, const auto& right) {
        if (left.relevant_chamber_crystal != right.relevant_chamber_crystal) {
            return left.relevant_chamber_crystal;
        }
        return nearest_then_id(left, right);
    });
    std::stable_sort(decorative.begin(), decorative.end(), nearest_then_id);

    std::vector<PointLight> selected;
    selected.reserve(lighting_policy.maximum_point_lights);
    selected.push_back(lantern);
    for (std::size_t index{}; index < std::min(crystals.size(), lighting_policy.crystal_slots); ++index) {
        selected.push_back(crystals[index].light);
    }
    for (std::size_t index{}; index < std::min(decorative.size(), lighting_policy.decorative_slots); ++index) {
        selected.push_back(decorative[index].light);
    }
    return selected;
}

FogParameters fog_parameters_for_zone(const std::uint64_t stable_zone_id)
{
    const float start{8.0F + static_cast<float>((stable_zone_id >> 8U) % 5U) * 0.35F};
    const float end{34.0F + static_cast<float>((stable_zone_id >> 16U) % 7U) * 0.5F};
    FogParameters fog{{0.018F, 0.024F, 0.034F}, start, end};
    validate_fog_parameters(fog);
    return fog;
}

void validate_fog_parameters(const FogParameters& fog)
{
    if (!finite_vector(fog.color_linear)
        || std::any_of(fog.color_linear.begin(), fog.color_linear.end(), [](const float value) {
               return value < 0.0F;
           })
        || !std::isfinite(fog.start_distance_metres)
        || !std::isfinite(fog.end_distance_metres)
        || fog.start_distance_metres < 0.0F
        || fog.end_distance_metres <= fog.start_distance_metres) {
        throw std::invalid_argument("Fog distances and color must be finite and ordered.");
    }
}

float fog_factor(const float distance_metres, const FogParameters& fog)
{
    validate_fog_parameters(fog);
    if (!std::isfinite(distance_metres)) {
        throw std::invalid_argument("Fog distance must be finite.");
    }
    return std::clamp((distance_metres - fog.start_distance_metres)
            / (fog.end_distance_metres - fog.start_distance_metres), 0.0F, 1.0F);
}

void validate_render_pass_state(const RenderPassState& state)
{
    const bool valid_cull{state.cull_mode == CullMode::none
        || state.cull_mode == CullMode::back};
    const bool valid_blend{state.blend_mode == BlendMode::disabled
        || state.blend_mode == BlendMode::straight_alpha
        || state.blend_mode == BlendMode::premultiplied_alpha
        || state.blend_mode == BlendMode::additive};
    if (!valid_cull || !valid_blend) {
        throw std::invalid_argument("Render pass contains an unsupported state value.");
    }
    if (state.depth_write && !state.depth_test) {
        throw std::invalid_argument("A pass cannot write depth while depth testing is disabled.");
    }
    if (state.blend_mode != BlendMode::disabled && state.depth_write) {
        throw std::invalid_argument("Blended passes must not write depth.");
    }
}

}  // namespace crystalbound
