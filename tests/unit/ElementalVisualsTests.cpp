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
    const ElementalPersona& fire{elemental_persona(Element::fire)};
    require(fire.albedo.red > fire.albedo.green * 4U
            && fire.albedo.red > fire.albedo.blue * 4U,
        "Fire collectible is not distinctly red");
    const ElementalPersona& water{elemental_persona(Element::water)};
    require(water.albedo.blue > water.albedo.green * 2U
            && water.albedo.blue > water.albedo.red * 4U,
        "Water collectible is not distinctly blue");
    const ElementalPersona& earth{elemental_persona(Element::earth)};
    require(earth.albedo.red > earth.albedo.green
            && earth.albedo.green > earth.albedo.blue,
        "Earth collectible is not a brown mineral color");
    const ElementalPersona& air{elemental_persona(Element::air)};
    const std::uint16_t air_minimum{std::min({
        air.albedo.red, air.albedo.green, air.albedo.blue})};
    const std::uint16_t air_maximum{std::max({
        air.albedo.red, air.albedo.green, air.albedo.blue})};
    require(air_minimum >= 850U && air_maximum - air_minimum <= 150U,
        "Air collectible is not white");
    const ElementalPersona& aether{elemental_persona(Element::aether)};
    require(aether.albedo.blue > aether.albedo.red
            && aether.albedo.red > aether.albedo.green,
        "Aether collectible is not purple");
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

[[nodiscard]] bool contains_error(
    const std::vector<std::string>& errors,
    const std::string_view needle)
{
    return std::any_of(errors.begin(), errors.end(),
        [needle](const std::string& error) {
            return error.find(needle) != std::string::npos;
        });
}

