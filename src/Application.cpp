#include "crystalbound/Application.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <glad/gl.h>

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat3x3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include "crystalbound/CrystalCollection.hpp"
#include "crystalbound/AuthoredChamber.hpp"
#include "crystalbound/GpuMesh.hpp"
#include "crystalbound/GpuTexture.hpp"
#include "crystalbound/ObjLoader.hpp"
#include "crystalbound/Rendering.hpp"
#include "crystalbound/ResourcePaths.hpp"
#include "crystalbound/ShaderProgram.hpp"

namespace crystalbound {
namespace {

constexpr int initial_window_width{1280};
constexpr int initial_window_height{720};
constexpr float background_red{0.08F};
constexpr float background_green{0.09F};
constexpr float background_blue{0.12F};
constexpr double profile_warmup_seconds{5.0};
constexpr std::size_t expected_maximum_profile_hertz{240U};
constexpr float millimetres_to_metres{0.001F};

struct ImportedMaterialStyle {
    MaterialKind kind{MaterialKind::untextured};
    std::array<float, 3> albedo{};
    std::array<float, 3> emission{};
};

[[nodiscard]] ImportedMaterialStyle fire_chamber_material_style(
    const MaterialMeshBatch& batch)
{
    if (batch.material_name == "M_Lava"
        || batch.material_name == "M_DeepAnimatedLava") {
        return {MaterialKind::lava, {1.0F, 0.11F, 0.008F}, {0.62F, 0.055F, 0.002F}};
    }
    if (batch.material_name == "M_Fire"
        || batch.material_name == "M_HotLavaCore"
        || batch.material_name == "M_Embers") {
        return {MaterialKind::lava, {1.0F, 0.28F, 0.012F}, {0.95F, 0.16F, 0.006F}};
    }
    if (batch.material_name == "M_Bubble") {
        return {MaterialKind::lava, {1.0F, 0.17F, 0.008F}, {0.68F, 0.075F, 0.002F}};
    }
    if (batch.material_name == "M_HotIron"
        || batch.material_name == "M_MagmaFissures") {
        return {MaterialKind::basalt_lava_crust, batch.diffuse, {0.45F, 0.11F, 0.015F}};
    }
    if (batch.material_name == "M_LavaStone"
        || batch.material_name == "M_PorousCooledBasalt") {
        return {
            MaterialKind::basalt_lava_crust,
            {0.038F, 0.031F, 0.028F},
            {0.085F, 0.018F, 0.002F}};
    }
    if (batch.material_name == "M_Stone"
        || batch.material_name == "M_GlassLikeObsidian"
        || batch.material_name == "M_DarkSocketMetal") {
        return {MaterialKind::rock, batch.diffuse, {0.0F, 0.0F, 0.0F}};
    }
    if (batch.material_name == "M_Grate") {
        return {
            MaterialKind::untextured,
            {0.20F, 0.10F, 0.055F},
            {0.16F, 0.035F, 0.004F}};
    }
    return {MaterialKind::untextured, batch.diffuse, batch.emission};
}

[[nodiscard]] ImportedMaterialStyle water_chamber_material_style(
    const MaterialMeshBatch& batch)
{
    if (batch.material_name == "M_CarraraPolished"
        || batch.material_name == "M_CarraraCeiling") {
        return {
            MaterialKind::water_marble,
            {0.82F, 0.86F, 0.88F},
            {0.0F, 0.0F, 0.0F}};
    }
    if (batch.material_name == "M_CarraraWet") {
        return {
            MaterialKind::water_marble,
            {0.63F, 0.75F, 0.79F},
            {0.0F, 0.0F, 0.0F}};
    }
    if (batch.material_name == "M_ClearStillWater") {
        return {
            MaterialKind::deep_water,
            {0.025F, 0.18F, 0.23F},
            {0.008F, 0.045F, 0.060F}};
    }
    if (batch.material_name == "M_BlueGreenInlay"
        || batch.material_name == "M_RecessMosaic") {
        return {
            MaterialKind::untextured,
            {0.025F, 0.23F, 0.25F},
            {0.010F, 0.060F, 0.070F}};
    }
    if (batch.material_name == "M_CoveGlow"
        || batch.material_name == "M_LampGlass") {
        return {
            MaterialKind::untextured,
            {0.24F, 0.64F, 0.72F},
            {0.26F, 0.78F, 0.92F}};
    }
    if (batch.material_name == "M_PassageDark") {
        return {
            MaterialKind::wet_rock,
            {0.004F, 0.006F, 0.008F},
            {0.0F, 0.0F, 0.0F}};
    }
    if (batch.material_name == "M_BlackWater") {
        return {
            MaterialKind::deep_water,
            {0.012F, 0.040F, 0.055F},
            {0.006F, 0.030F, 0.045F}};
    }
    if (batch.material_name == "M_Crystal") {
        return {
            MaterialKind::untextured,
            {0.06F, 0.22F, 0.28F},
            {0.20F, 0.78F, 1.0F}};
    }
    if (batch.material_name == "M_WetStone") {
        return {MaterialKind::water_marble, {0.58F, 0.72F, 0.80F}, {0.0F, 0.0F, 0.0F}};
    }
    if (batch.material_name == "M_Silt") {
        return {MaterialKind::soil_mineral, {0.055F, 0.060F, 0.052F}, {0.0F, 0.0F, 0.0F}};
    }
    if (batch.material_name == "M_LampGlass") {
        return {
            MaterialKind::untextured,
            {0.18F, 0.42F, 0.50F},
            {0.22F, 0.72F, 0.88F}};
    }
    if (batch.material_name == "M_Glyph") {
        return {
            MaterialKind::untextured,
            {0.24F, 0.31F, 0.32F},
            {0.035F, 0.12F, 0.15F}};
    }
    if (batch.material_name == "M_Niche") {
        return {MaterialKind::water_marble, {0.22F, 0.32F, 0.38F}, {0.0F, 0.0F, 0.0F}};
    }
    if (batch.material_name == "M_Vault") {
        return {MaterialKind::wet_rock, {0.16F, 0.18F, 0.17F}, {0.0F, 0.0F, 0.0F}};
    }
    return {MaterialKind::untextured, batch.diffuse, batch.emission};
}

[[nodiscard]] ImportedMaterialStyle earth_chamber_material_style(
    const MaterialMeshBatch& batch)
{
    if (batch.material_name == "M_FloorRock") {
        return {
            MaterialKind::soil_mineral,
            {0.27F, 0.17F, 0.075F},
            {0.0F, 0.0F, 0.0F}};
    }
    if (batch.material_name == "M_DryStackWall") {
        return {
            MaterialKind::rock,
            {0.25F, 0.19F, 0.13F},
            {0.0F, 0.0F, 0.0F}};
    }
    if (batch.material_name == "M_PathStone"
        || batch.material_name == "M_PavilionStone") {
        return {
            MaterialKind::soil_mineral,
            {0.29F, 0.21F, 0.12F},
            {0.0F, 0.0F, 0.0F}};
    }
    if (batch.material_name == "M_AgedIron") {
        return {
            MaterialKind::untextured,
            {0.075F, 0.08F, 0.065F},
            {0.0F, 0.0F, 0.0F}};
    }
    if (batch.material_name == "M_SignatureDetail") {
        return {
            MaterialKind::rock,
            {0.22F, 0.16F, 0.10F},
            {0.0F, 0.0F, 0.0F}};
    }
    if (batch.material_name == "M_SignatureGlow") {
        return {
            MaterialKind::untextured,
            {0.20F, 0.12F, 0.04F},
            {0.10F, 0.045F, 0.008F}};
    }
    if (batch.material_name == "M_StandingStone"
        || batch.material_name == "M_StandingStone.001") {
        return {
            MaterialKind::rock,
            {0.24F, 0.19F, 0.13F},
            {0.0F, 0.0F, 0.0F}};
    }
    if (batch.material_name == "M_Plinth") {
        return {
            MaterialKind::soil_mineral,
            {0.20F, 0.13F, 0.065F},
            {0.0F, 0.0F, 0.0F}};
    }
    if (batch.material_name == "M_Rune") {
        return {
            MaterialKind::untextured,
            {0.055F, 0.038F, 0.022F},
            {0.025F, 0.012F, 0.003F}};
    }
    if (batch.material_name == "M_Vein") {
        return {
            MaterialKind::soil_mineral,
            {0.34F, 0.20F, 0.07F},
            {0.12F, 0.055F, 0.008F}};
    }
    return {MaterialKind::soil_mineral, batch.diffuse, {0.0F, 0.0F, 0.0F}};
}

[[nodiscard]] ImportedMaterialStyle air_chamber_material_style(
    const MaterialMeshBatch& batch)
{
    if (batch.material_name == "M_Bark") {
        return {MaterialKind::wood_bark, {0.24F, 0.16F, 0.085F}, {0.0F, 0.0F, 0.0F}};
    }
    if (batch.material_name == "M_Leaf"
        || batch.material_name == "M_Moss") {
        return {MaterialKind::untextured, batch.diffuse, {0.0F, 0.0F, 0.0F}};
    }
    if (batch.material_name == "M_Cloud") {
        return {
            MaterialKind::untextured,
            {0.82F, 0.90F, 0.96F},
            {0.035F, 0.055F, 0.075F}};
    }
    if (batch.material_name == "M_Inlay") {
        return {
            MaterialKind::untextured,
            {0.16F, 0.22F, 0.24F},
            {0.018F, 0.055F, 0.065F}};
    }
    if (batch.material_name == "M_RoofTeal") {
        return {
            MaterialKind::untextured,
            {0.035F, 0.24F, 0.29F},
            {0.008F, 0.040F, 0.048F}};
    }
    if (batch.material_name == "M_TrimGold") {
        return {
            MaterialKind::untextured,
            {0.62F, 0.43F, 0.12F},
            {0.055F, 0.030F, 0.004F}};
    }
    if (batch.material_name == "M_TunnelDark") {
        return {MaterialKind::rock, {0.012F, 0.014F, 0.016F}, {0.0F, 0.0F, 0.0F}};
    }
    if (batch.material_name == "M_Shard"
        || batch.material_name == "M_DeadShard") {
        return {MaterialKind::rock, batch.diffuse, {0.0F, 0.0F, 0.0F}};
    }
    return {MaterialKind::rock, batch.diffuse, batch.emission};
}

[[nodiscard]] ImportedMaterialStyle authored_mtl_material_style(
    const MaterialMeshBatch& batch)
{
    return {MaterialKind::untextured, batch.diffuse, batch.emission};
}

[[nodiscard]] MaterialModelLoadResult load_water_chamber_asset(
    const std::filesystem::path& resource_directory)
{
    return load_water_chamber_render_asset(
        resource_directory / "assets" / "models" / "WaterChamber.obj");
}

[[nodiscard]] MaterialModelLoadResult load_fire_chamber_asset(
    const std::filesystem::path& resource_directory)
{
    return load_obj_material_batches(
        resource_directory / "assets" / "models" / "FireChamber.obj");
}

[[nodiscard]] MaterialModelLoadResult load_aether_chamber_asset(
    const std::filesystem::path& resource_directory)
{
    return load_obj_material_batches(
        resource_directory / "assets" / "models" / "AetherChamber.obj");
}

[[nodiscard]] MaterialModelLoadResult load_start_chamber_asset(
    const std::filesystem::path& resource_directory)
{
    return load_obj_material_batches(
        resource_directory / "assets" / "models" / "StartChamber.obj");
}

[[nodiscard]] MaterialModelLoadResult load_exit_chamber_asset(
    const std::filesystem::path& resource_directory)
{
    return load_obj_material_batches(
        resource_directory / "assets" / "models" / "ExitChamber.obj",
        {exit_render_excluded_object_names()});
}

[[nodiscard]] MaterialModelLoadResult load_earth_chamber_asset(
    const std::filesystem::path& resource_directory)
{
    return load_obj_material_batches(
        resource_directory / "assets" / "models" / "EarthChamber.obj");
}

[[nodiscard]] MaterialModelLoadResult load_air_chamber_asset(
    const std::filesystem::path& resource_directory)
{
    return load_obj_material_batches(
        resource_directory / "assets" / "models" / "AirChamber.obj",
        {air_render_excluded_object_names()});
}

[[nodiscard]] MaterialModelLoadResult load_elemental_crystal_asset(
    const std::filesystem::path& resource_directory)
{
    return load_obj_material_batches(
        resource_directory / "assets" / "models" / "ElementalCrystal.obj",
        {{"Plane.001", "SpaceBackground", "Cylinder.001"}});
}

[[nodiscard]] glm::mat4 elemental_crystal_local_model(
    const MaterialModelObject& source,
    const std::int32_t target_height_millimetres)
{
    const float source_height{
        source.maximum_bounds[1] - source.minimum_bounds[1]};
    if (!std::isfinite(source_height) || source_height <= 0.0F
        || target_height_millimetres <= 0) {
        throw std::runtime_error(
            "The authored elemental collectible has invalid height bounds.");
    }
    const float scale{
        static_cast<float>(target_height_millimetres)
        * millimetres_to_metres / source_height};
    const float source_center_x{
        (source.minimum_bounds[0] + source.maximum_bounds[0]) * 0.5F};
    const float source_center_z{
        (source.minimum_bounds[2] + source.maximum_bounds[2]) * 0.5F};
    glm::mat4 model{1.0F};
    model = glm::scale(model, glm::vec3{scale});
    return glm::translate(model, {
        -source_center_x, -source.minimum_bounds[1], -source_center_z});
}

[[nodiscard]] std::string format_elapsed_time(const double elapsed_seconds)
{
    const double safe_seconds{std::max(0.0, elapsed_seconds)};
    const auto total_centiseconds{static_cast<std::uint64_t>(safe_seconds * 100.0)};
    const std::uint64_t minutes{total_centiseconds / 6'000U};
    const std::uint64_t seconds{(total_centiseconds / 100U) % 60U};
    const std::uint64_t centiseconds{total_centiseconds % 100U};
    std::ostringstream formatted;
    formatted << std::setfill('0') << std::setw(2) << minutes << ':'
              << std::setw(2) << seconds << '.' << std::setw(2) << centiseconds;
    return formatted.str();
}

[[nodiscard]] const char* opengl_string(const unsigned int name)
{
    const auto* value = glGetString(name);
    return value == nullptr ? "<unavailable>" : reinterpret_cast<const char*>(value);
}

[[nodiscard]] Application* application_for(GLFWwindow* window)
{
    return static_cast<Application*>(glfwGetWindowUserPointer(window));
}

[[nodiscard]] bool key_is_down(GLFWwindow* window, const int key)
{
    const int state = glfwGetKey(window, key);
    return state == GLFW_PRESS || state == GLFW_REPEAT;
}

void apply_render_pass_state(const RenderPassState& state)
{
    validate_render_pass_state(state);
    (state.framebuffer_srgb ? glEnable : glDisable)(GL_FRAMEBUFFER_SRGB);
    (state.depth_test ? glEnable : glDisable)(GL_DEPTH_TEST);
    glDepthMask(state.depth_write ? GL_TRUE : GL_FALSE);
    if (state.cull_mode == CullMode::back) {
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glFrontFace(GL_CCW);
    } else {
        glDisable(GL_CULL_FACE);
    }
    if (state.blend_mode == BlendMode::straight_alpha) {
        glEnable(GL_BLEND);
        glBlendEquation(GL_FUNC_ADD);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    } else if (state.blend_mode == BlendMode::premultiplied_alpha) {
        glEnable(GL_BLEND);
        glBlendEquation(GL_FUNC_ADD);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    } else if (state.blend_mode == BlendMode::additive) {
        glEnable(GL_BLEND);
        glBlendEquation(GL_FUNC_ADD);
        glBlendFunc(GL_ONE, GL_ONE);
    } else {
        glDisable(GL_BLEND);
    }
}

#if !defined(NDEBUG)
void require_no_opengl_error(const char* operation)
{
    const unsigned int error = glGetError();
    if (error != GL_NO_ERROR) {
        throw std::runtime_error(
            std::string{"OpenGL error after "} + operation + ": " + std::to_string(error));
    }
}
#endif

}  // namespace

class Application::RenderResources {
public:
    RenderResources(
        const std::filesystem::path& resource_directory,
        const CaveSceneData& scene,
        const MaterialModelLoadResult& authored_fire,
        const MaterialModelLoadResult& authored_water,
        const MaterialModelLoadResult& authored_earth,
        const MaterialModelLoadResult& authored_air,
        const MaterialModelLoadResult& authored_aether,
        const MaterialModelLoadResult& authored_start,
        const MaterialModelLoadResult& authored_exit,
        const ExitArchData& exit_arch,
        const Seed effective_seed)
        : shader_program_(
              resource_directory / "shaders" / "cave_phong.vert",
              resource_directory / "shaders" / "cave_phong.frag"),
          rock_texture_(generate_rock_texture(effective_seed, rock_texture_stable_id)),
          wood_texture_(generate_wood_texture(effective_seed, wood_texture_stable_id))
    {
        elemental_visuals_ = scene.elemental_visuals;
        exit_arch_ = exit_arch;
        const AuthoredChamberPlacement water_placement{
            water_chamber_placement(scene)};
        const AuthoredChamberPlacement fire_placement{
            fire_chamber_placement(scene)};
        const AuthoredChamberPlacement earth_placement{
            earth_chamber_placement(scene)};
        const AuthoredChamberPlacement air_placement{
            air_chamber_placement(scene)};
        const AuthoredChamberPlacement aether_placement{
            aether_chamber_placement(scene)};
        const AuthoredChamberPlacement start_placement{
            start_chamber_placement(scene)};
        const AuthoredChamberPlacement exit_placement{
            exit_chamber_placement(scene)};
        pieces_.reserve(scene.mesh_pieces.size());
        for (const SceneMeshPiece& piece : scene.mesh_pieces) {
            const bool replaced_by_authored_fire{
                piece.owner_chamber_id == std::optional<NodeId>{
                    fire_placement.chamber_id}};
            const bool replaced_by_authored_water{
                piece.owner_chamber_id == std::optional<NodeId>{
                    water_placement.chamber_id}};
            const bool replaced_by_authored_earth{
                piece.owner_chamber_id == std::optional<NodeId>{
                    earth_placement.chamber_id}};
            const bool replaced_by_authored_air{
                piece.owner_chamber_id == std::optional<NodeId>{
                    air_placement.chamber_id}};
            const bool replaced_by_authored_aether{
                piece.owner_chamber_id == std::optional<NodeId>{
                    aether_placement.chamber_id}};
            const bool replaced_by_authored_start{
                piece.owner_chamber_id == std::optional<NodeId>{
                    start_placement.chamber_id}};
            const bool replaced_by_authored_exit{
                piece.owner_chamber_id == std::optional<NodeId>{
                    exit_placement.chamber_id}};
            if (replaced_by_authored_fire || replaced_by_authored_water || replaced_by_authored_earth
                || replaced_by_authored_air || replaced_by_authored_aether
                || replaced_by_authored_start || replaced_by_authored_exit) {
                continue;
            }
            pieces_.push_back({piece.material, piece.albedo,
                std::make_unique<GpuMesh>(piece.mesh)});
        }
        glm::mat4 fire_model{1.0F};
        fire_model = glm::translate(fire_model, {
            static_cast<float>(fire_placement.translation_metres.x),
            static_cast<float>(fire_placement.translation_metres.y),
            static_cast<float>(fire_placement.translation_metres.z),
        });
        fire_model = glm::rotate(
            fire_model,
            static_cast<float>(fire_placement.yaw_radians),
            {0.0F, 1.0F, 0.0F});
        fire_model = glm::scale(fire_model, {
            static_cast<float>(fire_placement.scale.x),
            static_cast<float>(fire_placement.scale.y),
            static_cast<float>(fire_placement.scale.z),
        });
        imported_fire_model_ = fire_model;
        imported_fire_pieces_.reserve(authored_fire.batches.size());
        std::size_t authored_vertex_count{};
        std::size_t authored_triangle_count{};
        for (const MaterialMeshBatch& batch : authored_fire.batches) {
            const ImportedMaterialStyle style{fire_chamber_material_style(batch)};
            authored_vertex_count += batch.mesh.vertices.size();
            authored_triangle_count += batch.mesh.indices.size() / 3U;
            imported_fire_pieces_.push_back({
                style.kind,
                style.albedo,
                style.emission,
                std::make_unique<GpuMesh>(batch.mesh),
            });
        }
        glm::mat4 water_model{1.0F};
        water_model = glm::translate(water_model, {
            static_cast<float>(water_placement.translation_metres.x),
            static_cast<float>(water_placement.translation_metres.y),
            static_cast<float>(water_placement.translation_metres.z),
        });
        water_model = glm::rotate(
            water_model,
            static_cast<float>(water_placement.yaw_radians),
            {0.0F, 1.0F, 0.0F});
        water_model = glm::scale(water_model, {
            static_cast<float>(water_placement.scale.x),
            static_cast<float>(water_placement.scale.y),
            static_cast<float>(water_placement.scale.z),
        });
        imported_water_model_ = water_model;
        imported_water_pieces_.reserve(authored_water.batches.size());
        std::size_t authored_water_vertex_count{};
        std::size_t authored_water_triangle_count{};
        for (const MaterialMeshBatch& batch : authored_water.batches) {
            const ImportedMaterialStyle style{
                water_chamber_material_style(batch)};
            authored_water_vertex_count += batch.mesh.vertices.size();
            authored_water_triangle_count += batch.mesh.indices.size() / 3U;
            imported_water_pieces_.push_back({
                style.kind,
                style.albedo,
                style.emission,
                std::make_unique<GpuMesh>(batch.mesh),
            });
        }
        glm::mat4 earth_model{1.0F};
        earth_model = glm::translate(earth_model, {
            static_cast<float>(earth_placement.translation_metres.x),
            static_cast<float>(earth_placement.translation_metres.y),
            static_cast<float>(earth_placement.translation_metres.z),
        });
        earth_model = glm::rotate(
            earth_model,
            static_cast<float>(earth_placement.yaw_radians),
            {0.0F, 1.0F, 0.0F});
        imported_earth_model_ = earth_model;
        imported_earth_pieces_.reserve(authored_earth.batches.size());
        std::size_t authored_earth_vertex_count{};
        std::size_t authored_earth_triangle_count{};
        for (const MaterialMeshBatch& batch : authored_earth.batches) {
            const ImportedMaterialStyle style{
                earth_chamber_material_style(batch)};
            authored_earth_vertex_count += batch.mesh.vertices.size();
            authored_earth_triangle_count += batch.mesh.indices.size() / 3U;
            imported_earth_pieces_.push_back({
                style.kind,
                style.albedo,
                style.emission,
                std::make_unique<GpuMesh>(batch.mesh),
            });
        }
        glm::mat4 air_model{1.0F};
        air_model = glm::translate(air_model, {
            static_cast<float>(air_placement.translation_metres.x),
            static_cast<float>(air_placement.translation_metres.y),
            static_cast<float>(air_placement.translation_metres.z),
        });
        air_model = glm::rotate(
            air_model,
            static_cast<float>(air_placement.yaw_radians),
            {0.0F, 1.0F, 0.0F});
        imported_air_model_ = air_model;
        imported_air_pieces_.reserve(authored_air.batches.size());
        std::size_t authored_air_vertex_count{};
        std::size_t authored_air_triangle_count{};
        for (const MaterialMeshBatch& batch : authored_air.batches) {
            const ImportedMaterialStyle style{
                air_chamber_material_style(batch)};
            authored_air_vertex_count += batch.mesh.vertices.size();
            authored_air_triangle_count += batch.mesh.indices.size() / 3U;
            imported_air_pieces_.push_back({
                style.kind,
                style.albedo,
                style.emission,
                std::make_unique<GpuMesh>(batch.mesh),
            });
        }
        const auto authored_model = [](const AuthoredChamberPlacement& placement) {
            glm::mat4 model{1.0F};
            model = glm::translate(model, {
                static_cast<float>(placement.translation_metres.x),
                static_cast<float>(placement.translation_metres.y),
                static_cast<float>(placement.translation_metres.z),
            });
            model = glm::rotate(model,
                static_cast<float>(placement.yaw_radians), {0.0F, 1.0F, 0.0F});
            return glm::scale(model, {
                static_cast<float>(placement.scale.x),
                static_cast<float>(placement.scale.y),
                static_cast<float>(placement.scale.z),
            });
        };
        const auto upload_authored = [](const MaterialModelLoadResult& source,
                                         std::vector<ImportedRenderPiece>& destination) {
            destination.reserve(source.batches.size());
            for (const MaterialMeshBatch& batch : source.batches) {
                const ImportedMaterialStyle style{
                    authored_mtl_material_style(batch)};
                destination.push_back({style.kind, style.albedo, style.emission,
                    std::make_unique<GpuMesh>(batch.mesh)});
            }
        };
        imported_aether_model_ = authored_model(aether_placement);
        upload_authored(authored_aether, imported_aether_pieces_);
        imported_start_model_ = authored_model(start_placement);
        upload_authored(authored_start, imported_start_pieces_);
        imported_exit_model_ = authored_model(exit_placement);
        upload_authored(authored_exit, imported_exit_pieces_);
        const MaterialModelLoadResult authored_elemental_crystal{
            load_elemental_crystal_asset(resource_directory)};
        if (authored_elemental_crystal.batches.size() != 1U) {
            throw std::runtime_error(
                "The authored elemental collectible must contain one visible material batch.");
        }
        const auto elemental_crystal_source{std::find_if(
            authored_elemental_crystal.objects.begin(),
            authored_elemental_crystal.objects.end(),
            [](const MaterialModelObject& object) {
                return object.name == "Cylinder.002";
            })};
        if (elemental_crystal_source
            == authored_elemental_crystal.objects.end()) {
            throw std::runtime_error(
                "The authored elemental collectible has no Cylinder.002 source object.");
        }
        elemental_pieces_.reserve(
            scene.elemental_visuals.opaque_draw_call_count
            + scene.elemental_visuals.transparent_effect_draw_count);
        for (const ElementalChamberVisual& chamber : elemental_visuals_.chambers) {
            const glm::mat4 crystal_local_model{
                elemental_crystal_local_model(
                    *elemental_crystal_source,
                    chamber.crystal_shape.height_millimetres)};
            elemental_pieces_.push_back({
                &chamber.pedestal,
                std::make_unique<GpuMesh>(chamber.pedestal.mesh),
                glm::mat4{1.0F},
                chamber.element == Element::fire
                    || chamber.element == Element::water
                    || chamber.element == Element::earth
                    || chamber.element == Element::air
                    || chamber.element == Element::aether,
            });
            elemental_pieces_.push_back({
                &chamber.crystal,
                std::make_unique<GpuMesh>(
                    authored_elemental_crystal.batches.front().mesh),
                crystal_local_model,
                false,
            });
            for (const ElementalVisualPiece& decoration : chamber.decorations) {
                elemental_pieces_.push_back({
                    &decoration,
                    std::make_unique<GpuMesh>(decoration.mesh),
                    glm::mat4{1.0F},
                    true,
                });
            }
        }
        exit_portal_mesh_ = std::make_unique<GpuMesh>(exit_arch_.portal_mesh);
        exit_socket_meshes_.reserve(exit_arch_.sockets.size());
        for (const ExitSocketContract& socket : exit_arch_.sockets) {
            exit_socket_meshes_.push_back(
                std::make_unique<GpuMesh>(socket.crystal_mesh));
        }
        shader_program_.use();
        shader_program_.set_integer("u_rock_texture", 0);
        shader_program_.set_integer("u_wood_texture", 1);
        if (effective_seed.value == 42U
            && (rock_texture_.fingerprint() != rock_texture_golden_fingerprint_seed_42
                || wood_texture_.fingerprint() != wood_texture_golden_fingerprint_seed_42)) {
            throw std::runtime_error("Seed 42 procedural texture fingerprints do not match the locked contract.");
        }
        std::cout << "Procedural materials uploaded once\n"
                  << "  Rock texture: " << format_fingerprint(rock_texture_.fingerprint()) << '\n'
                  << "  Wood texture: " << format_fingerprint(wood_texture_.fingerprint()) << '\n'
                  << "  Elemental meshes: " << elemental_pieces_.size() << '\n'
                  << "  Elemental fingerprint: "
                  << format_fingerprint(scene.elemental_visuals.fingerprint) << '\n'
                  << "  Exit arch fingerprint: "
                  << format_fingerprint(exit_arch_.fingerprint) << '\n'
                  << "Authored Fire chamber uploaded\n"
                  << "  Material batches: " << imported_fire_pieces_.size() << '\n'
                  << "  Vertices: " << authored_vertex_count << '\n'
                  << "  Triangles: " << authored_triangle_count << '\n'
                  << "  Scale: 1:1 authored metres\n"
                  << "Authored Water chamber uploaded\n"
                  << "  Material batches: " << imported_water_pieces_.size() << '\n'
                  << "  Objects: " << authored_water.objects.size() << '\n'
                  << "  Vertices: " << authored_water_vertex_count << '\n'
                  << "  Triangles: " << authored_water_triangle_count << '\n'
                  << "  Authored scale: 1\n"
                  << "Authored Earth chamber uploaded\n"
                  << "  Material batches: " << imported_earth_pieces_.size() << '\n'
                  << "  Objects: " << authored_earth.objects.size() << '\n'
                  << "  Vertices: " << authored_earth_vertex_count << '\n'
                  << "  Triangles: " << authored_earth_triangle_count << '\n'
                  << "  Authored scale: 1\n"
                  << "Authored Air chamber uploaded\n"
                  << "  Material batches: " << imported_air_pieces_.size() << '\n'
                  << "  Objects: " << authored_air.objects.size() << '\n'
                  << "  Vertices: " << authored_air_vertex_count << '\n'
                  << "  Triangles: " << authored_air_triangle_count << '\n'
                  << "  Authored scale: 1\n";
    }

