#include "ElementalVisualsTests.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "crystalbound/CaveScene.hpp"
#include "crystalbound/DeterministicRandom.hpp"
#include "crystalbound/ElementalVisuals.hpp"

namespace crystalbound::test {
namespace {

class ElementalVisualsTestFailure final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

void require(const bool condition, const std::string_view message)
{
    if (!condition) {
        throw ElementalVisualsTestFailure{std::string{message}};
    }
}

[[nodiscard]] const ElementalChamberVisual& chamber_for(
    const ElementalSceneData& visuals,
    const Element element)
{
    const auto found = std::find_if(
        visuals.chambers.begin(), visuals.chambers.end(), [element](const auto& chamber) {
            return chamber.element == element;
        });
    if (found == visuals.chambers.end()) {
        throw ElementalVisualsTestFailure{"elemental chamber is missing"};
    }
    return *found;
}

[[nodiscard]] double maximum_extent(const MeshData& mesh)
{
    const AxisAlignedBounds bounds{mesh_bounds(mesh)};
    return std::max({
        bounds.maximum_metres.x - bounds.minimum_metres.x,
        bounds.maximum_metres.y - bounds.minimum_metres.y,
        bounds.maximum_metres.z - bounds.minimum_metres.z,
    });
}

[[nodiscard]] std::vector<const ElementalVisualPiece*> pieces_for(
    const ElementalChamberVisual& chamber)
{
    std::vector<const ElementalVisualPiece*> pieces{&chamber.pedestal, &chamber.crystal};
    for (const ElementalVisualPiece& decoration : chamber.decorations) {
        pieces.push_back(&decoration);
    }
    return pieces;
}

void all_elements_have_one_chamber(const std::filesystem::path&)
{
    const CaveGenerationResult result{generate_cave({42U})};
    require(result.scene.elemental_visuals.chambers.size() == elemental_order.size(),
        "elemental chamber count changed");
    for (const Element element : elemental_order) {
        const ElementalChamberVisual& chamber{
            chamber_for(result.scene.elemental_visuals, element)};
        require(element_for_chamber(result.generation.topology, chamber.chamber_id) == element,
            "element-to-chamber mapping drifted");
    }
}

void personas_match_locked_identities(const std::filesystem::path&)
{
    require(elemental_persona(Element::fire).animation == CrystalAnimationKind::flicker,
        "Fire lost its flicker");
    require(elemental_persona(Element::water).animation == CrystalAnimationKind::wave,
        "Water lost its wave pulse");
    require(elemental_persona(Element::earth).animation == CrystalAnimationKind::steady,
        "Earth lost its steady glow");
    require(elemental_persona(Element::air).animation == CrystalAnimationKind::shimmer,
        "Air lost its shimmer");
    require(elemental_persona(Element::aether).animation == CrystalAnimationKind::rhythmic,
        "Aether lost its rhythmic pulse");
    std::set<std::array<std::uint16_t, 3>> colors;
    for (const Element element : elemental_order) {
        const LinearColorMilli color{elemental_persona(element).emission};
        colors.insert({color.red, color.green, color.blue});
    }
    require(colors.size() == elemental_order.size(), "elemental colors are not distinct");
}

void same_seed_repeats_elemental_contract(const std::filesystem::path&)
{
    const CaveGenerationResult first{generate_cave({42U})};
    const CaveGenerationResult second{generate_cave({42U})};
    require(first.scene.elemental_visuals.fingerprint
            == second.scene.elemental_visuals.fingerprint,
        "same seed changed elemental fingerprint");
    for (std::size_t index{}; index < first.scene.elemental_visuals.chambers.size(); ++index) {
        require(first.scene.elemental_visuals.chambers[index].fingerprint
                == second.scene.elemental_visuals.chambers[index].fingerprint,
            "same seed changed a chamber fingerprint");
    }
}

void decoration_substreams_are_stable_and_independent(const std::filesystem::path&)
{
    SplitMix64 first{make_substream(42U, random_domain::decoration, 1U)};
    SplitMix64 repeat{make_substream(42U, random_domain::decoration, 1U)};
    SplitMix64 changed{make_substream(42U, random_domain::decoration, 2U)};
    bool varied{};
    for (std::size_t index{}; index < 8U; ++index) {
        const std::uint64_t value{first.next()};
        require(value == repeat.next(), "decoration substream did not repeat");
        varied = varied || value != changed.next();
    }
    require(varied, "decoration stable object ID did not vary the substream");
}

void fixed_time_animation_is_deterministic_and_bounded(const std::filesystem::path&)
{
    const CaveGenerationResult result{generate_cave({42U})};
    for (const ElementalChamberVisual& chamber : result.scene.elemental_visuals.chambers) {
        const ElementalAnimationSample first{
            sample_elemental_animation(chamber.crystal.animation, 3.25F)};
        const ElementalAnimationSample repeat{
            sample_elemental_animation(chamber.crystal.animation, 3.25F)};
        require(first.emission_multiplier == repeat.emission_multiplier
                && first.scale_multiplier == repeat.scale_multiplier
                && first.vertical_offset_metres == repeat.vertical_offset_metres
                && first.orbit_angle_radians == repeat.orbit_angle_radians,
            "fixed-time animation sample changed");
        require(std::isfinite(first.emission_multiplier)
                && first.emission_multiplier >= 0.35F
                && first.emission_multiplier <= 2.5F
                && first.scale_multiplier >= 0.94F
                && first.scale_multiplier <= 1.08F,
            "animation sample left its visual bounds");
    }
}

void crystal_meshes_are_valid_and_distinct(const std::filesystem::path&)
{
    const CaveGenerationResult result{generate_cave({42U})};
    std::set<std::pair<std::size_t, std::size_t>> shapes;
    for (const ElementalChamberVisual& chamber : result.scene.elemental_visuals.chambers) {
        validate_procedural_mesh(chamber.crystal.mesh);
        require(chamber.crystal_shape.side_count == chamber.persona.crystal_side_count,
            "crystal shape and persona disagree");
        shapes.insert({chamber.crystal.mesh.vertices.size(), chamber.crystal.mesh.indices.size()});
    }
    require(shapes.size() >= 4U, "crystal silhouettes are not sufficiently distinct");
}

void socket_variants_are_smaller(const std::filesystem::path&)
{
    const CaveGenerationResult result{generate_cave({42U})};
    for (const ElementalChamberVisual& chamber : result.scene.elemental_visuals.chambers) {
        validate_procedural_mesh(chamber.socket_crystal_mesh);
        require(maximum_extent(chamber.socket_crystal_mesh)
                < maximum_extent(chamber.crystal.mesh) * 0.6,
            "socket crystal is not consistently smaller");
    }
}

void pedestals_and_crystals_are_present(const std::filesystem::path&)
{
    const CaveGenerationResult result{generate_cave({42U})};
    for (const ElementalChamberVisual& chamber : result.scene.elemental_visuals.chambers) {
        require(chamber.pedestal.kind == ElementalPieceKind::pedestal
                && chamber.pedestal.layer == ElementalRenderLayer::opaque,
            "elemental pedestal contract changed");
        require(chamber.crystal.kind == ElementalPieceKind::crystal
                && chamber.crystal.layer == ElementalRenderLayer::emissive,
            "elemental crystal contract changed");
    }
}

void crystal_emission_and_light_match(const std::filesystem::path&)
{
    const CaveGenerationResult result{generate_cave({42U})};
    for (const ElementalChamberVisual& chamber : result.scene.elemental_visuals.chambers) {
        const PointLight light{crystal_point_light(chamber, 1.5F)};
        require(light.role == PointLightRole::crystal
                && light.stable_object_id == chamber.crystal.stable_object_id
                && light.color_linear == linear_color(chamber.persona.light_color),
            "crystal light does not match its persona");
    }
}

void light_selection_reserves_relevant_crystal(const std::filesystem::path&)
{
    const CaveGenerationResult result{generate_cave({42U})};
    const ElementalChamberVisual& relevant{result.scene.elemental_visuals.chambers.front()};
    std::vector<StableLightCandidate> candidates;
    for (const ElementalChamberVisual& chamber : result.scene.elemental_visuals.chambers) {
        candidates.push_back({crystal_point_light(chamber, 0.0F),
            chamber.chamber_id == relevant.chamber_id});
    }
    const std::vector<PointLight> selected{
        select_point_lights(camera_lantern({500.0F, 0.0F, 0.0F}), candidates)};
    require(selected.size() == 6U && selected.front().role == PointLightRole::camera_lantern,
        "lantern and five crystal lights do not fit the cap");
    require(std::any_of(selected.begin(), selected.end(), [&](const PointLight& light) {
        return light.stable_object_id == relevant.crystal.stable_object_id;
    }), "relevant chamber crystal was not selected");
}

void transparent_sort_is_stable(const std::filesystem::path&)
{
    const CaveGenerationResult result{generate_cave({42U})};
    const std::vector<std::size_t> first{sorted_transparent_piece_indices(
        result.scene.elemental_visuals, {0.0, 0.0, 0.0})};
    const std::vector<std::size_t> second{sorted_transparent_piece_indices(
        result.scene.elemental_visuals, {0.0, 0.0, 0.0})};
    require(first == second && !first.empty(), "transparent sort is empty or unstable");
}

void elemental_budgets_are_enforced(const std::filesystem::path&)
{
    const CaveGenerationResult result{generate_cave({42U})};
    const ElementalSceneData& visuals{result.scene.elemental_visuals};
    require(result.scene.static_vertex_count + visuals.generated_vertex_count
                <= geometry_budgets.maximum_static_vertices,
        "combined static vertex budget was exceeded");
    require(result.scene.opaque_draw_call_count + visuals.opaque_draw_call_count
                <= geometry_budgets.maximum_opaque_draw_calls,
        "combined opaque draw budget was exceeded");
    require(visuals.transparent_effect_draw_count
                <= geometry_budgets.maximum_transparent_draw_calls,
        "transparent effect draw budget was exceeded");
    require(visuals.particle_count <= geometry_budgets.maximum_particles,
        "particle budget was exceeded");
}

void elemental_render_passes_are_explicit(const std::filesystem::path&)
{
    validate_render_pass_state(emissive_render_pass);
    validate_render_pass_state(transparent_effect_render_pass);
    validate_render_pass_state(additive_effect_render_pass);
    require(transparent_effect_render_pass.blend_mode == BlendMode::premultiplied_alpha
            && !transparent_effect_render_pass.depth_write,
        "transparent pass no longer uses premultiplied alpha");
    require(additive_effect_render_pass.blend_mode == BlendMode::additive
            && additive_effect_render_pass.depth_test
            && !additive_effect_render_pass.depth_write,
        "additive pass policy changed");
}

void structural_and_collision_contracts_remain_unchanged(const std::filesystem::path&)
{
    const CaveGenerationResult result{generate_cave({42U})};
    require(result.scene.fingerprint == 0x9fb15c446b74730dULL,
        "structural cave fingerprint changed");
    require(result.reachability.accepted, "elemental visuals changed reachability");
    for (const ElementalChamberVisual& chamber : result.scene.elemental_visuals.chambers) {
        for (const ElementalVisualPiece* piece : pieces_for(chamber)) {
            require(std::none_of(result.scene.colliders.begin(), result.scene.colliders.end(),
                        [&](const SceneCollider& collider) {
                            return collider.stable_object_id == piece->stable_object_id;
                        }),
                "cosmetic elemental piece entered the collider contract");
        }
    }
}

void accepted_and_fallback_visuals_repeat(const std::filesystem::path&)
{
    for (const Seed seed : {Seed{42U}, Seed{123'456'789U}}) {
        const CaveGenerationResult first{generate_cave(seed)};
        const CaveGenerationResult second{generate_cave(seed)};
        require(first.scene.elemental_visuals.fingerprint
                == second.scene.elemental_visuals.fingerprint,
            "accepted or fallback elemental visuals changed");
    }
}

void generated_elemental_scene_validates(const std::filesystem::path&)
{
    const CaveGenerationResult result{generate_cave({42U})};
    require(validate_elemental_scene(
                result.generation.topology, result.scene.elemental_visuals).empty(),
        "generated elemental visuals did not validate");
}

}  // namespace

std::vector<TestCase> elemental_visuals_test_cases()
{
    return {
        {"all elements have one chamber", all_elements_have_one_chamber},
        {"personas match locked identities", personas_match_locked_identities},
        {"same seed repeats elemental contract", same_seed_repeats_elemental_contract},
        {"decoration substreams are stable and independent", decoration_substreams_are_stable_and_independent},
        {"fixed-time animation is deterministic and bounded", fixed_time_animation_is_deterministic_and_bounded},
        {"crystal meshes are valid and distinct", crystal_meshes_are_valid_and_distinct},
        {"socket crystal variants are smaller", socket_variants_are_smaller},
        {"pedestals and crystals are present", pedestals_and_crystals_are_present},
        {"crystal emission and light match", crystal_emission_and_light_match},
        {"light selection reserves relevant crystal", light_selection_reserves_relevant_crystal},
        {"transparent sorting is stable", transparent_sort_is_stable},
        {"elemental budgets are enforced", elemental_budgets_are_enforced},
        {"elemental render passes are explicit", elemental_render_passes_are_explicit},
        {"structural and collision contracts remain unchanged", structural_and_collision_contracts_remain_unchanged},
        {"accepted and fallback visuals repeat", accepted_and_fallback_visuals_repeat},
        {"generated elemental scene validates", generated_elemental_scene_validates},
    };
}

}  // namespace crystalbound::test