void crystal_scale_is_player_relative(const std::filesystem::path&)
{
    const CaveGenerationResult result{generate_cave({42U})};
    constexpr double minimum_height_metres{0.75};
    constexpr double maximum_height_metres{1.10};
    constexpr double minimum_radius_metres{0.25};
    constexpr double maximum_radius_metres{0.42};
    constexpr double mesh_measurement_tolerance{1.0e-5};
    for (const ElementalChamberVisual& chamber : result.scene.elemental_visuals.chambers) {
        const double scale{
            static_cast<double>(chamber.crystal.base_scale_milli) / 1'000.0};
        double minimum_y{std::numeric_limits<double>::infinity()};
        double maximum_y{-std::numeric_limits<double>::infinity()};
        double widest_radius{};
        for (const Vertex& vertex : chamber.crystal.mesh.vertices) {
            minimum_y = std::min(minimum_y,
                static_cast<double>(vertex.position[1]) * scale);
            maximum_y = std::max(maximum_y,
                static_cast<double>(vertex.position[1]) * scale);
            widest_radius = std::max(widest_radius,
                std::hypot(static_cast<double>(vertex.position[0]),
                    static_cast<double>(vertex.position[2])) * scale);
        }
        const double height{maximum_y - minimum_y};
        require(height >= minimum_height_metres - mesh_measurement_tolerance
                && height <= maximum_height_metres + mesh_measurement_tolerance,
            std::string{element_name(chamber.element)}
                + " crystal height is outside the 0.75-1.10 m band: "
                + std::to_string(height));
        require(widest_radius >= minimum_radius_metres - mesh_measurement_tolerance
                && widest_radius <= maximum_radius_metres + mesh_measurement_tolerance,
            std::string{element_name(chamber.element)}
                + " crystal radius is outside the 0.25-0.42 m band: "
                + std::to_string(widest_radius));
    }
}

void crystal_animation_and_light_transforms_stay_synchronized(
    const std::filesystem::path&)
{
    const CaveGenerationResult result{generate_cave({42U})};
    constexpr std::array<float, 5> sample_times{
        0.0F, 0.35F, 1.25F, 3.5F, 8.0F};
    for (const ElementalChamberVisual& chamber : result.scene.elemental_visuals.chambers) {
        for (const float time : sample_times) {
            const ElementalTransformSample transform{
                sample_elemental_transform(chamber.crystal, time)};
            const PointLight light{crystal_point_light(chamber, time)};
            const double expected_light_y{transform.position_metres.y
                + static_cast<double>(chamber.crystal_shape.height_millimetres)
                    / 2'000.0 * transform.uniform_scale};
            require(std::isfinite(transform.position_metres.x)
                    && std::isfinite(transform.position_metres.y)
                    && std::isfinite(transform.position_metres.z)
                    && std::isfinite(transform.uniform_scale)
                    && std::isfinite(light.position_metres[0])
                    && std::isfinite(light.position_metres[1])
                    && std::isfinite(light.position_metres[2]),
                "crystal animation or light transform became non-finite");
            require(std::abs(static_cast<double>(light.position_metres[0])
                        - transform.position_metres.x) < 1.0e-5
                    && std::abs(static_cast<double>(light.position_metres[1])
                        - expected_light_y) < 1.0e-5
                    && std::abs(static_cast<double>(light.position_metres[2])
                        - transform.position_metres.z) < 1.0e-5,
                std::string{element_name(chamber.element)}
                    + " crystal light drifted from its animated visible body");
        }
    }
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

void elemental_formations_are_grouped_batched_and_keep_clear(
    const std::filesystem::path&)
{
    const CaveGenerationResult result{generate_cave({42U})};
    std::set<std::set<ElementalMotifFamily>> elemental_languages;
    for (const ElementalChamberVisual& chamber : result.scene.elemental_visuals.chambers) {
        if (chamber.element == Element::fire
            || chamber.element == Element::water
            || chamber.element == Element::earth
            || chamber.element == Element::air) {
            require(chamber.formations.empty(),
                "authored chamber must not retain generated formations");
            continue;
        }
        require(chamber.formations.size() == 15U,
            "elemental chamber must contain fifteen logical formations");
        std::set<std::uint64_t> ids;
        std::set<std::uint32_t> groups;
        std::set<ElementalMotifFamily> motifs;
        std::set<std::uint64_t> render_batches;
        std::uint32_t dominant_count{};
        std::array<GeometryVector3, 5U> group_centers{};
        std::array<std::uint32_t, 5U> group_counts{};
        for (const ElementalFormationInstance& formation : chamber.formations) {
            ids.insert(formation.stable_object_id);
            groups.insert(formation.group_id);
            motifs.insert(formation.motif);
            render_batches.insert(formation.render_batch_id);
            dominant_count += formation.dominant_landmark ? 1U : 0U;
            require(formation.keep_clear_verified,
                "formation lacks keep-clear evidence");
            require(formation.group_id < group_centers.size(),
                "formation group is outside the locked range");
            group_centers[formation.group_id].x
                += formation.position_millimetres.x_millimetres / 1'000.0;
            group_centers[formation.group_id].z
                += formation.position_millimetres.z_millimetres / 1'000.0;
            ++group_counts[formation.group_id];
        }
        require(ids.size() == chamber.formations.size()
                && groups.size() == 5U && motifs.size() == 3U
                && render_batches.size() == 5U && dominant_count == 1U,
            "formation identity, grouping, batching, or landmark contract drifted");
        elemental_languages.insert(motifs);
        for (std::size_t first{}; first < group_centers.size(); ++first) {
            require(group_counts[first] == 3U,
                "identity group does not contain three formations");
            group_centers[first].x /= group_counts[first];
            group_centers[first].z /= group_counts[first];
            for (std::size_t second{}; second < first; ++second) {
                const double dx{group_centers[first].x - group_centers[second].x};
                const double dz{group_centers[first].z - group_centers[second].z};
                const double maximum_crystal_diameter{
                    2.0 * (chamber.crystal_shape.radius_millimetres + 20)
                    / 1'000.0 * 1.08};
                require(std::hypot(dx, dz) >= maximum_crystal_diameter,
                    "identity group anchors are not spatially separated");
            }
        }
        for (const std::uint64_t batch_id : render_batches) {
            require(std::any_of(chamber.decorations.begin(), chamber.decorations.end(),
                    [&](const ElementalVisualPiece& piece) {
                        return piece.stable_object_id == batch_id
                            && piece.kind == ElementalPieceKind::formation_batch
                            && piece.layer == ElementalRenderLayer::opaque;
                    }),
                "logical formations lost their batched render mesh");
        }
    }
    require(elemental_languages.size() == elemental_order.size() - 4U,
        "the procedural Aether room lost its distinct motif language");
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
    require(result.scene.fingerprint == 0x52CCEB23A788803DULL,
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

void authored_fire_keeps_only_the_collectible_crystal(const std::filesystem::path&)
{
    const CaveGenerationResult result{generate_cave({42U})};
    const ElementalChamberVisual& fire{
        chamber_for(result.scene.elemental_visuals, Element::fire)};
    require(fire.formations.empty(),
        "Fire chamber retains old generated formations");
    require(fire.decorations.empty(),
        "Fire chamber retains old generated decorations");
    require(!fire.crystal.mesh.vertices.empty()
            && fire.crystal.kind == ElementalPieceKind::crystal,
        "Fire chamber lost its collectible crystal");
    const auto fire_node{std::find_if(
        result.generation.topology.nodes.begin(),
        result.generation.topology.nodes.end(),
        [](const ChamberNode& node) {
            return node.element == Element::fire;
        })};
    require(fire_node != result.generation.topology.nodes.end()
            && fire.crystal.base_position_millimetres.y_millimetres
                == fire_node->anchor.elevation_millimetres
                    + authored_fire_crystal_base_height_millimetres,
        "Fire collectible does not float above the authored central altar");
}

void water_visual_bindings_cover_every_water_anchor(const std::filesystem::path&)
{
    const CaveGenerationResult result{generate_cave({42U})};
    const ElementalChamberVisual& water{
        chamber_for(result.scene.elemental_visuals, Element::water)};
    require(water.decorations.empty(),
        "Water chamber retains generated scenery supplied by the authored OBJ");
    require(water.formations.empty(),
        "Water chamber retains logical non-collectible formations");
    require(!water.crystal.mesh.vertices.empty()
            && water.crystal.kind == ElementalPieceKind::crystal,
        "Water chamber lost its collectible crystal");
}

void authored_earth_keeps_only_the_collectible_crystal(
    const std::filesystem::path&)
{
    const CaveGenerationResult result{generate_cave({42U})};
    const ElementalChamberVisual& earth{
        chamber_for(result.scene.elemental_visuals, Element::earth)};
    require(earth.formations.empty(),
        "Earth chamber retains old generated formations");
    require(earth.decorations.empty(),
        "Earth chamber retains old generated decorations");
    require(!earth.crystal.mesh.vertices.empty()
            && earth.crystal.kind == ElementalPieceKind::crystal,
        "Earth chamber lost its collectible crystal");
}

void authored_air_keeps_one_centered_collectible_crystal(
    const std::filesystem::path&)
{
    const CaveGenerationResult result{generate_cave({42U})};
    const ElementalChamberVisual& air{
        chamber_for(result.scene.elemental_visuals, Element::air)};
    const auto air_node{std::find_if(
        result.generation.topology.nodes.begin(),
        result.generation.topology.nodes.end(),
        [](const ChamberNode& node) {
            return node.element == Element::air;
        })};
    require(air_node != result.generation.topology.nodes.end(),
        "generated topology has no Air chamber node");
    require(air.formations.empty(),
        "Air chamber retains old generated formations");
    require(air.decorations.empty(),
        "Air chamber retains old generated decorations");
    require(!air.crystal.mesh.vertices.empty()
            && air.crystal.kind == ElementalPieceKind::crystal,
        "Air chamber lost its collectible crystal");
    require(air.crystal.base_position_millimetres.x_millimetres
                == air_node->anchor.x_millimetres
            && air.crystal.base_position_millimetres.z_millimetres
                == air_node->anchor.z_millimetres,
        "Air collectible crystal is not centered in the authored chamber");
}

void visual_pieces_stay_inside_owning_cosmetic_zones(
    const std::filesystem::path&)
{
    const CaveGenerationResult result{generate_cave({42U})};
    for (const ElementalChamberVisual& chamber
        : result.scene.elemental_visuals.chambers) {
        const auto spatial{std::find_if(
            result.scene.elemental_visuals.spatial_contracts.begin(),
            result.scene.elemental_visuals.spatial_contracts.end(),
            [&](const ElementalChamberSpatialContract& contract) {
                return contract.chamber_id == chamber.chamber_id;
            })};
        require(spatial != result.scene.elemental_visuals.spatial_contracts.end(),
            "elemental cosmetic zone is missing");
        for (const ElementalVisualPiece* piece : pieces_for(chamber)) {
            const std::int64_t dx{static_cast<std::int64_t>(
                piece->base_position_millimetres.x_millimetres)
                - spatial->center_millimetres.x_millimetres};
            const std::int64_t dz{static_cast<std::int64_t>(
                piece->base_position_millimetres.z_millimetres)
                - spatial->center_millimetres.z_millimetres};
            const std::int64_t radius{spatial->usable_radius_millimetres};
            require(dx * dx + dz * dz < radius * radius,
                "visual piece escaped its owning cosmetic zone");
        }
    }
}

void elemental_material_budgets_aggregate(const std::filesystem::path&)
{
    const CaveGenerationResult result{generate_cave({42U})};
    std::vector<MaterialKind> materials;
    for (const ElementalChamberVisual& chamber
        : result.scene.elemental_visuals.chambers) {
        for (const ElementalVisualPiece* piece : pieces_for(chamber)) {
            materials.push_back(piece->material);
        }
    }
    const MaterialBudgetUsage usage{accumulate_material_budget(materials)};
    require(usage.profile_count >= 3U && usage.mask_bytes > 0U,
        "elemental material budget omitted active profiles");
}

void cosmetics_never_alter_gameplay_fingerprints(const std::filesystem::path&)
{
    const CaveGenerationResult result{generate_cave({42U})};
    ElementalSceneData changed{result.scene.elemental_visuals};
    auto decorated{std::find_if(changed.chambers.begin(), changed.chambers.end(),
        [](const ElementalChamberVisual& chamber) {
            return !chamber.decorations.empty();
        })};
    require(decorated != changed.chambers.end(),
        "no generated cosmetic remains available for fingerprint mutation");
    ++decorated->decorations.front().base_position_millimetres.x_millimetres;
    require(elemental_scene_fingerprint({42U}, changed)
            != result.scene.elemental_visuals.fingerprint,
        "elemental cosmetic mutation did not change its visual fingerprint");
    require(result.scene.template_gameplay_fingerprint
            == template_gameplay_fingerprint(
                result.generation.topology, result.scene.template_socket_assignments),
        "cosmetic binding changed the gameplay fingerprint");
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
        {"crystal scale is player-relative", crystal_scale_is_player_relative},
        {"crystal animation and light transforms stay synchronized",
            crystal_animation_and_light_transforms_stay_synchronized},
        {"socket crystal variants are smaller", socket_variants_are_smaller},
        {"pedestals and crystals are present", pedestals_and_crystals_are_present},
        {"crystal emission and light match", crystal_emission_and_light_match},
        {"light selection reserves relevant crystal", light_selection_reserves_relevant_crystal},
        {"transparent sorting is stable", transparent_sort_is_stable},
        {"elemental budgets are enforced", elemental_budgets_are_enforced},
        {"elemental formations are grouped batched and keep-clear",
            elemental_formations_are_grouped_batched_and_keep_clear},
        {"elemental render passes are explicit", elemental_render_passes_are_explicit},
        {"structural and collision contracts remain unchanged", structural_and_collision_contracts_remain_unchanged},
        {"accepted and fallback visuals repeat", accepted_and_fallback_visuals_repeat},
        {"generated elemental scene validates", generated_elemental_scene_validates},
        {"authored Fire keeps only the collectible crystal",
            authored_fire_keeps_only_the_collectible_crystal},
        {"Water visual bindings cover every water anchor",
            water_visual_bindings_cover_every_water_anchor},
        {"authored Earth keeps only the collectible crystal",
            authored_earth_keeps_only_the_collectible_crystal},
        {"authored Air keeps one centered collectible crystal",
            authored_air_keeps_one_centered_collectible_crystal},
        {"visual pieces stay inside owning cosmetic zones",
            visual_pieces_stay_inside_owning_cosmetic_zones},
        {"elemental material budgets aggregate",
            elemental_material_budgets_aggregate},
        {"cosmetics never alter gameplay fingerprints",
            cosmetics_never_alter_gameplay_fingerprints},
    };
}

}  // namespace crystalbound::test