    void render(
        const glm::mat4& view,
        const glm::mat4& projection,
        const glm::vec3& camera_position,
        const NodeId stable_zone_id,
        const float elapsed_seconds,
        const CrystalCollectionState& collection)
    {
        const PointLight lantern{camera_lantern(
            {camera_position.x, camera_position.y, camera_position.z})};
        std::vector<StableLightCandidate> candidates{
            active_crystal_lights(
                elemental_visuals_, collection, stable_zone_id, elapsed_seconds)};
        if (collection.all_collected()) {
            const GeometryVector3& position{exit_arch_.interaction_position_metres};
            candidates.push_back({
                {exit_arch_.stable_object_id ^ 0x4C49474854ULL,
                    PointLightRole::decorative,
                    {static_cast<float>(position.x), static_cast<float>(position.y),
                        static_cast<float>(position.z)},
                    {0.72F, 0.24F, 1.0F}, 3.2F, 1.0F, 0.16F, 0.18F, 7.5F},
                stable_zone_id == exit_arch_.chamber_id});
        }
        const std::vector<PointLight> lights{select_point_lights(lantern, candidates)};
        const FogParameters fog{fog_parameters_for_zone(stable_zone_id.value)};

        shader_program_.use();
        rock_texture_.bind(0U);
        wood_texture_.bind(1U);
        shader_program_.set_matrix("u_view", view);
        shader_program_.set_matrix("u_projection", projection);
        shader_program_.set_vector("u_camera_position", camera_position);
        shader_program_.set_integer(
            "u_point_light_count", static_cast<int>(lights.size()));
        for (std::size_t index{}; index < lights.size(); ++index) {
            const PointLight& light{lights[index]};
            const std::string prefix{
                "u_point_lights[" + std::to_string(index) + "]."};
            shader_program_.set_vector(prefix + "position", glm::vec3{
                light.position_metres[0], light.position_metres[1],
                light.position_metres[2]});
            shader_program_.set_vector(prefix + "color", glm::vec3{
                light.color_linear[0], light.color_linear[1], light.color_linear[2]});
            shader_program_.set_float(prefix + "intensity", light.intensity);
            shader_program_.set_float(
                prefix + "attenuation_constant", light.attenuation_constant);
            shader_program_.set_float(
                prefix + "attenuation_linear", light.attenuation_linear);
            shader_program_.set_float(
                prefix + "attenuation_quadratic", light.attenuation_quadratic);
            shader_program_.set_float(prefix + "range_metres", light.range_metres);
        }
        shader_program_.set_vector("u_fog_color", glm::vec3{
            fog.color_linear[0], fog.color_linear[1], fog.color_linear[2]});
        shader_program_.set_float("u_fog_start", fog.start_distance_metres);
        shader_program_.set_float("u_fog_end", fog.end_distance_metres);
        validate_visual_effect_time(elapsed_seconds);
        shader_program_.set_float("u_time_seconds", elapsed_seconds);

        apply_render_pass_state(opaque_render_pass);
        set_model(glm::mat4{1.0F});
        for (const RenderPiece& piece : pieces_) {
            set_material(piece.material, piece.albedo, {0.0F, 0.0F, 0.0F}, 1.0F);
            piece.mesh->draw();
        }
        set_model(imported_fire_model_);
        for (const ImportedRenderPiece& piece : imported_fire_pieces_) {
            set_material(piece.material, piece.albedo, piece.emission, 1.0F);
            piece.mesh->draw();
        }
        set_model(imported_water_model_);
        for (const ImportedRenderPiece& piece : imported_water_pieces_) {
            set_material(piece.material, piece.albedo, piece.emission, 1.0F);
            piece.mesh->draw();
        }
        set_model(imported_earth_model_);
        for (const ImportedRenderPiece& piece : imported_earth_pieces_) {
            set_material(piece.material, piece.albedo, piece.emission, 1.0F);
            piece.mesh->draw();
        }
        set_model(imported_air_model_);
        for (const ImportedRenderPiece& piece : imported_air_pieces_) {
            set_material(piece.material, piece.albedo, piece.emission, 1.0F);
            piece.mesh->draw();
        }
        set_model(imported_aether_model_);
        for (const ImportedRenderPiece& piece : imported_aether_pieces_) {
            set_material(piece.material, piece.albedo, piece.emission, 1.0F);
            piece.mesh->draw();
        }
        set_model(imported_start_model_);
        for (const ImportedRenderPiece& piece : imported_start_pieces_) {
            set_material(piece.material, piece.albedo, piece.emission, 1.0F);
            piece.mesh->draw();
        }
        set_model(imported_exit_model_);
        for (const ImportedRenderPiece& piece : imported_exit_pieces_) {
            set_material(piece.material, piece.albedo, piece.emission, 1.0F);
            piece.mesh->draw();
        }

        render_elemental_layer(ElementalRenderLayer::opaque, opaque_render_pass,
            elapsed_seconds, collection);
        render_elemental_layer(ElementalRenderLayer::emissive, emissive_render_pass,
            elapsed_seconds, collection);
        render_exit_arch(elapsed_seconds, collection);

        apply_render_pass_state(transparent_effect_render_pass);
        const std::vector<std::size_t> transparent_indices{
            sorted_transparent_piece_indices(elemental_visuals_,
                {camera_position.x, camera_position.y, camera_position.z})};
        for (const std::size_t index : transparent_indices) {
            if (index >= elemental_pieces_.size()
                || elemental_pieces_[index].contract->layer
                    != ElementalRenderLayer::transparent) {
                throw std::runtime_error(
                    "Transparent elemental ordering disagrees with uploaded meshes.");
            }
            draw_elemental(elemental_pieces_[index], elapsed_seconds);
        }
        render_elemental_layer(ElementalRenderLayer::additive,
            additive_effect_render_pass, elapsed_seconds, collection);
#if !defined(NDEBUG)
        if (!first_frame_validated_) {
            require_no_opengl_error("the first elemental cave draw");
            first_frame_validated_ = true;
        }
#endif
    }

private:
    struct RenderPiece {
        MaterialKind material{MaterialKind::rock};
        std::array<float, 3> albedo{};
        std::unique_ptr<GpuMesh> mesh{};
    };

