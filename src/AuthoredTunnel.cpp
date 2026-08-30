#include "crystalbound/AuthoredTunnel.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <string_view>

#include "crystalbound/MazeGeneration.hpp"

namespace crystalbound {
namespace {

constexpr float bounds_tolerance_metres{0.002F};
constexpr double millimetres_per_metre{1'000.0};
constexpr double route_centerline_height_metres{1.35};
constexpr double segment_source_length_metres{4.0};
constexpr double entrance_source_outward_metres{1.5};
constexpr double entrance_source_inward_metres{0.1};
constexpr double seam_tolerance_metres{0.002};
constexpr double decoration_endpoint_clearance_metres{2.25};
constexpr double pillar_spacing_metres{10.0};
constexpr double lantern_spacing_metres{9.0};
constexpr double support_spacing_metres{14.0};
constexpr double rubble_spacing_metres{7.0};
constexpr std::uint64_t segment_instance_domain{0x5455'4E4E'454C'0000ULL};
constexpr std::uint64_t entrance_instance_domain{0x454E'5452'414E'4300ULL};
constexpr std::uint64_t decoration_instance_domain{0x4445'434F'5200'0000ULL};

struct RouteRun {
    Edge route{};
    std::uint8_t ordinal{};
    GeometryVector3 first_centerline_metres{};
    GeometryVector3 second_centerline_metres{};
    GeometryVector3 tangent{};
    GeometryVector3 left{};
    double length_metres{};
};

[[nodiscard]] GeometryVector3 to_metres(const IntegerPoint3& point) noexcept
{
    return {point.x_millimetres / millimetres_per_metre,
        point.y_millimetres / millimetres_per_metre,
        point.z_millimetres / millimetres_per_metre};
}

[[nodiscard]] bool near(const float actual, const float expected) noexcept
{
    return std::isfinite(actual)
        && std::abs(actual - expected) <= bounds_tolerance_metres;
}

[[nodiscard]] bool has_object(
    const MaterialModelLoadResult& model,
    const std::string_view name)
{
    return std::any_of(model.objects.begin(), model.objects.end(),
        [name](const MaterialModelObject& object) {
            return object.name == name;
        });
}

void require_model(
    const MaterialModelLoadResult& model,
    const std::string_view description,
    std::vector<std::string>& errors)
{
    if (model.batches.empty() || model.objects.empty()) {
        errors.push_back(std::string{description} + " has no renderable geometry.");
    }
    if (!model.warnings.empty()) {
        errors.push_back(std::string{description} + " produced OBJ warnings.");
    }
}

[[nodiscard]] RouteRun make_run(
    const Edge route,
    const std::uint8_t ordinal,
    const RouteGeometryContract& contract)
{
    if (contract.spline.control_points.size() < 2U) {
        throw GeometryError{"Authored tunnel run has fewer than two endpoints."};
    }
    const GeometryVector3 first{to_metres(contract.spline.control_points.front())};
    const GeometryVector3 second{to_metres(contract.spline.control_points.back())};
    const double dx{second.x - first.x};
    const double dz{second.z - first.z};
    const double length{std::hypot(dx, dz)};
    if (!std::isfinite(length) || length <= 0.0
        || std::abs(second.y - first.y) > seam_tolerance_metres) {
        throw GeometryError{"Authored tunnel run must be non-empty and level."};
    }
    for (const IntegerPoint3& point : contract.spline.control_points) {
        const GeometryVector3 sample{to_metres(point)};
        const double cross{dx * (sample.z - first.z)
            - dz * (sample.x - first.x)};
        if (std::abs(sample.y - first.y) > seam_tolerance_metres
            || std::abs(cross) > seam_tolerance_metres * length) {
            throw GeometryError{"Authored tunnel assets require a straight level route."};
        }
    }
    const GeometryVector3 tangent{dx / length, 0.0, dz / length};
    return {route, ordinal, first, second, tangent,
        {-tangent.z, 0.0, tangent.x}, length};
}

[[nodiscard]] std::vector<RouteRun> route_runs(const CaveSceneData& scene)
{
    std::vector<RouteRun> runs;
    runs.reserve(scene.routes.size() + scene.maze_rooms.size());
    for (const RouteGeometryContract& route : scene.routes) {
        const MazeRoomContract* maze{maze_room_for_route(scene.maze_rooms, route.edge)};
        if (maze == nullptr) {
            runs.push_back(make_run(route.edge, 0U, route));
            continue;
        }
        const std::array<RouteGeometryContract, 2> segments{
            maze_tunnel_segments(route, *maze)};
        runs.push_back(make_run(route.edge, 0U, segments[0]));
        runs.push_back(make_run(route.edge, 1U, segments[1]));
    }
    return runs;
}

[[nodiscard]] std::uint32_t decoration_site_count(
    const RouteRun& run,
    const double target_spacing_metres,
    const std::uint32_t minimum_count) noexcept
{
    const auto spaced_count{static_cast<std::uint32_t>(
        std::floor(run.length_metres / target_spacing_metres))};
    return std::max(minimum_count, spaced_count);
}

[[nodiscard]] GeometryVector3 decoration_site(
    const RouteRun& run,
    const std::uint32_t index,
    const std::uint32_t count,
    const double phase) noexcept
{
    const double clearance{std::min(
        decoration_endpoint_clearance_metres,
        run.length_metres * 0.375)};
    const double usable_length{
        std::max(0.0, run.length_metres - clearance * 2.0)};
    const double denominator{static_cast<double>(count) + 1.0};
    const double normalized{std::clamp(
        (static_cast<double>(index) + 1.0 + phase) / denominator,
        0.0,
        1.0)};
    const double distance{clearance + usable_length * normalized};
    return {run.first_centerline_metres.x
            + run.tangent.x * distance,
        run.first_centerline_metres.y - route_centerline_height_metres,
        run.first_centerline_metres.z
            + run.tangent.z * distance};
}

[[nodiscard]] double yaw_for_local_z(
    const GeometryVector3& direction) noexcept
{
    return std::atan2(direction.x, direction.z);
}

[[nodiscard]] ChamberTemplateRole destination_role(
    const CaveSceneData& scene,
    const Edge route)
{
    const auto chamber{std::find_if(scene.compiled_chambers.begin(),
        scene.compiled_chambers.end(), [route](const CompiledChamberTemplate& candidate) {
            return candidate.chamber_id == route.second;
        })};
    if (chamber == scene.compiled_chambers.end()) {
        throw GeometryError{"Authored tunnel route has no destination chamber."};
    }
    return chamber->role;
}

void add_decoration(
    AuthoredTunnelLayout& layout,
    const RouteRun& run,
    const TunnelDecorationKind kind,
    const GeometryVector3& position,
    const double yaw,
    std::uint64_t& ordinal)
{
    layout.decorations.push_back({kind,
        decoration_instance_domain ^ stable_edge_id(run.route)
            ^ (static_cast<std::uint64_t>(run.ordinal) << 40U) ^ ordinal++,
        run.route,
        run.ordinal,
        {position, yaw, {1.0, 1.0, 1.0}}});
}

[[nodiscard]] GeometryVector3 offset(
    const GeometryVector3& position,
    const GeometryVector3& direction,
    const double distance) noexcept
{
    return {position.x + direction.x * distance,
        position.y + direction.y * distance,
        position.z + direction.z * distance};
}

[[nodiscard]] GeometryVector3 local_z_direction(
    const TunnelTransform& transform) noexcept
{
    return {std::sin(transform.yaw_radians), 0.0,
        std::cos(transform.yaw_radians)};
}

[[nodiscard]] GeometryVector3 transformed_local_z_point(
    const TunnelTransform& transform,
    const double local_z) noexcept
{
    const GeometryVector3 direction{local_z_direction(transform)};
    return {transform.translation_metres.x
            + direction.x * local_z * transform.scale.z,
        transform.translation_metres.y,
        transform.translation_metres.z
            + direction.z * local_z * transform.scale.z};
}

[[nodiscard]] double horizontal_distance(
    const GeometryVector3& first,
    const GeometryVector3& second) noexcept
{
    return std::hypot(first.x - second.x, first.z - second.z);
}

[[nodiscard]] bool finite_transform(const TunnelTransform& transform) noexcept
{
    return std::isfinite(transform.translation_metres.x)
        && std::isfinite(transform.translation_metres.y)
        && std::isfinite(transform.translation_metres.z)
        && std::isfinite(transform.yaw_radians)
        && std::isfinite(transform.scale.x)
        && std::isfinite(transform.scale.y)
        && std::isfinite(transform.scale.z)
        && transform.scale.x > 0.0 && transform.scale.y > 0.0
        && transform.scale.z > 0.0;
}

}  // namespace

AuthoredTunnelAssets load_authored_tunnel_assets(
    const std::filesystem::path& resource_directory)
{
    const std::filesystem::path packaged_models{
        resource_directory / "assets" / "models"};
    const std::filesystem::path models{
        std::filesystem::exists(packaged_models / "TunnelSegment.obj")
            ? packaged_models
            : resource_directory};
    return {
        load_obj_material_batches(models / "TunnelSegment.obj"),
        load_obj_material_batches(models / "TunnelEntrance.obj"),
        load_obj_material_batches(models / "TunnelPillar.obj"),
        load_obj_material_batches(models / "TunnelRockSmall.obj"),
        load_obj_material_batches(models / "TunnelBrokenStones.obj"),
        load_obj_material_batches(models / "TunnelLantern.obj"),
        load_obj_material_batches(models / "TunnelPuddle.obj"),
        load_obj_material_batches(models / "TunnelWoodSupport.obj"),
    };
}

const MaterialModelLoadResult& authored_tunnel_decoration_asset(
    const AuthoredTunnelAssets& assets,
    const TunnelDecorationKind kind)
{
    switch (kind) {
    case TunnelDecorationKind::pillar:
        return assets.pillar;
    case TunnelDecorationKind::rock:
        return assets.rock;
    case TunnelDecorationKind::broken_stones:
        return assets.broken_stones;
    case TunnelDecorationKind::lantern:
        return assets.lantern;
    case TunnelDecorationKind::puddle:
        return assets.puddle;
    case TunnelDecorationKind::wood_support:
        return assets.wood_support;
    }
    throw ModelLoadError{"Unknown authored tunnel decoration kind."};
}

std::vector<std::string> validate_authored_tunnel_assets(
    const AuthoredTunnelAssets& assets)
{
    std::vector<std::string> errors;
    require_model(assets.segment, "Tunnel segment", errors);
    require_model(assets.entrance, "Tunnel entrance", errors);
    require_model(assets.pillar, "Tunnel pillar", errors);
    require_model(assets.rock, "Tunnel rock", errors);
    require_model(assets.broken_stones, "Tunnel broken stones", errors);
    require_model(assets.lantern, "Tunnel lantern", errors);
    require_model(assets.puddle, "Tunnel puddle", errors);
    require_model(assets.wood_support, "Tunnel wood support", errors);

    if (!near(assets.segment.minimum_bounds[2], -2.0F)
        || !near(assets.segment.maximum_bounds[2], 2.0F)
        || !near(assets.segment.minimum_bounds[1], -0.25F)
        || assets.segment.maximum_bounds[1] < 4.0F
        || !has_object(assets.segment, "TunnelSegment")) {
        errors.push_back(
            "Tunnel segment does not preserve its four-metre horseshoe contract.");
    }
    if (!near(assets.entrance.minimum_bounds[2], -1.5F)
        || assets.entrance.maximum_bounds[2] < 0.1F
        || !has_object(assets.entrance, "TunnelEntrance_Passage")) {
        errors.push_back(
            "Tunnel entrance does not preserve its passage and overlap contract.");
    }
    if (!near(assets.pillar.minimum_bounds[1], 0.0F)
        || !near(assets.pillar.maximum_bounds[1], 2.2F)) {
        errors.push_back("Tunnel pillar height no longer matches the side wall.");
    }
    if (assets.wood_support.minimum_bounds[0] > -1.6F
        || assets.wood_support.maximum_bounds[0] < 1.6F
        || assets.wood_support.maximum_bounds[1] < 4.0F) {
        errors.push_back("Tunnel wood support no longer spans the horseshoe profile.");
    }
    return errors;
}

AuthoredTunnelLayout build_authored_tunnel_layout(const CaveSceneData& scene)
{
    AuthoredTunnelLayout layout;
    const std::vector<RouteRun> runs{route_runs(scene)};
    for (const RouteRun& run : runs) {
        const std::uint32_t tile_count{std::max<std::uint32_t>(1U,
            static_cast<std::uint32_t>(
                std::ceil(run.length_metres / segment_source_length_metres)))};
        const double tile_length{run.length_metres / tile_count};
        for (std::uint32_t tile{}; tile < tile_count; ++tile) {
            const double distance{(static_cast<double>(tile) + 0.5) * tile_length};
            const GeometryVector3 center{
                run.first_centerline_metres.x + run.tangent.x * distance,
                run.first_centerline_metres.y - route_centerline_height_metres,
                run.first_centerline_metres.z + run.tangent.z * distance};
            layout.segments.push_back({
                segment_instance_domain ^ stable_edge_id(run.route)
                    ^ (static_cast<std::uint64_t>(run.ordinal) << 40U) ^ tile,
                run.route,
                run.ordinal,
                tile,
                tile_count,
                {center, yaw_for_local_z(run.tangent),
                    {1.0, 1.0, tile_length / segment_source_length_metres}},
            });
        }

        std::uint64_t decoration_ordinal{1U};
        const double side_sign{
            ((stable_edge_id(run.route) + run.ordinal) & 1U) == 0U ? 1.0 : -1.0};
        const GeometryVector3 selected_side{
            run.left.x * side_sign, 0.0, run.left.z * side_sign};
        const GeometryVector3 opposite_side{
            -selected_side.x, 0.0, -selected_side.z};

        const ChamberTemplateRole role{destination_role(scene, run.route)};
        const std::uint32_t pillar_sites{
            decoration_site_count(run, pillar_spacing_metres, 2U)};
        for (std::uint32_t index{}; index < pillar_sites; ++index) {
            const GeometryVector3 position{
                decoration_site(run, index, pillar_sites, 0.0)};
            add_decoration(layout, run, TunnelDecorationKind::pillar,
                offset(position, selected_side, 1.65),
                yaw_for_local_z(selected_side), decoration_ordinal);
            add_decoration(layout, run, TunnelDecorationKind::pillar,
                offset(position, opposite_side, 1.65),
                yaw_for_local_z(opposite_side), decoration_ordinal);
        }

        const std::uint32_t lantern_sites{
            decoration_site_count(run, lantern_spacing_metres, 2U)};
        for (std::uint32_t index{}; index < lantern_sites; ++index) {
            const GeometryVector3& wall_side{
                (index & 1U) == 0U ? selected_side : opposite_side};
            GeometryVector3 position{offset(
                decoration_site(run, index, lantern_sites, 0.22),
                wall_side,
                1.70)};
            position.y += 2.05;
            add_decoration(layout, run, TunnelDecorationKind::lantern,
                position, yaw_for_local_z(wall_side), decoration_ordinal);
        }

        const double route_support_spacing{
            role == ChamberTemplateRole::air ? 9.0 : support_spacing_metres};
        const std::uint32_t support_sites{
            decoration_site_count(run, route_support_spacing, 1U)};
        for (std::uint32_t index{}; index < support_sites; ++index) {
            add_decoration(layout, run, TunnelDecorationKind::wood_support,
                decoration_site(run, index, support_sites, -0.16),
                yaw_for_local_z(run.tangent),
                decoration_ordinal);
        }

        const std::uint32_t rubble_sites{
            decoration_site_count(run, rubble_spacing_metres, 2U)};
        for (std::uint32_t index{}; index < rubble_sites; ++index) {
            const bool use_selected_side{(index & 1U) == 0U};
            const GeometryVector3& wall_side{
                use_selected_side ? selected_side : opposite_side};
            const TunnelDecorationKind kind{use_selected_side
                    ? TunnelDecorationKind::rock
                    : TunnelDecorationKind::broken_stones};
            const double lateral_distance{use_selected_side ? 1.42 : 1.72};
            add_decoration(layout, run, kind,
                offset(decoration_site(run, index, rubble_sites, -0.24),
                    wall_side,
                    lateral_distance),
                yaw_for_local_z(run.tangent), decoration_ordinal);
        }

        if (role == ChamberTemplateRole::water) {
            const std::uint32_t puddle_sites{
                decoration_site_count(run, support_spacing_metres, 1U)};
            for (std::uint32_t index{}; index < puddle_sites; ++index) {
                const GeometryVector3& puddle_side{
                    (index & 1U) == 0U ? selected_side : opposite_side};
                add_decoration(layout, run, TunnelDecorationKind::puddle,
                    offset(decoration_site(run, index, puddle_sites, 0.12),
                        puddle_side,
                        0.45),
                    yaw_for_local_z(run.tangent), decoration_ordinal);
            }
        } else if (role == ChamberTemplateRole::fire
            || role == ChamberTemplateRole::earth) {
            add_decoration(layout, run, TunnelDecorationKind::rock,
                offset(decoration_site(run, 0U, 2U, 0.30), selected_side, 1.42),
                yaw_for_local_z(run.tangent), decoration_ordinal);
            add_decoration(layout, run, TunnelDecorationKind::broken_stones,
                offset(decoration_site(run, 1U, 2U, 0.30),
                    opposite_side,
                    1.72),
                yaw_for_local_z(run.tangent), decoration_ordinal);
        }
    }

    layout.entrances.reserve(scene.portals.size());
    for (const PortalContract& portal : scene.portals) {
        const double inward_x{portal.inward_direction_millimetres.x_millimetres
            / millimetres_per_metre};
        const double inward_z{portal.inward_direction_millimetres.z_millimetres
            / millimetres_per_metre};
        const double inward_length{std::hypot(inward_x, inward_z)};
        if (!std::isfinite(inward_length) || inward_length <= 0.0) {
            throw GeometryError{"Authored tunnel entrance has no inward direction."};
        }
        const GeometryVector3 inward{
            inward_x / inward_length, 0.0, inward_z / inward_length};
        const GeometryVector3 portal_center{to_metres(portal.center_millimetres)};
        const double approach{portal.approach_depth_millimetres
            / millimetres_per_metre};
        const GeometryVector3 inner_centerline{
            portal_center.x + inward.x * approach,
            portal_center.y,
            portal_center.z + inward.z * approach};
        const double source_span{entrance_source_outward_metres
            + entrance_source_inward_metres};
        const double required_span{approach
            + route_join_overlap_millimetres / millimetres_per_metre};
        const double scale_z{std::max(1.0, required_span / source_span)};
        const GeometryVector3 translation{
            inner_centerline.x - inward.x * entrance_source_inward_metres * scale_z,
            portal_center.y - route_centerline_height_metres,
            inner_centerline.z - inward.z * entrance_source_inward_metres * scale_z};
        layout.entrances.push_back({
            entrance_instance_domain ^ stable_edge_id(portal.route)
                ^ (static_cast<std::uint64_t>(portal.chamber_id.value) << 32U),
            portal.chamber_id,
            portal.route,
            {translation, yaw_for_local_z(inward), {1.0, 1.0, scale_z}},
        });
    }
    return layout;
}

std::vector<std::string> validate_authored_tunnel_layout(
    const CaveSceneData& scene,
    const AuthoredTunnelLayout& layout)
{
    std::vector<std::string> errors;
    if (layout.segments.empty()) {
        errors.push_back("Authored tunnel layout does not cover any route segments.");
    }
    const std::vector<RouteRun> runs{route_runs(scene)};
    for (const RouteRun& run : runs) {
        std::vector<const AuthoredTunnelSegmentInstance*> instances;
        for (const AuthoredTunnelSegmentInstance& instance : layout.segments) {
            if (instance.route == run.route && instance.run_ordinal == run.ordinal) {
                instances.push_back(&instance);
            }
        }
        std::sort(instances.begin(), instances.end(),
            [](const AuthoredTunnelSegmentInstance* left,
                const AuthoredTunnelSegmentInstance* right) {
                return left->tile_index < right->tile_index;
            });
        if (instances.empty()) {
            errors.push_back("Authored tunnel run has no visual segment tiles.");
            continue;
        }
        const std::uint32_t tile_count{instances.front()->tile_count};
        if (instances.size() != tile_count) {
            errors.push_back("Authored tunnel run has an incomplete tile set.");
            continue;
        }
        for (std::size_t index{}; index < instances.size(); ++index) {
            const AuthoredTunnelSegmentInstance& instance{*instances[index]};
            if (instance.tile_index != index || instance.tile_count != tile_count
                || !finite_transform(instance.transform)) {
                errors.push_back("Authored tunnel tile ordering or transform is invalid.");
                break;
            }
            const double expected_floor{
                run.first_centerline_metres.y - route_centerline_height_metres};
            if (std::abs(instance.transform.translation_metres.y - expected_floor)
                > seam_tolerance_metres) {
                errors.push_back("Authored tunnel tile changed floor elevation.");
                break;
            }
        }
        const GeometryVector3 first{transformed_local_z_point(
            instances.front()->transform, -segment_source_length_metres * 0.5)};
        const GeometryVector3 last{transformed_local_z_point(
            instances.back()->transform, segment_source_length_metres * 0.5)};
        if (horizontal_distance(first, run.first_centerline_metres)
                > seam_tolerance_metres
            || horizontal_distance(last, run.second_centerline_metres)
                > seam_tolerance_metres) {
            errors.push_back("Authored tunnel tiles do not reach both route endpoints.");
        }
        for (std::size_t index{1U}; index < instances.size(); ++index) {
            const GeometryVector3 previous_end{transformed_local_z_point(
                instances[index - 1U]->transform,
                segment_source_length_metres * 0.5)};
            const GeometryVector3 next_start{transformed_local_z_point(
                instances[index]->transform,
                -segment_source_length_metres * 0.5)};
            if (horizontal_distance(previous_end, next_start)
                > seam_tolerance_metres) {
                errors.push_back("Adjacent authored tunnel tiles contain a gap.");
                break;
            }
        }
    }
    if (layout.entrances.size() != scene.portals.size()) {
        errors.push_back("Authored tunnel entrances do not cover every chamber portal.");
    }
    for (const PortalContract& portal : scene.portals) {
        const auto instance{std::find_if(layout.entrances.begin(),
            layout.entrances.end(), [&](const AuthoredTunnelEntranceInstance& candidate) {
                return candidate.chamber_id == portal.chamber_id
                    && candidate.route == portal.route;
            })};
        if (instance == layout.entrances.end()
            || !finite_transform(instance->transform)) {
            errors.push_back("Chamber portal has no valid authored entrance transform.");
            continue;
        }
        const GeometryVector3 inward{local_z_direction(instance->transform)};
        const GeometryVector3 portal_center{to_metres(portal.center_millimetres)};
        const double approach{portal.approach_depth_millimetres
            / millimetres_per_metre};
        const GeometryVector3 expected_inner{
            portal_center.x + inward.x * approach,
            portal_center.y,
            portal_center.z + inward.z * approach};
        const GeometryVector3 model_inner{transformed_local_z_point(
            instance->transform, entrance_source_inward_metres)};
        if (horizontal_distance(model_inner, expected_inner)
            > seam_tolerance_metres) {
            errors.push_back("Authored tunnel entrance misses the chamber-side seam.");
        }
        const GeometryVector3 expected_outer{
            portal_center.x - inward.x * route_join_overlap_millimetres
                / millimetres_per_metre,
            portal_center.y,
            portal_center.z - inward.z * route_join_overlap_millimetres
                / millimetres_per_metre};
        const GeometryVector3 model_outer{transformed_local_z_point(
            instance->transform, -entrance_source_outward_metres)};
        const double inward_projection{
            (model_outer.x - expected_outer.x) * inward.x
            + (model_outer.z - expected_outer.z) * inward.z};
        const double lateral_projection{
            (model_outer.x - expected_outer.x) * -inward.z
            + (model_outer.z - expected_outer.z) * inward.x};
        if (inward_projection > seam_tolerance_metres
            || std::abs(lateral_projection) > seam_tolerance_metres) {
            errors.push_back("Authored tunnel entrance stops before the route overlap.");
        }
        const double expected_floor{portal_center.y - route_centerline_height_metres};
        if (std::abs(instance->transform.translation_metres.y - expected_floor)
            > seam_tolerance_metres) {
            errors.push_back("Authored tunnel entrance changed floor elevation.");
        }
    }
    if (layout.decorations.empty()) {
        errors.push_back("Authored tunnel layout contains no decoration instances.");
    }
    for (const TunnelDecorationKind kind : {
             TunnelDecorationKind::pillar,
             TunnelDecorationKind::rock,
             TunnelDecorationKind::broken_stones,
             TunnelDecorationKind::lantern,
             TunnelDecorationKind::puddle,
             TunnelDecorationKind::wood_support}) {
        if (std::none_of(layout.decorations.begin(), layout.decorations.end(),
                [kind](const AuthoredTunnelDecorationInstance& instance) {
                    return instance.kind == kind;
                })) {
            errors.push_back("Authored tunnel decoration kit is incomplete.");
        }
    }
    for (const AuthoredTunnelDecorationInstance& instance : layout.decorations) {
        if (!finite_transform(instance.transform)) {
            errors.push_back("Authored tunnel decoration has an invalid transform.");
            continue;
        }
        const auto run{std::find_if(runs.begin(), runs.end(),
            [&](const RouteRun& candidate) {
                return candidate.route == instance.route
                    && candidate.ordinal == instance.run_ordinal;
            })};
        if (run == runs.end()) {
            errors.push_back("Authored tunnel decoration references an unknown route.");
            continue;
        }
        const double dx{instance.transform.translation_metres.x
            - run->first_centerline_metres.x};
        const double dz{instance.transform.translation_metres.z
            - run->first_centerline_metres.z};
        const double lateral{std::abs(dx * run->left.x + dz * run->left.z)};
        const double minimum_lateral{instance.kind == TunnelDecorationKind::pillar
                ? 1.60
            : instance.kind == TunnelDecorationKind::lantern
                ? 1.65
            : instance.kind == TunnelDecorationKind::rock
                ? 1.38
            : instance.kind == TunnelDecorationKind::broken_stones
                ? 1.65
                : 0.0};
        if (lateral + seam_tolerance_metres < minimum_lateral) {
            errors.push_back(
                "Authored tunnel decoration intrudes into the protected corridor.");
        }
    }
    return errors;
}

}  // namespace crystalbound
