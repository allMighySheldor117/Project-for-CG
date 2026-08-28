#include "RenderingTests.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "crystalbound/CaveScene.hpp"
#include "crystalbound/DeterministicRandom.hpp"
#include "crystalbound/Rendering.hpp"

namespace crystalbound::test {
namespace {

class RenderingTestFailure final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

void require(const bool condition, const std::string_view message)
{
    if (!condition) {
        throw RenderingTestFailure{std::string{message}};
    }
}

template <typename Exception, typename Function>
void require_throws(Function&& function, const std::string_view message)
{
    try {
        function();
    } catch (const Exception&) {
        return;
    } catch (const std::exception& error) {
        throw RenderingTestFailure{
            std::string{message} + ": wrong exception: " + error.what()};
    }
    throw RenderingTestFailure{std::string{message} + ": no exception was thrown"};
}

[[nodiscard]] std::string hexadecimal(const std::uint64_t value)
{
    std::ostringstream stream;
    stream << std::hex << std::setfill('0') << std::setw(16) << value;
    return stream.str();
}

[[nodiscard]] PointLight test_light(
    const std::uint64_t id,
    const PointLightRole role,
    const float x)
{
    return {id, role, {x, 0.0F, 0.0F}, {0.4F, 0.5F, 0.6F},
        1.0F, 1.0F, 0.1F, 0.05F, 8.0F};
}

void texture_dimensions_formats_and_sampling_are_locked(const std::filesystem::path&)
{
    const TextureImage rock{generate_rock_texture({42U}, rock_texture_stable_id)};
    const TextureImage wood{generate_wood_texture({42U}, wood_texture_stable_id)};
    require(rock.width == 128U && rock.height == 128U
            && rock.format == TextureFormat::r8_linear
            && rock.bytes.size() == 128U * 128U,
        "rock texture contract changed");
    require(wood.width == 128U && wood.height == 128U
            && wood.format == TextureFormat::srgb8
            && wood.bytes.size() == 128U * 128U * 3U,
        "wood texture contract changed");
    require(rock.sampling.wrap_s == TextureWrap::repeat
            && rock.sampling.wrap_t == TextureWrap::repeat
            && rock.sampling.minimum_filter == TextureFilter::linear
            && wood.sampling.magnification_filter == TextureFilter::linear,
        "procedural texture sampling is not explicit");
}

void invalid_texture_byte_counts_are_rejected(const std::filesystem::path&)
{
    TextureImage image{2U, 2U, TextureFormat::srgb8, procedural_texture_sampling, {1U, 2U}};
    require_throws<std::invalid_argument>([&] { validate_texture_image(image); },
        "invalid byte count was accepted");
    image.format = static_cast<TextureFormat>(255U);
    image.bytes.resize(4U);
    require_throws<std::invalid_argument>([&] { validate_texture_image(image); },
        "invalid texture format was accepted");
}

void round_half_up_fixed_math_is_locked(const std::filesystem::path&)
{
    require(round_half_up_divide(1, 2) == 1, "positive half did not round up");
    require(round_half_up_divide(-1, 2) == 0, "negative half did not round toward positive infinity");
    require(fixed_multiply_round_half_up(32'768, 32'768) == 16'384,
        "fixed multiply changed");
    require(fixed_lerp_round_half_up(0, 65'535, 32'768) == 32'768,
        "fixed interpolation changed");
    require_throws<std::overflow_error>([] {
        static_cast<void>(fixed_multiply_round_half_up(
            std::numeric_limits<std::int64_t>::max(), 2));
    }, "fixed multiply overflow was not rejected");
}

void quintic_fade_boundaries_are_locked(const std::filesystem::path&)
{
    require(quintic_fade_fixed(0U) == 0U, "fade no longer starts at zero");
    require(quintic_fade_fixed(32'768U) == 32'768U, "fade midpoint changed");
    require(quintic_fade_fixed(65'536U) == 65'536U, "fade no longer ends at one");
    require_throws<std::invalid_argument>([] { static_cast<void>(quintic_fade_fixed(65'537U)); },
        "out-of-range fade input was accepted");
}

void periodic_noise_wraps_in_both_axes(const std::filesystem::path&)
{
    const std::vector<std::uint16_t> lattice{0U, 65'535U, 20'000U, 40'000U};
    const std::uint16_t original{periodic_value_noise(lattice, 2U, 21'845, 37'000)};
    require(periodic_value_noise(lattice, 2U,
                21'845 + 2 * fixed_point_one, 37'000 - 2 * fixed_point_one) == original,
        "periodic value noise did not wrap");
}

void invalid_noise_lattice_is_rejected(const std::filesystem::path&)
{
    require_throws<std::invalid_argument>([] {
        static_cast<void>(periodic_value_noise({1U, 2U}, 2U, 0, 0));
    }, "invalid value-noise lattice was accepted");
}

void rock_fbm_octaves_and_weights_are_locked(const std::filesystem::path&)
{
    require(rock_lattice_sizes == std::array<std::uint32_t, 4>{8U, 16U, 32U, 64U},
        "rock lattice sizes changed");
    require(rock_octave_weights == std::array<std::uint32_t, 4>{8U, 4U, 2U, 1U},
        "rock octave weights changed");

    SplitMix64 first{make_substream(42U, random_domain::materials, rock_texture_stable_id)};
    SplitMix64 second{make_substream(42U, random_domain::materials, rock_texture_stable_id)};
    SplitMix64 changed{make_substream(42U, random_domain::materials, rock_texture_stable_id + 1U)};
    bool changed_id_varied{};
    for (std::size_t index{}; index < 16U; ++index) {
        const std::uint16_t first_high_bits{static_cast<std::uint16_t>(first.next() >> 48U)};
        const std::uint16_t second_high_bits{static_cast<std::uint16_t>(second.next() >> 48U)};
        const std::uint16_t changed_high_bits{static_cast<std::uint16_t>(changed.next() >> 48U)};
        require(first_high_bits == second_high_bits,
            "material substream changed high-16-bit lattice values");
        changed_id_varied = changed_id_varied || first_high_bits != changed_high_bits;
    }
    require(changed_id_varied, "material substream ignored the stable object ID");
}

void rock_texture_is_repeatable_and_object_specific(const std::filesystem::path&)
{
    const TextureImage first{generate_rock_texture({42U}, rock_texture_stable_id)};
    const TextureImage second{generate_rock_texture({42U}, rock_texture_stable_id)};
    const TextureImage changed{generate_rock_texture({42U}, rock_texture_stable_id + 1U)};
    require(first.bytes == second.bytes, "same rock inputs changed bytes");
    require(first.bytes != changed.bytes, "rock stable object ID did not vary bytes");
    const auto [minimum, maximum]{std::minmax_element(first.bytes.begin(), first.bytes.end())};
    require(minimum != first.bytes.end() && *minimum < *maximum,
        "rock texture has no value variation");
}

void rock_texture_matches_golden_bytes(const std::filesystem::path&)
{
    const TextureImage image{generate_rock_texture({42U}, rock_texture_stable_id)};
    constexpr std::uint64_t expected{0xC960'C475'ACBB'3B70ULL};
    const std::uint64_t actual{texture_byte_fingerprint(image)};
    require(rock_texture_golden_fingerprint_seed_42 == expected && actual == expected,
        "rock texture golden changed; actual=" + hexadecimal(actual));
}

void wood_octaves_anisotropy_and_band_are_locked(const std::filesystem::path&)
{
    require(wood_lattice_sizes == std::array<std::uint32_t, 3>{8U, 16U, 32U},
        "wood lattice sizes changed");
    require(wood_octave_weights == std::array<std::uint32_t, 3>{4U, 2U, 1U},
        "wood octave weights changed");
    require(wood_vertical_frequency_multiplier == 4U, "wood anisotropy changed");
    require(wood_triangle_wave_fixed(0U) == 0U
            && wood_triangle_wave_fixed(16U) == 65'535U
            && wood_triangle_wave_fixed(32U) == 0U
            && wood_triangle_wave_fixed(48U) == 65'535U,
        "wood triangle-wave boundaries changed");
}

void wood_palette_and_blend_formula_are_locked(const std::filesystem::path&)
{
    require(wood_band_period_pixels == 32U
            && wood_noise_blend_weight == 3U && wood_band_blend_weight == 4U,
        "wood band or blend constants changed");
    require(wood_palette_color(0U) == wood_dark_palette,
        "wood dark palette endpoint changed");
    require(wood_palette_color(65'535U) == wood_light_palette,
        "wood light palette endpoint changed");
}

void wood_texture_is_repeatable_and_bounded(const std::filesystem::path&)
{
    const TextureImage first{generate_wood_texture({42U}, wood_texture_stable_id)};
    const TextureImage second{generate_wood_texture({42U}, wood_texture_stable_id)};
    const TextureImage changed{generate_wood_texture({43U}, wood_texture_stable_id)};
    require(first.bytes == second.bytes, "same wood inputs changed bytes");
    require(first.bytes != changed.bytes, "wood seed did not vary bytes");
    const auto [minimum, maximum]{std::minmax_element(first.bytes.begin(), first.bytes.end())};
    require(minimum != first.bytes.end() && *minimum < *maximum,
        "wood texture has no color variation");
}

void wood_texture_matches_golden_bytes(const std::filesystem::path&)
{
    const TextureImage image{generate_wood_texture({42U}, wood_texture_stable_id)};
    constexpr std::uint64_t expected{0x746A'0F29'BF16'61EDULL};
    const std::uint64_t actual{texture_byte_fingerprint(image)};
    require(wood_texture_golden_fingerprint_seed_42 == expected && actual == expected,
        "wood texture golden changed; actual=" + hexadecimal(actual));
}

void material_parameters_are_distinct_and_valid(const std::filesystem::path&)
{
    const MaterialParameters rock{material_parameters(MaterialKind::rock)};
    const MaterialParameters wood{material_parameters(MaterialKind::wood)};
    validate_material_parameters(rock);
    validate_material_parameters(wood);
    require(rock.triplanar_sharpness > wood.triplanar_sharpness
            && rock.texture_scale != wood.texture_scale,
        "rock and wood material policies are not distinct");
}

void material_profiles_cover_every_template_surface(const std::filesystem::path&)
{
    const std::array<TemplateSurfaceKind, 6> surfaces{
        TemplateSurfaceKind::stone,
        TemplateSurfaceKind::basalt,
        TemplateSurfaceKind::shallow_water,
        TemplateSurfaceKind::earth,
        TemplateSurfaceKind::wood,
        TemplateSurfaceKind::aether_stone,
    };
    std::vector<MaterialKind> mapped;
    for (const TemplateSurfaceKind surface : surfaces) {
        const MaterialKind kind{material_for_template_surface(surface)};
        validate_material_parameters(material_parameters(kind));
        validate_render_pass_state(material_profile(kind).pass);
        mapped.push_back(kind);
    }
    std::sort(mapped.begin(), mapped.end(), [](const MaterialKind left,
                                               const MaterialKind right) {
        return static_cast<std::uint8_t>(left)
            < static_cast<std::uint8_t>(right);
    });
    require(std::adjacent_find(mapped.begin(), mapped.end()) == mapped.end(),
        "template surfaces share an unintended material profile");
    require(required_cave_material_profiles().size() == 12U,
        "required material-profile inventory is incomplete");
}

void material_masks_repeat_and_respond_to_seed_and_profile(
    const std::filesystem::path&)
{
    const TextureImage first{generate_material_mask(
        {42U}, MaterialKind::basalt_lava_crust, 0x1234U)};
    const TextureImage repeat{generate_material_mask(
        {42U}, MaterialKind::basalt_lava_crust, 0x1234U)};
    const TextureImage changed_seed{generate_material_mask(
        {43U}, MaterialKind::basalt_lava_crust, 0x1234U)};
    const TextureImage changed_profile{generate_material_mask(
        {42U}, MaterialKind::wet_rock, 0x1234U)};
    require(first.bytes == repeat.bytes,
        "material mask does not repeat for the same inputs");
    require(first.bytes != changed_seed.bytes
            && first.bytes != changed_profile.bytes,
        "material mask ignores its seed or profile");
}

void transparent_profiles_preserve_pass_state(const std::filesystem::path&)
{
    for (const MaterialKind kind : {
             MaterialKind::shallow_water,
             MaterialKind::deep_water,
             MaterialKind::mist}) {
        const MaterialProfile profile{material_profile(kind)};
        require(profile.pass.framebuffer_srgb && profile.pass.depth_test
                && !profile.pass.depth_write
                && profile.pass.cull_mode == CullMode::none
                && profile.pass.blend_mode == BlendMode::premultiplied_alpha,
            "transparent profile changed its depth/blend contract");
        require(profile.visual_time_only,
            "transparent effect is not marked visual-only");
    }
    require_throws<std::invalid_argument>([] {
        validate_visual_effect_time(std::numeric_limits<float>::infinity());
    }, "non-finite visual effect time was accepted");
}

void material_budget_accumulator_rejects_overflow(const std::filesystem::path&)
{
    const std::vector<MaterialKind> profiles{
        required_cave_material_profiles()};
    const MaterialBudgetUsage usage{accumulate_material_budget(profiles)};
    require(usage.profile_count == profiles.size()
            && usage.mask_bytes > 0U,
        "material budget did not aggregate required profiles");
    require_throws<std::invalid_argument>([&] {
        static_cast<void>(accumulate_material_budget(
            profiles, {usage.profile_count - 1U, usage.mask_bytes}));
    }, "material profile-count overflow was accepted");
    require_throws<std::invalid_argument>([&] {
        static_cast<void>(accumulate_material_budget(
            profiles, {usage.profile_count, usage.mask_bytes - 1U}));
    }, "material mask-byte overflow was accepted");
}

void cave_and_tunnel_material_assignment_is_stable(const std::filesystem::path&)
{
    const CaveGenerationResult result{generate_cave({42U})};
    bool found_route{};
    std::vector<MaterialKind> chamber_materials;
    for (const SceneMeshPiece& piece : result.scene.mesh_pieces) {
        require(piece.kind != ScenePieceKind::bridge,
            "fixed authored layout unexpectedly emitted a bridge material batch");
        if (piece.kind == ScenePieceKind::tunnel
            || piece.kind == ScenePieceKind::junction) {
            require(piece.material == MaterialKind::rock,
                "neutral route lost its rock material");
            found_route = true;
        } else {
            chamber_materials.push_back(piece.material);
        }
    }
    require(found_route, "material corpus did not include a tunnel or junction");
    for (const MaterialKind expected : {
             MaterialKind::basalt_lava_crust,
             MaterialKind::lava,
             MaterialKind::wet_rock,
             MaterialKind::water_marble,
             MaterialKind::soil_mineral,
             MaterialKind::wood_bark,
             MaterialKind::aether_crystal}) {
        require(std::find(chamber_materials.begin(), chamber_materials.end(),
                    expected) != chamber_materials.end(),
            "elemental chamber structural material is missing");
    }
}

void triplanar_weights_are_finite_and_normalized(const std::filesystem::path&)
{
    const auto axis{triplanar_blend_weights({0.0F, -1.0F, 0.0F}, 4.0F)};
    require(axis == std::array<float, 3>{0.0F, 1.0F, 0.0F},
        "axis-aligned triplanar weights changed");
    const auto zero{triplanar_blend_weights({0.0F, 0.0F, 0.0F}, 4.0F)};
    const float sum{zero[0] + zero[1] + zero[2]};
    require(std::isfinite(sum) && std::abs(sum - 1.0F) < 1.0e-5F,
        "degenerate triplanar weights are not finite and normalized");
}

void light_budget_and_lantern_are_locked(const std::filesystem::path&)
{
    require(lighting_policy.maximum_point_lights == 8U
            && lighting_policy.lantern_slots == 1U
            && lighting_policy.crystal_slots == 5U
            && lighting_policy.decorative_slots == 2U,
        "point-light reservations changed");
    const PointLight lantern{camera_lantern({1.0F, 2.0F, 3.0F})};
    require(lantern.role == PointLightRole::camera_lantern
            && lantern.position_metres == std::array<float, 3>{1.0F, 2.0F, 3.0F}
            && lantern.range_metres <= 12.0F,
        "camera lantern contract changed");
}

void light_selection_reserves_roles_and_caps_at_eight(const std::filesystem::path&)
{
    const PointLight lantern{camera_lantern({0.0F, 0.0F, 0.0F})};
    std::vector<StableLightCandidate> candidates;
    for (std::uint64_t id{1U}; id <= 7U; ++id) {
        candidates.push_back({test_light(id, PointLightRole::crystal, static_cast<float>(id)), id == 7U});
    }
    for (std::uint64_t id{20U}; id <= 23U; ++id) {
        candidates.push_back({test_light(id, PointLightRole::decorative, static_cast<float>(id - 18U)), false});
    }
    const std::vector<PointLight> selected{select_point_lights(lantern, candidates)};
    require(selected.size() == 8U && selected.front().role == PointLightRole::camera_lantern,
        "light selection did not reserve and cap the lantern");
    require(selected[1].stable_object_id == 7U,
        "relevant future chamber crystal was not reserved");
    require(std::count_if(selected.begin(), selected.end(), [](const PointLight& light) {
                return light.role == PointLightRole::decorative;
            }) == 2,
        "decorative-light capacity changed");
}

void light_selection_uses_distance_then_stable_id(const std::filesystem::path&)
{
    const PointLight lantern{camera_lantern({0.0F, 0.0F, 0.0F})};
    const std::vector<StableLightCandidate> candidates{
        {test_light(9U, PointLightRole::decorative, -2.0F), false},
        {test_light(3U, PointLightRole::decorative, 2.0F), false},
        {test_light(1U, PointLightRole::decorative, 5.0F), false},
    };
    const std::vector<PointLight> selected{select_point_lights(lantern, candidates)};
    require(selected.size() == 3U && selected[1].stable_object_id == 3U
            && selected[2].stable_object_id == 9U,
        "light selection did not use nearest distance and stable-ID tie-breaking");
}

void invalid_lights_are_rejected(const std::filesystem::path&)
{
    PointLight invalid{test_light(1U, PointLightRole::decorative, 0.0F)};
    invalid.intensity = std::numeric_limits<float>::quiet_NaN();
    require_throws<std::invalid_argument>([&] { validate_point_light(invalid); },
        "non-finite point light was accepted");
}

void fog_is_deterministic_ordered_and_bounded(const std::filesystem::path&)
{
    const FogParameters first{fog_parameters_for_zone(17U)};
    const FogParameters second{fog_parameters_for_zone(17U)};
    require(first.color_linear == second.color_linear
            && first.start_distance_metres == second.start_distance_metres
            && first.end_distance_metres == second.end_distance_metres,
        "same zone changed fog parameters");
    require(fog_factor(first.start_distance_metres - 1.0F, first) == 0.0F
            && fog_factor(first.end_distance_metres + 1.0F, first) == 1.0F,
        "fog boundaries changed");
}

void invalid_fog_is_rejected(const std::filesystem::path&)
{
    require_throws<std::invalid_argument>([] {
        validate_fog_parameters({{0.0F, 0.0F, 0.0F}, 10.0F, 5.0F});
    }, "reversed fog range was accepted");
}

void render_pass_and_gamma_policy_is_explicit(const std::filesystem::path&)
{
    validate_render_pass_state(opaque_render_pass);
    validate_render_pass_state(ui_render_pass);
    require(opaque_render_pass.framebuffer_srgb && opaque_render_pass.depth_test
            && opaque_render_pass.depth_write && opaque_render_pass.cull_mode == CullMode::back
            && opaque_render_pass.blend_mode == BlendMode::disabled,
        "opaque pass policy changed");
    require(!ui_render_pass.framebuffer_srgb && !ui_render_pass.depth_test
            && !ui_render_pass.depth_write && ui_render_pass.blend_mode == BlendMode::straight_alpha,
        "UI state restoration policy changed");
}

void invalid_render_pass_is_rejected(const std::filesystem::path&)
{
    require_throws<std::invalid_argument>([] {
        validate_render_pass_state({true, false, true, CullMode::back, BlendMode::disabled});
    }, "depth write without depth test was accepted");
}

void fixed_layout_seed_contracts_remain_unchanged(const std::filesystem::path&)
{
    const CaveGenerationResult accepted{generate_cave({42U})};
    const CaveGenerationResult reference{generate_cave({123'456'789U})};
    require(!accepted.generation.used_fallback
            && accepted.scene.fingerprint == 0x1F8517F2C8D6C15AULL
            && accepted.reachability.accepted,
        "seed 42 fixed-layout acceptance changed");
    require(!reference.generation.used_fallback
            && reference.generation.effective_seed == Seed{123'456'789U}
            && reference.generation.diagnostics.size() == 1U
            && reference.scene.fingerprint == 0x52F9039C6BAA9835ULL
            && reference.reachability.accepted,
        "reference fixed-layout acceptance changed");
}

}  // namespace

std::vector<TestCase> rendering_test_cases()
{
    return {
        {"texture dimensions, formats, and sampling are locked", texture_dimensions_formats_and_sampling_are_locked},
        {"invalid texture byte counts are rejected", invalid_texture_byte_counts_are_rejected},
        {"round-half-up fixed math is locked", round_half_up_fixed_math_is_locked},
        {"quintic fade boundaries are locked", quintic_fade_boundaries_are_locked},
        {"periodic value noise wraps", periodic_noise_wraps_in_both_axes},
        {"invalid value-noise lattice is rejected", invalid_noise_lattice_is_rejected},
        {"rock FBm octaves and weights are locked", rock_fbm_octaves_and_weights_are_locked},
        {"rock texture is repeatable and object-specific", rock_texture_is_repeatable_and_object_specific},
        {"rock texture matches row-major golden bytes", rock_texture_matches_golden_bytes},
        {"wood octaves, anisotropy, and band are locked", wood_octaves_anisotropy_and_band_are_locked},
        {"wood palette and blend formula are locked", wood_palette_and_blend_formula_are_locked},
        {"wood texture is repeatable and bounded", wood_texture_is_repeatable_and_bounded},
        {"wood texture matches golden bytes", wood_texture_matches_golden_bytes},
        {"material parameters are distinct and valid", material_parameters_are_distinct_and_valid},
        {"material profiles cover every template surface",
            material_profiles_cover_every_template_surface},
        {"material masks repeat and respond to seed/profile",
            material_masks_repeat_and_respond_to_seed_and_profile},
        {"transparent profiles preserve pass state",
            transparent_profiles_preserve_pass_state},
        {"material budget accumulator rejects overflow",
            material_budget_accumulator_rejects_overflow},
        {"cave and tunnel material assignment is stable", cave_and_tunnel_material_assignment_is_stable},
        {"triplanar weights are finite and normalized", triplanar_weights_are_finite_and_normalized},
        {"light budget and lantern are locked", light_budget_and_lantern_are_locked},
        {"light selection reserves roles and caps at eight", light_selection_reserves_roles_and_caps_at_eight},
        {"light selection uses distance then stable ID", light_selection_uses_distance_then_stable_id},
        {"invalid lights are rejected", invalid_lights_are_rejected},
        {"fog is deterministic, ordered, and bounded", fog_is_deterministic_ordered_and_bounded},
        {"invalid fog is rejected", invalid_fog_is_rejected},
        {"render pass and gamma policy is explicit", render_pass_and_gamma_policy_is_explicit},
        {"invalid render pass is rejected", invalid_render_pass_is_rejected},
        {"fixed-layout seed contracts remain unchanged", fixed_layout_seed_contracts_remain_unchanged},
    };
}

}  // namespace crystalbound::test