    struct ElementalRenderPiece {
        const ElementalVisualPiece* contract{};
        std::unique_ptr<GpuMesh> mesh{};
        glm::mat4 local_model{1.0F};
        bool suppressed{};
    };

    struct ImportedRenderPiece {
        MaterialKind material{MaterialKind::untextured};
        std::array<float, 3> albedo{};
        std::array<float, 3> emission{};
        std::unique_ptr<GpuMesh> mesh{};
    };

    void set_model(const glm::mat4& model)
    {
        shader_program_.set_matrix("u_model", model);
        shader_program_.set_matrix(
            "u_normal_matrix", glm::inverseTranspose(glm::mat3{model}));
    }

    void set_material(
        const MaterialKind kind,
        const std::array<float, 3>& albedo,
        const std::array<float, 3>& emission,
        const float alpha)
    {
        shader_program_.set_vector("u_albedo", {albedo[0], albedo[1], albedo[2]});
        shader_program_.set_vector(
            "u_material_emission", {emission[0], emission[1], emission[2]});
        shader_program_.set_float("u_alpha", alpha);
        if (!bound_material_kind_.has_value() || *bound_material_kind_ != kind) {
            const MaterialParameters material{material_parameters(kind)};
            validate_material_parameters(material);
            shader_program_.set_integer("u_material_kind", static_cast<int>(kind));
            shader_program_.set_vector("u_material_ambient", glm::vec3{
                material.ambient[0], material.ambient[1], material.ambient[2]});
            shader_program_.set_vector("u_material_diffuse", glm::vec3{
                material.diffuse[0], material.diffuse[1], material.diffuse[2]});
            shader_program_.set_vector("u_material_specular", glm::vec3{
                material.specular[0], material.specular[1], material.specular[2]});
            shader_program_.set_float("u_material_shininess", material.shininess);
            shader_program_.set_float("u_texture_scale", material.texture_scale);
            shader_program_.set_float(
                "u_triplanar_sharpness", material.triplanar_sharpness);
            bound_material_kind_ = kind;
        }
    }

    void draw_elemental(
        const ElementalRenderPiece& render_piece,
        const float elapsed_seconds)
    {
        if (render_piece.suppressed) {
            return;
        }
        const ElementalVisualPiece& piece{*render_piece.contract};
        const ElementalTransformSample transform{
            sample_elemental_transform(piece, elapsed_seconds)};
        glm::mat4 model{1.0F};
        model = glm::translate(model, {
            static_cast<float>(transform.position_metres.x),
            static_cast<float>(transform.position_metres.y),
            static_cast<float>(transform.position_metres.z)});
        model = glm::rotate(model, transform.rotation_y_radians, {0.0F, 1.0F, 0.0F});
        model = glm::scale(model, glm::vec3{transform.uniform_scale});
        model *= render_piece.local_model;
        set_model(model);
        const std::array<float, 3> albedo{linear_color(piece.albedo)};
        std::array<float, 3> emission{linear_color(piece.emission)};
        for (float& component : emission) {
            component *= transform.emission_multiplier;
        }
        set_material(piece.material, albedo, emission,
            static_cast<float>(piece.alpha_milli) / 1'000.0F);
        render_piece.mesh->draw();
    }

    void render_elemental_layer(
        const ElementalRenderLayer layer,
        const RenderPassState& state,
        const float elapsed_seconds,
        const CrystalCollectionState& collection)
    {
        apply_render_pass_state(state);
        for (const ElementalRenderPiece& piece : elemental_pieces_) {
            if (piece.contract->layer == layer
                && is_elemental_piece_visible(*piece.contract, collection)) {
                draw_elemental(piece, elapsed_seconds);
            }
        }
    }

    void render_exit_arch(
        const float elapsed_seconds,
        const CrystalCollectionState& collection)
    {
        apply_render_pass_state(emissive_render_pass);
        const ExitArchDisplayState display{
            exit_arch_display_state(exit_arch_, collection)};
        for (std::size_t index{}; index < exit_arch_.sockets.size(); ++index) {
            const ExitSocketContract& socket{exit_arch_.sockets[index]};
            if (!display.filled.displays(socket.element)) {
                continue;
            }
            const ElementalAnimationSample animation{
                sample_elemental_animation(socket.animation, elapsed_seconds)};
            glm::mat4 model{1.0F};
            model = glm::translate(model, {
                static_cast<float>(socket.position_metres.x),
                static_cast<float>(socket.position_metres.y)
                    + animation.vertical_offset_metres * 0.18F,
                static_cast<float>(socket.position_metres.z)});
            model = glm::scale(model, glm::vec3{animation.scale_multiplier});
            set_model(model);
            const std::array<float, 3> albedo{linear_color(socket.albedo)};
            std::array<float, 3> emission{linear_color(socket.emission)};
            for (float& component : emission) {
                component *= animation.emission_multiplier;
            }
            set_material(MaterialKind::untextured, albedo, emission, 1.0F);
            exit_socket_meshes_[index]->draw();
        }
        if (display.active) {
            set_model(glm::mat4{1.0F});
            set_material(MaterialKind::untextured, {0.18F, 0.055F, 0.28F},
                {0.95F, 0.32F, 1.15F}, 1.0F);
            exit_portal_mesh_->draw();
        }
    }

    ShaderProgram shader_program_;
    GpuTexture rock_texture_;
    GpuTexture wood_texture_;
    std::vector<RenderPiece> pieces_{};
    glm::mat4 imported_fire_model_{1.0F};
    std::vector<ImportedRenderPiece> imported_fire_pieces_{};
    glm::mat4 imported_water_model_{1.0F};
    std::vector<ImportedRenderPiece> imported_water_pieces_{};
    glm::mat4 imported_earth_model_{1.0F};
    std::vector<ImportedRenderPiece> imported_earth_pieces_{};
    glm::mat4 imported_air_model_{1.0F};
    std::vector<ImportedRenderPiece> imported_air_pieces_{};
    glm::mat4 imported_aether_model_{1.0F};
    std::vector<ImportedRenderPiece> imported_aether_pieces_{};
    glm::mat4 imported_start_model_{1.0F};
    std::vector<ImportedRenderPiece> imported_start_pieces_{};
    glm::mat4 imported_exit_model_{1.0F};
    std::vector<ImportedRenderPiece> imported_exit_pieces_{};
    ElementalSceneData elemental_visuals_{};
    std::vector<ElementalRenderPiece> elemental_pieces_{};
    ExitArchData exit_arch_{};
    std::unique_ptr<GpuMesh> exit_portal_mesh_{};
    std::vector<std::unique_ptr<GpuMesh>> exit_socket_meshes_{};
    std::optional<MaterialKind> bound_material_kind_{};
#if !defined(NDEBUG)
    bool first_frame_validated_{};
#endif
};

Application::Application(
    CaveGenerationResult generation,
    SeedSource seed_source,
    const std::optional<std::uint32_t> profile_seconds,
    const bool profile_vsync,
    const bool automated_profile_traversal)
    : generation_(std::move(generation)),
      seed_source_(std::move(seed_source)),
      profile_seconds_(profile_seconds),
      profile_vsync_(profile_vsync)
{
    if (!seed_source_) {
        throw std::invalid_argument("Application requires a New Cave seed source.");
    }
    if (profile_seconds_.has_value()) {
        frame_profiler_.emplace(
            profile_warmup_seconds,
            static_cast<double>(*profile_seconds_),
            static_cast<std::size_t>(*profile_seconds_)
                * expected_maximum_profile_hertz);
    }
    if (automated_profile_traversal) {
        if (!profile_seconds_.has_value()) {
            throw std::invalid_argument(
                "Automated profile traversal requires a measured duration.");
        }
        profile_traversal_ = build_profile_traversal_workload(generation_);
        profile_waypoint_index_ = profile_traversal_->waypoints_metres.size() > 1U
            ? 1U : 0U;
    }
}

Application::~Application()
{
    shutdown();
}

int Application::run()
{
    initialize();
    run_frame_loop();
    shutdown();
    return 0;
}

void Application::initialize()
{
    const std::filesystem::path resources = resource_root();
    const MaterialModelLoadResult authored_fire{
        load_fire_chamber_asset(resources)};
    const MaterialModelLoadResult authored_water{
        load_water_chamber_asset(resources)};
    const MaterialModelLoadResult authored_earth{
        load_earth_chamber_asset(resources)};
    const MaterialModelLoadResult authored_air{
        load_air_chamber_asset(resources)};
    const MaterialModelLoadResult authored_aether{
        load_aether_chamber_asset(resources)};
    const MaterialModelLoadResult authored_start{
        load_start_chamber_asset(resources)};
    const MaterialModelLoadResult authored_exit{
        load_exit_chamber_asset(resources)};
    const GeometryVector3& position{generation_.scene.start_camera_position_metres};
    const GeometryVector3& forward{generation_.scene.start_camera_forward};
    camera_.set_pose(
        {static_cast<float>(position.x), static_cast<float>(position.y),
         static_cast<float>(position.z)},
        {static_cast<float>(forward.x), static_cast<float>(forward.y),
         static_cast<float>(forward.z)});
    CollisionWorld collision_world{build_collision_world(generation_.scene)};
    append_authored_chamber_collision(
        collision_world,
        build_fire_chamber_collision(
            authored_fire, fire_chamber_placement(generation_.scene)));
    append_authored_chamber_collision(
        collision_world,
        build_water_chamber_collision(
            authored_water, water_chamber_placement(generation_.scene)));
    append_authored_chamber_collision(
        collision_world,
        build_earth_chamber_collision(
            authored_earth, earth_chamber_placement(generation_.scene)));
    append_authored_chamber_collision(
        collision_world,
        build_air_chamber_collision(
            authored_air, air_chamber_placement(generation_.scene)));
    append_authored_chamber_collision(collision_world,
        build_aether_chamber_collision(
            authored_aether, aether_chamber_placement(generation_.scene)));
    append_authored_chamber_collision(collision_world,
        build_start_chamber_collision(
            authored_start, start_chamber_placement(generation_.scene)));
    append_authored_chamber_collision(collision_world,
        build_exit_chamber_collision(
            authored_exit, exit_chamber_placement(generation_.scene)));
    controller_ = std::make_unique<GroundedController>(
        std::move(collision_world), find_start_spawn(generation_));
    interaction_targets_ = build_crystal_interaction_targets(
        generation_.scene.elemental_visuals);
    visibility_world_ = build_crystal_visibility_world(generation_.scene);
    exit_arch_ = build_exit_arch(generation_);
    const GeometryVector3 camera_position{controller_->camera_position_metres()};
    camera_.set_position({
        static_cast<float>(camera_position.x),
        static_cast<float>(camera_position.y),
        static_cast<float>(camera_position.z),
    });

    initialize_window();
    initialize_opengl();

    glfwSetWindowUserPointer(window_, this);
    glfwSetFramebufferSizeCallback(window_, framebuffer_size_callback);
    glfwSetCursorPosCallback(window_, cursor_position_callback);
    glfwSetKeyCallback(window_, key_callback);
    glfwSetWindowFocusCallback(window_, window_focus_callback);
    glfwSetWindowIconifyCallback(window_, window_iconify_callback);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    if (!ImGui_ImplGlfw_InitForOpenGL(window_, true)) {
        ImGui::DestroyContext();
        throw std::runtime_error("Dear ImGui GLFW initialization failed.");
    }
    if (!ImGui_ImplOpenGL3_Init("#version 330")) {
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        throw std::runtime_error("Dear ImGui OpenGL initialization failed.");
    }
    imgui_initialized_ = true;
    set_mouse_captured(false);
    clear_runtime_input();

    render_resources_ = std::make_unique<RenderResources>(
        resources, generation_.scene, authored_fire, authored_water, authored_earth,
        authored_air, authored_aether, authored_start, authored_exit, exit_arch_,
        generation_.generation.effective_seed);
    if (profile_traversal_.has_value()) {
        apply_transition(game_session_.begin_exploration(GameClock::now()));
        movement_input_blocked_ = false;
        std::cout << "Profile traversal workload\n"
                  << "  Fingerprint: "
                  << format_fingerprint(profile_traversal_->fingerprint) << '\n'
                  << "  Chamber visits: "
                  << profile_traversal_->chamber_visit_order.size() << '\n'
                  << "  Waypoints: "
                  << profile_traversal_->waypoints_metres.size() << '\n';
    }
    std::cout << "Generated cave scene\n"
              << "  Mesh pieces: " << generation_.scene.mesh_pieces.size() << '\n'
              << "  Static vertices: " << generation_.scene.static_vertex_count << '\n'
              << "  Colliders: " << generation_.scene.colliders.size() << '\n'
              << "  Grounded start chamber: "
              << controller_->state().safe_chamber_id.value << '\n'
              << "  Scene fingerprint: "
              << format_fingerprint(generation_.scene.fingerprint) << '\n';
}

void Application::initialize_window()
{
    glfwSetErrorCallback(glfw_error_callback);
    if (glfwInit() != GLFW_TRUE) {
        throw std::runtime_error("GLFW initialization failed.");
    }
    glfw_initialized_ = true;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SRGB_CAPABLE, GLFW_TRUE);
#if defined(__APPLE__)
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif

    window_ = glfwCreateWindow(
        initial_window_width,
        initial_window_height,
        "Crystalbound - Elemental Cave Exploration",
        nullptr,
        nullptr);
    if (window_ == nullptr) {
        throw std::runtime_error("GLFW could not create an OpenGL 3.3 Core window.");
    }

    glfwMakeContextCurrent(window_);
    glfwSwapInterval(profile_seconds_.has_value() && !profile_vsync_ ? 0 : 1);
}

void Application::initialize_opengl()
{
    const int loaded_version = gladLoadGL(glfwGetProcAddress);
    if (loaded_version == 0) {
        throw std::runtime_error("GLAD could not load OpenGL through GLFW.");
    }

    const int loaded_major = GLAD_VERSION_MAJOR(loaded_version);
    const int loaded_minor = GLAD_VERSION_MINOR(loaded_version);
    if (loaded_major < 3 || (loaded_major == 3 && loaded_minor < 3)) {
        throw std::runtime_error(
            "GLAD loaded OpenGL " + std::to_string(loaded_major) + '.'
            + std::to_string(loaded_minor) + ", but OpenGL 3.3 Core is required.");
    }

    int context_major{};
    int context_minor{};
    int profile_mask{};
    glGetIntegerv(GL_MAJOR_VERSION, &context_major);
    glGetIntegerv(GL_MINOR_VERSION, &context_minor);
    glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &profile_mask);
    if (context_major < 3 || (context_major == 3 && context_minor < 3)
        || (profile_mask & GL_CONTEXT_CORE_PROFILE_BIT) == 0) {
        throw std::runtime_error(
            "The created context does not satisfy the OpenGL 3.3 Core requirement.");
    }
    int framebuffer_color_encoding{};
    glGetFramebufferAttachmentParameteriv(
        GL_FRAMEBUFFER,
        GL_BACK_LEFT,
        GL_FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING,
        &framebuffer_color_encoding);
    if (glGetError() != GL_NO_ERROR || framebuffer_color_encoding != GL_SRGB) {
        throw std::runtime_error(
            "The default framebuffer is not sRGB-capable; Crystalbound requires linear lighting output.");
    }

    std::cout << "OpenGL runtime initialized\n"
              << "  Vendor: " << opengl_string(GL_VENDOR) << '\n'
              << "  Renderer: " << opengl_string(GL_RENDERER) << '\n'
              << "  OpenGL: " << opengl_string(GL_VERSION) << '\n'
              << "  GLSL: " << opengl_string(GL_SHADING_LANGUAGE_VERSION) << '\n';

    glDepthFunc(GL_LESS);
    apply_render_pass_state(opaque_render_pass);
    glClearColor(background_red, background_green, background_blue, 1.0F);

    glfwGetFramebufferSize(window_, &framebuffer_width_, &framebuffer_height_);
    if (framebuffer_width_ > 0 && framebuffer_height_ > 0) {
        glViewport(0, 0, framebuffer_width_, framebuffer_height_);
    }
}

void Application::run_frame_loop()
{
    double previous_time = glfwGetTime();
    while (glfwWindowShouldClose(window_) == GLFW_FALSE) {
        const double frame_started = glfwGetTime();
        const double current_time = glfwGetTime();
        const double elapsed = std::max(0.0, current_time - previous_time);
        const GameTimePoint game_now{GameClock::now()};
        previous_time = current_time;

        process_movement(static_cast<float>(elapsed));
        process_interaction(game_now);

        if (framebuffer_width_ <= 0 || framebuffer_height_ <= 0) {
            pause_game(game_now);
            glfwWaitEventsTimeout(0.05);
            previous_time = glfwGetTime();
            continue;
        }

        apply_render_pass_state(opaque_render_pass);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        const float aspect_ratio = static_cast<float>(framebuffer_width_)
            / static_cast<float>(framebuffer_height_);
        render_resources_->render(
            camera_.view_matrix(), camera_.projection_matrix(aspect_ratio), camera_.position(),
            controller_->state().safe_chamber_id,
            static_cast<float>(current_time), crystal_collection_);
        apply_render_pass_state(ui_render_pass);
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        render_game_ui(game_now);
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        const double before_present = glfwGetTime();
        glfwSwapBuffers(window_);
        const double after_present = glfwGetTime();
        glfwPollEvents();
        if (frame_profiler_.has_value()) {
            const double frame_finished{glfwGetTime()};
            if (frame_profiler_->record({
                    (frame_finished - frame_started) * 1'000.0,
                    (before_present - frame_started) * 1'000.0,
                    (after_present - before_present) * 1'000.0})) {
                const FrameProfileSummary summary{frame_profiler_->summary()};
                std::cout << std::fixed << std::setprecision(3)
                          << "Frame profile complete\n"
                          << "  Resolution: " << framebuffer_width_ << 'x'
                          << framebuffer_height_ << '\n'
                          << "  VSync: " << (profile_vsync_ ? "on" : "off") << '\n'
                          << "  Warm-up seconds: " << profile_warmup_seconds << '\n'
                          << "  Measured seconds: " << *profile_seconds_ << '\n'
                          << "  Samples: " << summary.sample_count << '\n'
                          << "  Median frame time (ms): "
                          << summary.median_milliseconds << '\n'
                          << "  P95 frame time (ms): "
                          << summary.p95_milliseconds << '\n'
                          << "  Median CPU before present (ms): "
                          << summary.median_cpu_before_present_milliseconds << '\n'
                          << "  P95 CPU before present (ms): "
                          << summary.p95_cpu_before_present_milliseconds << '\n'
                          << "  Median present wait (ms): "
                          << summary.median_present_milliseconds << '\n'
                          << "  P95 present wait (ms): "
                          << summary.p95_present_milliseconds << '\n';
                glfwSetWindowShouldClose(window_, GLFW_TRUE);
            }
        }
    }
}

void Application::process_movement(const float delta_seconds)
{
    if (game_session_.state() != GameState::playing) {
        return;
    }
    GroundedMovementInput input{};
    if (profile_traversal_.has_value()
        && !profile_traversal_->waypoints_metres.empty()) {
        const GeometryVector3 position{controller_->state().feet_position_metres};
        constexpr double waypoint_radius_metres{0.30};
        std::size_t checked{};
        while (checked < profile_traversal_->waypoints_metres.size()) {
            const GeometryVector3& target{
                profile_traversal_->waypoints_metres[profile_waypoint_index_]};
            if (std::hypot(target.x - position.x, target.z - position.z)
                > waypoint_radius_metres) {
                break;
            }
            ++profile_waypoint_index_;
            if (profile_waypoint_index_
                >= profile_traversal_->waypoints_metres.size()) {
                profile_waypoint_index_ = profile_traversal_->waypoints_metres.size() > 1U
                    ? 1U : 0U;
            }
            ++checked;
        }
        const GeometryVector3& target{
            profile_traversal_->waypoints_metres[profile_waypoint_index_]};
        constexpr double radians_to_degrees{57.29577951308232};
        const double yaw_degrees{
            std::atan2(target.z - position.z, target.x - position.x)
            * radians_to_degrees};
        input.view_yaw_degrees = yaw_degrees;
        input.forward = 1.0;
        const GeometryVector3 camera_position{controller_->camera_position_metres()};
        camera_.set_pose(
            {static_cast<float>(camera_position.x),
             static_cast<float>(camera_position.y),
             static_cast<float>(camera_position.z)},
            {static_cast<float>(target.x - position.x), 0.0F,
             static_cast<float>(target.z - position.z)});
    } else {
        input.view_yaw_degrees = camera_.yaw_degrees();
    }
    if (movement_input_blocked_) {
        movement_input_blocked_ = !gameplay_keys_released();
    }
    if (!movement_input_blocked_ && !profile_traversal_.has_value()) {
        input.forward = (key_is_down(window_, GLFW_KEY_W) ? 1.0F : 0.0F)
            - (key_is_down(window_, GLFW_KEY_S) ? 1.0F : 0.0F);
        input.right = (key_is_down(window_, GLFW_KEY_D) ? 1.0F : 0.0F)
            - (key_is_down(window_, GLFW_KEY_A) ? 1.0F : 0.0F);
        input.jump = key_is_down(window_, GLFW_KEY_SPACE);
        input.sprint = key_is_down(window_, GLFW_KEY_LEFT_SHIFT)
            || key_is_down(window_, GLFW_KEY_RIGHT_SHIFT);
    }
    const ControllerAdvanceResult result{controller_->advance(input, delta_seconds)};
    if (result.backlog_discarded && !backlog_warning_emitted_) {
        std::cerr << "Controller warning: discarded excess fixed-step backlog.\n";
        backlog_warning_emitted_ = true;
    }
    const GeometryVector3 camera_position{controller_->camera_position_metres()};
    camera_.set_position({
        static_cast<float>(camera_position.x),
        static_cast<float>(camera_position.y),
        static_cast<float>(camera_position.z),
    });
}

void Application::process_interaction(const GameTimePoint now)
{
    if (game_session_.state() != GameState::playing) {
        focused_crystal_.reset();
        focused_exit_arch_ = false;
        interaction_button_.reset(key_is_down(window_, GLFW_KEY_E));
        return;
    }
    const glm::vec3& position{camera_.position()};
    const glm::vec3& forward{camera_.forward()};
    const CameraInteractionQuery query{
        {position.x, position.y, position.z},
        {forward.x, forward.y, forward.z},
    };
    const bool pressed_edge{
        interaction_button_.update(key_is_down(window_, GLFW_KEY_E))};
    FocusedCrystalResult focus{focus_crystal(
        interaction_targets_, query, visibility_world_, crystal_collection_)};
    ExitFocusResult exit_focus{focus_exit_arch(exit_arch_, query, visibility_world_)};
    focused_exit_arch_ = exit_focus.focused.has_value();
    if (mouse_captured_ && pressed_edge && focused_exit_arch_) {
        const ExitAttemptResult result{attempt_exit_arch(
            exit_arch_, query, visibility_world_, true, true, crystal_collection_)};
        if (result.completed) {
            const GameTransition transition{
                game_session_.complete(now, current_best_key())};
            apply_transition(transition);
            std::cout << "Escaped the cave in "
                      << format_elapsed_time(game_session_.elapsed_seconds(now))
                      << '\n';
        }
    } else if (mouse_captured_ && pressed_edge) {
        const CollectionAttemptResult result{attempt_crystal_collection(
            interaction_targets_, query, visibility_world_, true,
            crystal_collection_)};
        if (result.collected && result.element.has_value()) {
            std::cout << "Collected the " << element_name(*result.element)
                      << " Crystal (" << crystal_collection_.collected_count()
                      << '/' << elemental_order.size() << ")\n";
            focus = focus_crystal(
                interaction_targets_, query, visibility_world_, crystal_collection_);
            exit_focus = focus_exit_arch(exit_arch_, query, visibility_world_);
            focused_exit_arch_ = exit_focus.focused.has_value();
        }
    }
    focused_crystal_ = mouse_captured_ && !focused_exit_arch_
        ? focus.focused : std::nullopt;
    if (game_session_.state() != GameState::playing) {
        focused_crystal_.reset();
        focused_exit_arch_ = false;
    }
}

void Application::render_game_ui(const GameTimePoint now)
{
    switch (game_session_.state()) {
    case GameState::start:
        render_start_ui(now);
        break;
    case GameState::playing:
        render_playing_ui(now);
        break;
    case GameState::paused:
        render_pause_ui(now);
        break;
    case GameState::completed:
        render_completed_ui(now);
        break;
    }
}

void Application::render_start_ui(const GameTimePoint now)
{
    const ImGuiIO& io{ImGui::GetIO()};
    ImGui::SetNextWindowPos(
        {io.DisplaySize.x * 0.5F, io.DisplaySize.y * 0.5F},
        ImGuiCond_Always, {0.5F, 0.5F});
    ImGui::SetNextWindowBgAlpha(0.94F);
    constexpr ImGuiWindowFlags flags{ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings};
    if (ImGui::Begin("Crystalbound", nullptr, flags)) {
        ImGui::TextUnformatted("CRYSTALBOUND");
        ImGui::Separator();
        ImGui::TextWrapped(
            "Explore a shifting elemental cave, recover its five glowing "
            "crystals, and return them to the ancient exit arch.");
        ImGui::Spacing();
        ImGui::TextUnformatted("Objective");
        ImGui::TextWrapped(
            "Collect Fire, Water, Earth, Air, and Aether in any order, then "
            "press E at the awakened arch to escape.");
        ImGui::Spacing();
        render_crystal_progress();
        ImGui::Spacing();
        ImGui::TextUnformatted("Controls");
        ImGui::TextUnformatted("WASD  Move          Mouse  Look");
        ImGui::TextUnformatted("Space Jump          Shift  Sprint");
        ImGui::TextUnformatted("E     Interact      Esc    Pause");
        ImGui::Spacing();
        const std::string seed_label{
            format_run_seed_label(generation_.generation)};
        ImGui::Text("Seed: %s", seed_label.c_str());
        if (!ui_error_message_.empty()) {
            ImGui::TextColored({1.0F, 0.35F, 0.30F, 1.0F}, "%s",
                ui_error_message_.c_str());
        }
        ImGui::Spacing();
        if (ImGui::Button("Begin Exploration", {250.0F, 42.0F})) {
            ui_error_message_.clear();
            apply_transition(game_session_.begin_exploration(now));
        }
    }
    ImGui::End();
}

void Application::render_playing_ui(const GameTimePoint now)
{
    ImGui::SetNextWindowPos({18.0F, 18.0F}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.58F);
    constexpr ImGuiWindowFlags hud_flags{ImGuiWindowFlags_NoDecoration
        | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav
        | ImGuiWindowFlags_NoInputs};
    if (ImGui::Begin("Run HUD", nullptr, hud_flags)) {
        const std::string elapsed{
            format_elapsed_time(game_session_.elapsed_seconds(now))};
        ImGui::Text("Time  %s", elapsed.c_str());
        render_crystal_progress();
    }
    ImGui::End();
    render_interaction_prompt();
}

void Application::render_pause_ui(const GameTimePoint now)
{
    const ImGuiIO& io{ImGui::GetIO()};
    ImGui::SetNextWindowPos(
        {io.DisplaySize.x * 0.5F, io.DisplaySize.y * 0.5F},
        ImGuiCond_Always, {0.5F, 0.5F});
    ImGui::SetNextWindowBgAlpha(0.94F);
    constexpr ImGuiWindowFlags flags{ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings};
    if (ImGui::Begin("Paused", nullptr, flags)) {
        const std::string elapsed{
            format_elapsed_time(game_session_.elapsed_seconds(now))};
        const std::string seed_label{
            format_run_seed_label(generation_.generation)};
        ImGui::Text("Time: %s", elapsed.c_str());
        ImGui::Text("Seed: %s", seed_label.c_str());
        render_crystal_progress();
        if (!ui_error_message_.empty()) {
            ImGui::TextColored({1.0F, 0.35F, 0.30F, 1.0F}, "%s",
                ui_error_message_.c_str());
        }
        ImGui::Spacing();
        if (ImGui::Button("Resume", {220.0F, 34.0F})) {
            ui_error_message_.clear();
            apply_transition(game_session_.resume(now));
        }
        if (ImGui::Button("Restart Seed", {220.0F, 34.0F})) {
            restart_seed();
        }
        if (ImGui::Button("New Cave", {220.0F, 34.0F})) {
            new_cave();
        }
        if (ImGui::Button("Quit", {220.0F, 34.0F})) {
            glfwSetWindowShouldClose(window_, GLFW_TRUE);
        }
    }
    ImGui::End();
}

void Application::render_completed_ui(const GameTimePoint now)
{
    const ImGuiIO& io{ImGui::GetIO()};
    ImGui::SetNextWindowPos(
        {io.DisplaySize.x * 0.5F, io.DisplaySize.y * 0.5F},
        ImGuiCond_Always, {0.5F, 0.5F});
    ImGui::SetNextWindowBgAlpha(0.95F);
    constexpr ImGuiWindowFlags flags{ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings};
    if (ImGui::Begin("Cave Escaped", nullptr, flags)) {
        ImGui::TextUnformatted("THE ARCH AWAKENS");
        ImGui::Separator();
        const double final_seconds{game_session_.elapsed_seconds(now)};
        const std::string final_time{format_elapsed_time(final_seconds)};
        const std::string seed_label{
            format_run_seed_label(generation_.generation)};
        ImGui::Text("Final time: %s", final_time.c_str());
        ImGui::Text("Seed: %s", seed_label.c_str());
        render_crystal_progress();
        const std::optional<double> best{
            game_session_.best_seconds(current_best_key())};
        if (best.has_value()) {
            const std::string best_time{format_elapsed_time(*best)};
            ImGui::Text("Session best: %s", best_time.c_str());
        }
        if (!ui_error_message_.empty()) {
            ImGui::TextColored({1.0F, 0.35F, 0.30F, 1.0F}, "%s",
                ui_error_message_.c_str());
        }
        ImGui::Spacing();
        if (ImGui::Button("Play Again", {220.0F, 34.0F})) {
            play_again();
        }
        if (ImGui::Button("Restart Seed", {220.0F, 34.0F})) {
            restart_seed();
        }
        if (ImGui::Button("Quit", {220.0F, 34.0F})) {
            glfwSetWindowShouldClose(window_, GLFW_TRUE);
        }
    }
    ImGui::End();
}

void Application::render_crystal_progress()
{
    ImGui::Text("Crystals: %zu/%zu", crystal_collection_.collected_count(),
        elemental_order.size());
    for (std::size_t index{}; index < elemental_order.size(); ++index) {
        const Element element{elemental_order[index]};
        const std::array<float, 3> color{
            linear_color(elemental_persona(element).emission)};
        const std::string label{std::string{element_name(element)}
            + (crystal_collection_.is_collected(element) ? " [x]" : " [ ]")};
        ImGui::TextColored({color[0], color[1], color[2], 1.0F}, "%s",
            label.c_str());
        if (index + 1U < elemental_order.size()) {
            ImGui::SameLine();
        }
    }
}

void Application::render_interaction_prompt()
{
    if (!focused_crystal_.has_value() && !focused_exit_arch_) {
        return;
    }
    std::string prompt;
    if (focused_exit_arch_) {
        prompt = crystal_collection_.all_collected()
            ? "Press E to escape"
            : "The arch needs all five crystals";
    } else {
        prompt = "Press E to collect the "
            + std::string{element_name(focused_crystal_->target.element)}
            + " Crystal";
    }
    const ImGuiIO& io{ImGui::GetIO()};
    ImGui::SetNextWindowPos(
        {io.DisplaySize.x * 0.5F, io.DisplaySize.y * 0.82F},
        ImGuiCond_Always,
        {0.5F, 0.5F});
    ImGui::SetNextWindowBgAlpha(0.62F);
    constexpr ImGuiWindowFlags prompt_flags{
        ImGuiWindowFlags_NoDecoration
        | ImGuiWindowFlags_AlwaysAutoResize
        | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoFocusOnAppearing
        | ImGuiWindowFlags_NoNav
        | ImGuiWindowFlags_NoInputs};
    if (ImGui::Begin("Interaction prompt", nullptr, prompt_flags)) {
        ImGui::TextUnformatted(prompt.c_str());
    }
    ImGui::End();
}

void Application::apply_transition(const GameTransition& transition)
{
    if (!transition.accepted) {
        return;
    }
    if (transition.clear_held_input) {
        clear_runtime_input();
    }
    if (transition.discard_first_mouse_delta) {
        mouse_sample_pending_ = true;
    }
    if (transition.cursor == CursorRequest::capture) {
        set_mouse_captured(true);
    } else if (transition.cursor == CursorRequest::release) {
        set_mouse_captured(false);
    }
}

void Application::pause_game(const GameTimePoint now)
{
    apply_transition(game_session_.pause(now));
}

void Application::rebuild_run(const Seed requested_seed)
{
    CaveGenerationResult candidate_generation{generate_cave(requested_seed)};
    ExitArchData candidate_arch{build_exit_arch(candidate_generation)};
    const MaterialModelLoadResult authored_fire{
        load_fire_chamber_asset(resource_root())};
    const MaterialModelLoadResult authored_water{
        load_water_chamber_asset(resource_root())};
    const MaterialModelLoadResult authored_earth{
        load_earth_chamber_asset(resource_root())};
    const MaterialModelLoadResult authored_air{
        load_air_chamber_asset(resource_root())};
    const MaterialModelLoadResult authored_aether{
        load_aether_chamber_asset(resource_root())};
    const MaterialModelLoadResult authored_start{
        load_start_chamber_asset(resource_root())};
    const MaterialModelLoadResult authored_exit{
        load_exit_chamber_asset(resource_root())};
    CollisionWorld candidate_collision_world{
        build_collision_world(candidate_generation.scene)};
    append_authored_chamber_collision(
        candidate_collision_world,
        build_fire_chamber_collision(
            authored_fire,
            fire_chamber_placement(candidate_generation.scene)));
    append_authored_chamber_collision(
        candidate_collision_world,
        build_water_chamber_collision(
            authored_water,
            water_chamber_placement(candidate_generation.scene)));
    append_authored_chamber_collision(
        candidate_collision_world,
        build_earth_chamber_collision(
            authored_earth,
            earth_chamber_placement(candidate_generation.scene)));
    append_authored_chamber_collision(
        candidate_collision_world,
        build_air_chamber_collision(
            authored_air,
            air_chamber_placement(candidate_generation.scene)));
    append_authored_chamber_collision(candidate_collision_world,
        build_aether_chamber_collision(authored_aether,
            aether_chamber_placement(candidate_generation.scene)));
    append_authored_chamber_collision(candidate_collision_world,
        build_start_chamber_collision(authored_start,
            start_chamber_placement(candidate_generation.scene)));
    append_authored_chamber_collision(candidate_collision_world,
        build_exit_chamber_collision(authored_exit,
            exit_chamber_placement(candidate_generation.scene)));
    auto candidate_controller{std::make_unique<GroundedController>(
        std::move(candidate_collision_world),
        find_start_spawn(candidate_generation))};
    std::vector<CrystalInteractionTarget> candidate_targets{
        build_crystal_interaction_targets(
            candidate_generation.scene.elemental_visuals)};
    VisibilityWorld candidate_visibility{
        build_crystal_visibility_world(candidate_generation.scene)};
    auto candidate_resources{std::make_unique<RenderResources>(
        resource_root(), candidate_generation.scene, authored_fire, authored_water,
        authored_earth, authored_air, authored_aether, authored_start, authored_exit,
        candidate_arch,
        candidate_generation.generation.effective_seed)};

    generation_ = std::move(candidate_generation);
    exit_arch_ = std::move(candidate_arch);
    controller_ = std::move(candidate_controller);
    interaction_targets_ = std::move(candidate_targets);
    visibility_world_ = std::move(candidate_visibility);
    render_resources_ = std::move(candidate_resources);
    crystal_collection_ = {};
    backlog_warning_emitted_ = false;

    const GeometryVector3& position{generation_.scene.start_camera_position_metres};
    const GeometryVector3& forward{generation_.scene.start_camera_forward};
    camera_.set_pose(
        {static_cast<float>(position.x), static_cast<float>(position.y),
            static_cast<float>(position.z)},
        {static_cast<float>(forward.x), static_cast<float>(forward.y),
            static_cast<float>(forward.z)});
    const GeometryVector3 camera_position{controller_->camera_position_metres()};
    camera_.set_position({static_cast<float>(camera_position.x),
        static_cast<float>(camera_position.y),
        static_cast<float>(camera_position.z)});
    clear_runtime_input();
    ui_error_message_.clear();

    std::cout << "Started cave for requested seed "
              << generation_.generation.requested_seed.value
              << " (effective " << generation_.generation.effective_seed.value
              << ")\n";
}

void Application::restart_seed()
{
    if (game_session_.state() != GameState::paused
        && game_session_.state() != GameState::completed) {
        ui_error_message_ = "Restart Seed is unavailable in this game state.";
        return;
    }
    try {
        const Seed requested{generation_.generation.requested_seed};
        rebuild_run(requested);
        const GameTransition transition{game_session_.restart_seed()};
        if (!transition.accepted) {
            throw std::logic_error("Restart Seed is unavailable in this game state.");
        }
        apply_transition(transition);
    } catch (const std::exception& error) {
        ui_error_message_ = std::string{"Restart failed: "} + error.what();
    }
}

void Application::new_cave()
{
    if (game_session_.state() != GameState::paused) {
        ui_error_message_ = "New Cave is unavailable in this game state.";
        return;
    }
    try {
        const Seed requested{choose_new_requested_seed(
            generation_.generation.requested_seed, seed_source_)};
        rebuild_run(requested);
        const GameTransition transition{game_session_.new_cave()};
        if (!transition.accepted) {
            throw std::logic_error("New Cave is unavailable in this game state.");
        }
        apply_transition(transition);
    } catch (const std::exception& error) {
        ui_error_message_ = std::string{"New Cave failed: "} + error.what();
    }
}

void Application::play_again()
{
    if (game_session_.state() != GameState::completed) {
        ui_error_message_ = "Play Again is unavailable in this game state.";
        return;
    }
    try {
        const Seed requested{choose_new_requested_seed(
            generation_.generation.requested_seed, seed_source_)};
        rebuild_run(requested);
        const GameTransition transition{game_session_.play_again()};
        if (!transition.accepted) {
            throw std::logic_error("Play Again is unavailable in this game state.");
        }
        apply_transition(transition);
    } catch (const std::exception& error) {
        ui_error_message_ = std::string{"Play Again failed: "} + error.what();
    }
}

void Application::clear_runtime_input()
{
    movement_input_blocked_ = true;
    focused_crystal_.reset();
    focused_exit_arch_ = false;
    interaction_button_.reset(
        window_ != nullptr && key_is_down(window_, GLFW_KEY_E));
    mouse_sample_pending_ = true;
}

bool Application::gameplay_keys_released() const
{
    return !key_is_down(window_, GLFW_KEY_W)
        && !key_is_down(window_, GLFW_KEY_A)
        && !key_is_down(window_, GLFW_KEY_S)
        && !key_is_down(window_, GLFW_KEY_D)
        && !key_is_down(window_, GLFW_KEY_SPACE)
        && !key_is_down(window_, GLFW_KEY_LEFT_SHIFT)
        && !key_is_down(window_, GLFW_KEY_RIGHT_SHIFT)
        && !key_is_down(window_, GLFW_KEY_E);
}

SessionBestKey Application::current_best_key() const noexcept
{
    return {generation_.generation.generator_version,
        generation_.generation.effective_seed};
}

void Application::set_mouse_captured(const bool captured)
{
    mouse_captured_ = captured;
    mouse_sample_pending_ = true;
    if (window_ != nullptr) {
        glfwSetInputMode(window_, GLFW_CURSOR,
            mouse_captured_ ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    }
}

void Application::shutdown() noexcept
{
    if (window_ != nullptr) {
        if (glfwGetCurrentContext() != window_) {
            glfwMakeContextCurrent(window_);
        }
        if (imgui_initialized_) {
            ImGui_ImplOpenGL3_Shutdown();
            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext();
            imgui_initialized_ = false;
        }
        render_resources_.reset();
        glfwDestroyWindow(window_);
        window_ = nullptr;
    }
    if (glfw_initialized_) {
        glfwTerminate();
        glfw_initialized_ = false;
    }
}

void Application::glfw_error_callback(const int error_code, const char* description)
{
    std::cerr << "GLFW error " << error_code << ": "
              << (description == nullptr ? "<no description>" : description) << '\n';
}

void Application::framebuffer_size_callback(GLFWwindow* window, const int width, const int height)
{
    Application* application = application_for(window);
    if (application == nullptr) {
        return;
    }
    application->framebuffer_width_ = width;
    application->framebuffer_height_ = height;
    if (width > 0 && height > 0) {
        glViewport(0, 0, width, height);
    }
}

void Application::cursor_position_callback(
    GLFWwindow* window,
    const double x_position,
    const double y_position)
{
    Application* application = application_for(window);
    if (application == nullptr || !application->mouse_captured_) {
        return;
    }

    if (application->mouse_sample_pending_) {
        application->last_mouse_x_ = x_position;
        application->last_mouse_y_ = y_position;
        application->mouse_sample_pending_ = false;
        return;
    }

    const float horizontal_delta = static_cast<float>(x_position - application->last_mouse_x_);
    const float vertical_delta = static_cast<float>(application->last_mouse_y_ - y_position);
    application->last_mouse_x_ = x_position;
    application->last_mouse_y_ = y_position;
    application->camera_.rotate(horizontal_delta, vertical_delta);
}

void Application::window_focus_callback(GLFWwindow* window, const int focused)
{
    Application* application = application_for(window);
    if (application != nullptr && focused == GLFW_FALSE) {
        application->pause_game(GameClock::now());
    }
}

void Application::window_iconify_callback(GLFWwindow* window, const int iconified)
{
    Application* application = application_for(window);
    if (application != nullptr && iconified == GLFW_TRUE) {
        application->pause_game(GameClock::now());
    }
}

void Application::key_callback(
    GLFWwindow* window,
    const int key,
    const int scan_code,
    const int action,
    const int modifiers)
{
    static_cast<void>(scan_code);
    static_cast<void>(modifiers);

    Application* application = application_for(window);
    if (application != nullptr && key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        application->pause_game(GameClock::now());
    }
}

}  // namespace crystalbound
