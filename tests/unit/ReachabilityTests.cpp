#include "ReachabilityTests.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "crystalbound/CaveScene.hpp"
#include "crystalbound/PlayerController.hpp"
#include "crystalbound/Reachability.hpp"

namespace crystalbound::test {
namespace {

class ReachabilityTestFailure final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

void require(const bool condition, const std::string_view message)
{
    if (!condition) {
        throw ReachabilityTestFailure{std::string{message}};
    }
}

template <typename Exception, typename Function>
void require_throws(
    Function&& function,
    const std::string_view expected,
    const std::string_view message)
{
    try {
        function();
    } catch (const Exception& error) {
        if (std::string_view{error.what()}.find(expected) == std::string_view::npos) {
            throw ReachabilityTestFailure{
                std::string{message} + ": " + error.what()};
        }
        return;
    }
    throw ReachabilityTestFailure{std::string{message} + ": no exception"};
}

struct ReachabilityFixture {
    CaveGenerationResult generation{generate_cave({42U})};
    CollisionWorld collision{build_collision_world(generation.scene)};
};

[[nodiscard]] MechanicalReachabilityReport validate(
    const ReachabilityFixture& fixture)
{
    return validate_mechanical_reachability(
        fixture.generation.generation.topology,
        fixture.generation.scene,
        fixture.collision);
}

[[nodiscard]] bool has_failure(
    const MechanicalReachabilityReport& report,
    const ReachabilityFailure failure)
{
    return std::any_of(
        report.issues.begin(), report.issues.end(),
        [failure](const ReachabilityIssue& issue) {
            return issue.failure == failure;
        });
}

[[nodiscard]] const ChamberNode& start_node(const TopologyData& topology)
{
    const auto found{std::find_if(
        topology.nodes.begin(), topology.nodes.end(),
        [](const ChamberNode& node) {
            return node.role == ChamberRole::start;
        })};
    if (found == topology.nodes.end()) {
        throw ReachabilityTestFailure{"fixture has no Start"};
    }
    return *found;
}

[[nodiscard]] RouteCollisionRegion& collision_route(
    ReachabilityFixture& fixture,
    const Edge edge)
{
    const auto found{std::find_if(
        fixture.collision.routes.begin(), fixture.collision.routes.end(),
        [edge](const RouteCollisionRegion& route) {
            return route.edge == edge;
        })};
    if (found == fixture.collision.routes.end()) {
        throw ReachabilityTestFailure{"fixture route is missing"};
    }
    return *found;
}

[[nodiscard]] ReachabilityIssue forced_issue()
{
    return {
        ReachabilityFailure::excessive_gap,
        std::nullopt,
        make_edge({0U}, {1U}),
        RouteDirection::first_to_second,
        stable_edge_id(make_edge({0U}, {1U})),
    };
}

[[nodiscard]] bool diagnostic_sequences_equal(
    const GenerationResult& left,
    const GenerationResult& right)
{
    if (left.diagnostics.size() != right.diagnostics.size()) {
        return false;
    }
    for (std::size_t index{}; index < left.diagnostics.size(); ++index) {
        const GenerationDiagnostic& first{left.diagnostics[index]};
        const GenerationDiagnostic& second{right.diagnostics[index]};
        if (first.attempt_index != second.attempt_index
            || first.attempt_seed != second.attempt_seed
            || first.outcome != second.outcome || first.message != second.message
            || !(first.mechanical_failure == second.mechanical_failure)) {
            return false;
        }
    }
    return true;
}

void default_report_succeeds(const std::filesystem::path&)
{
    const ReachabilityFixture fixture;
    const MechanicalReachabilityReport report{validate(fixture)};
    require(report.accepted, "default generated cave was mechanically rejected");
    require(report.issues.empty(), "successful report contains issues");
    require(report.start_chamber.has_value(), "successful report has no Start");
    require(
        report.reachable_chambers.size()
            == fixture.generation.generation.topology.nodes.size(),
        "not every chamber is reachable");
}

void directed_graph_is_stable(const std::filesystem::path&)
{
    const ReachabilityFixture fixture;
    const MechanicalReachabilityReport report{validate(fixture)};
    require(
        report.directed_routes.size()
            == fixture.generation.generation.topology.routes.size() * 2U,
        "directed graph does not have two verdicts per route");
    for (std::size_t index{}; index < report.directed_routes.size(); index += 2U) {
        const DirectedRouteTraversal& forward{report.directed_routes[index]};
        const DirectedRouteTraversal& reverse{report.directed_routes[index + 1U]};
        require(forward.edge == reverse.edge, "direction pair ordering changed");
        require(
            forward.direction == RouteDirection::first_to_second
                && reverse.direction == RouteDirection::second_to_first,
            "route directions are not stable");
        if (index != 0U) {
            require(
                report.directed_routes[index - 2U].stable_object_id
                    < forward.stable_object_id,
                "route object ordering is not stable");
        }
    }
}

void repeated_validation_is_identical(const std::filesystem::path&)
{
    const ReachabilityFixture fixture;
    require(validate(fixture) == validate(fixture), "repeated reports differ");
}

void required_roles_are_reachable(const std::filesystem::path&)
{
    const ReachabilityFixture fixture;
    const MechanicalReachabilityReport report{validate(fixture)};
    std::array<bool, 5> elements{};
    bool exit{};
    for (const ChamberNode& node : fixture.generation.generation.topology.nodes) {
        require(
            std::binary_search(
                report.reachable_chambers.begin(),
                report.reachable_chambers.end(),
                node.id),
            "required chamber is absent from reachable set");
        if (node.element.has_value()) {
            elements[static_cast<std::size_t>(*node.element)] = true;
        }
        exit = exit || node.role == ChamberRole::exit;
    }
    require(
        std::all_of(elements.begin(), elements.end(), [](const bool found) {
            return found;
        }),
        "not all five elemental chambers exist");
    require(exit, "Exit chamber does not exist");
}

void neutral_is_required_when_present(const std::filesystem::path&)
{
    ReachabilityFixture fixture;
    const auto candidate{std::find_if(
        fixture.generation.generation.topology.nodes.begin(),
        fixture.generation.generation.topology.nodes.end(),
        [](const ChamberNode& node) {
            return node.role == ChamberRole::elemental;
        })};
    require(
        candidate != fixture.generation.generation.topology.nodes.end(),
        "fixture has no role to convert");
    candidate->role = ChamberRole::neutral;
    candidate->element.reset();
    const MechanicalReachabilityReport report{validate(fixture)};
    require(
        std::binary_search(
            report.reachable_chambers.begin(),
            report.reachable_chambers.end(),
            candidate->id),
        "Neutral chamber was not required and reachable");
}

void disconnected_required_chamber_is_rejected(const std::filesystem::path&)
{
    ReachabilityFixture fixture;
    const NodeId start{start_node(fixture.generation.generation.topology).id};
    std::vector<std::uint32_t> degree(
        fixture.generation.generation.topology.nodes.size());
    for (const Edge edge : fixture.generation.generation.topology.edges) {
        ++degree[edge.first.value];
        ++degree[edge.second.value];
    }
    const auto leaf{std::find_if(
        fixture.generation.generation.topology.nodes.begin(),
        fixture.generation.generation.topology.nodes.end(),
        [&](const ChamberNode& node) {
            return node.id != start && degree[node.id.value] == 1U;
        })};
    require(
        leaf != fixture.generation.generation.topology.nodes.end(),
        "fixture has no non-Start leaf");
    const auto edge{std::find_if(
        fixture.generation.generation.topology.edges.begin(),
        fixture.generation.generation.topology.edges.end(),
        [&](const Edge candidate) {
            return candidate.first == leaf->id || candidate.second == leaf->id;
        })};
    RouteCollisionRegion& route{collision_route(fixture, *edge)};
    route.directed[0].route_to_chamber_supported = false;
    route.directed[1].chamber_to_junction_supported = false;
    const MechanicalReachabilityReport report{validate(fixture)};
    require(!report.accepted, "disconnected required chamber was accepted");
    require(
        std::find(
            report.required_unreachable_chambers.begin(),
            report.required_unreachable_chambers.end(),
            leaf->id)
            != report.required_unreachable_chambers.end(),
        "disconnected chamber was not reported");
}

void missing_start_is_rejected(const std::filesystem::path&)
{
    ReachabilityFixture fixture;
    auto& topology{fixture.generation.generation.topology};
    const NodeId start{start_node(topology).id};
    const auto found{std::find_if(
        topology.nodes.begin(), topology.nodes.end(),
        [start](const ChamberNode& node) {
            return node.id == start;
        })};
    found->role = ChamberRole::neutral;
    const MechanicalReachabilityReport report{validate(fixture)};
    require(
        !report.accepted && has_failure(report, ReachabilityFailure::missing_start),
        "missing Start was accepted");
}

void multiple_starts_are_rejected(const std::filesystem::path&)
{
    ReachabilityFixture fixture;
    auto& nodes{fixture.generation.generation.topology.nodes};
    const auto other{std::find_if(
        nodes.begin(), nodes.end(),
        [](const ChamberNode& node) {
            return node.role != ChamberRole::start;
        })};
    other->role = ChamberRole::start;
    other->element.reset();
    const MechanicalReachabilityReport report{validate(fixture)};
    require(
        !report.accepted && has_failure(report, ReachabilityFailure::multiple_starts),
        "multiple Starts were accepted");
}

void every_route_is_bidirectional(const std::filesystem::path&)
{
    const ReachabilityFixture fixture;
    const MechanicalReachabilityReport report{validate(fixture)};
    require(
        std::all_of(
            report.directed_routes.begin(), report.directed_routes.end(),
            [](const DirectedRouteTraversal& route) {
                return route.traversable;
            }),
        "a generated direction is not traversable");
}

void guaranteed_loop_is_bidirectional(const std::filesystem::path&)
{
    const ReachabilityFixture fixture;
    const MechanicalReachabilityReport report{validate(fixture)};
    for (const RouteDescriptor& route : fixture.generation.generation.topology.routes) {
        if (!route.on_guaranteed_cycle) {
            continue;
        }
        const std::size_t count{static_cast<std::size_t>(std::count_if(
            report.directed_routes.begin(), report.directed_routes.end(),
            [&](const DirectedRouteTraversal& traversal) {
                return traversal.edge == route.edge && traversal.traversable;
            }))};
        require(count == 2U, "protected loop route is not bidirectional");
    }
}

void guaranteed_loop_has_alternate_paths(const std::filesystem::path&)
{
    const ReachabilityFixture fixture;
    const MechanicalReachabilityReport report{validate(fixture)};
    const std::vector<NodeId>& cycle{
        fixture.generation.generation.topology.guaranteed_cycle};
    require(cycle.size() >= 3U, "guaranteed loop is too small");
    const Edge omitted{make_edge(cycle[0], cycle[1])};
    std::vector<NodeId> reached{cycle[0]};
    bool changed{true};
    while (changed) {
        changed = false;
        for (const DirectedRouteTraversal& route : report.directed_routes) {
            if (!route.traversable || route.edge == omitted
                || std::find(reached.begin(), reached.end(), route.from) == reached.end()
                || std::find(reached.begin(), reached.end(), route.to) != reached.end()) {
                continue;
            }
            reached.push_back(route.to);
            changed = true;
        }
    }
    require(
        std::find(reached.begin(), reached.end(), cycle[1]) != reached.end(),
        "guaranteed loop has no alternate mechanical path");
}

void bridge_is_bidirectional(const std::filesystem::path&)
{
    const ReachabilityFixture fixture;
    const MechanicalReachabilityReport report{validate(fixture)};
    require(
        fixture.generation.scene.bridge_routes.size() == 1U,
        "fixture bridge count changed");
    const Edge bridge{fixture.generation.scene.bridge_routes.front()};
    require(
        std::count_if(
            report.directed_routes.begin(), report.directed_routes.end(),
            [bridge](const DirectedRouteTraversal& traversal) {
                return traversal.edge == bridge && traversal.bridge
                    && traversal.traversable;
            })
            == 2,
        "wooden bridge is not safe in both directions");
}

void unsafe_bridge_is_rejected(const std::filesystem::path&)
{
    ReachabilityFixture fixture;
    RouteCollisionRegion& bridge{collision_route(
        fixture, fixture.generation.scene.bridge_routes.front())};
    bridge.rail_height_metres = 0.0;
    const MechanicalReachabilityReport report{validate(fixture)};
    require(
        !report.accepted && has_failure(report, ReachabilityFailure::unsafe_bridge),
        "unsafe bridge rail was accepted");
}

void chamber_junction_seam_is_required(const std::filesystem::path&)
{
    ReachabilityFixture fixture;
    fixture.collision.routes.front()
        .directed[0].chamber_to_junction_supported = false;
    require(
        has_failure(
            validate(fixture),
            ReachabilityFailure::unsupported_chamber_junction_seam),
        "broken chamber-junction seam was accepted");
}

void junction_route_seam_is_required(const std::filesystem::path&)
{
    ReachabilityFixture fixture;
    fixture.collision.routes.front()
        .directed[0].junction_to_route_supported = false;
    require(
        has_failure(
            validate(fixture),
            ReachabilityFailure::unsupported_junction_route_seam),
        "broken junction-route seam was accepted");
}

void route_chamber_seam_is_required(const std::filesystem::path&)
{
    ReachabilityFixture fixture;
    fixture.collision.routes.front()
        .directed[0].route_to_chamber_supported = false;
    require(
        has_failure(
            validate(fixture),
            ReachabilityFailure::unsupported_route_chamber_seam),
        "broken route-chamber seam was accepted");
}

void slope_boundary_is_exact(const std::filesystem::path&)
{
    ReachabilityFixture fixture;
    fixture.collision.routes.front().maximum_slope_millidegrees = 35'000;
    require(validate(fixture).accepted, "exactly 35 degrees was rejected");
    fixture.collision.routes.front().maximum_slope_millidegrees = 35'001;
    require(
        has_failure(validate(fixture), ReachabilityFailure::excessive_slope),
        "slope above 35 degrees was accepted");
}

void step_boundary_is_exact(const std::filesystem::path&)
{
    ReachabilityFixture fixture;
    fixture.collision.routes.front()
        .directed[0].maximum_step_up_millimetres = 300;
    require(validate(fixture).accepted, "exactly 300 mm step was rejected");
    fixture.collision.routes.front()
        .directed[0].maximum_step_up_millimetres = 301;
    require(
        has_failure(validate(fixture), ReachabilityFailure::excessive_step),
        "step above 300 mm was accepted");
}

void gap_boundary_is_exact(const std::filesystem::path&)
{
    ReachabilityFixture fixture;
    fixture.collision.routes.front().directed[0].maximum_gap_millimetres = 1'200;
    require(validate(fixture).accepted, "exactly 1,200 mm gap was rejected");
    fixture.collision.routes.front().directed[0].maximum_gap_millimetres = 1'201;
    require(
        has_failure(validate(fixture), ReachabilityFailure::excessive_gap),
        "gap above 1,200 mm was accepted");
}

void landing_boundary_is_exact(const std::filesystem::path&)
{
    ReachabilityFixture fixture;
    fixture.collision.routes.front()
        .directed[0].minimum_landing_width_millimetres = 1'500;
    require(validate(fixture).accepted, "exactly 1,500 mm landing was rejected");
    fixture.collision.routes.front()
        .directed[0].minimum_landing_width_millimetres = 1'499;
    require(
        has_failure(
            validate(fixture),
            ReachabilityFailure::insufficient_landing_width),
        "landing below 1,500 mm was accepted");
}

void clearance_boundaries_are_exact(const std::filesystem::path&)
{
    ReachabilityFixture fixture;
    RouteCollisionRegion& route{fixture.collision.routes.front()};
    route.clearance_width_millimetres = 1'500;
    route.clearance_height_millimetres = 2'300;
    require(validate(fixture).accepted, "minimum width and height were rejected");
    route.clearance_width_millimetres = 1'499;
    require(
        has_failure(
            validate(fixture),
            ReachabilityFailure::insufficient_clearance_width),
        "narrow route was accepted");
    route.clearance_width_millimetres = 1'500;
    route.clearance_height_millimetres = 2'299;
    require(
        has_failure(
            validate(fixture),
            ReachabilityFailure::insufficient_clearance_height),
        "low route was accepted");
}

void non_finite_sample_is_rejected(const std::filesystem::path&)
{
    ReachabilityFixture fixture;
    fixture.collision.routes.front().samples.front().position_metres.x =
        std::numeric_limits<double>::quiet_NaN();
    require(
        has_failure(
            validate(fixture),
            ReachabilityFailure::non_finite_collision_data),
        "non-finite route sample was accepted");
}

void invalid_bounds_are_rejected(const std::filesystem::path&)
{
    ReachabilityFixture fixture;
    fixture.collision.fall_regions.front().bounds.minimum_metres.x =
        fixture.collision.fall_regions.front().bounds.maximum_metres.x + 1.0;
    require(
        has_failure(
            validate(fixture),
            ReachabilityFailure::invalid_collision_world),
        "invalid collision bounds were accepted");
}

void unsafe_respawn_is_rejected(const std::filesystem::path&)
{
    ReachabilityFixture fixture;
    const ChamberNode& start{
        start_node(fixture.generation.generation.topology)};
    const auto chamber{std::find_if(
        fixture.collision.chambers.begin(), fixture.collision.chambers.end(),
        [start](const ChamberCollisionRegion& candidate) {
            return candidate.chamber_id == start.id;
        })};
    fixture.collision.fall_regions.front().bounds = {
        {chamber->center_metres.x - 0.5, chamber->floor_height_metres - 0.5,
            chamber->center_metres.z - 0.5},
        {chamber->center_metres.x + 0.5, chamber->floor_height_metres + 0.5,
            chamber->center_metres.z + 0.5},
    };
    require(
        has_failure(validate(fixture), ReachabilityFailure::unsafe_respawn),
        "respawn inside a fall region was accepted");
}

void failure_order_and_format_are_deterministic(const std::filesystem::path&)
{
    ReachabilityFixture fixture;
    RouteCollisionRegion& route{fixture.collision.routes.front()};
    route.clearance_width_millimetres = 0;
    route.clearance_height_millimetres = 0;
    route.maximum_slope_millidegrees = 40'000;
    const MechanicalReachabilityReport first{validate(fixture)};
    const MechanicalReachabilityReport second{validate(fixture)};
    require(first == second, "failure report ordering changed");
    require(
        first.directed_routes.front().failures.size() >= 3U
            && first.directed_routes.front().failures[0]
                == ReachabilityFailure::insufficient_clearance_width
            && first.directed_routes.front().failures[1]
                == ReachabilityFailure::insufficient_clearance_height
            && first.directed_routes.front().failures[2]
                == ReachabilityFailure::excessive_slope,
        "typed failure order changed");
    const std::string formatted{format_reachability_issue(first.issues.front())};
    require(
        formatted.find("insufficient_clearance_width")
            != std::string::npos,
        "diagnostic omitted typed failure");
}

void mechanical_rejection_retries(const std::filesystem::path&)
{
    CaveGenerationTestSeams seams;
    bool rejected{};
    seams.reject_mechanical = [&rejected](
                                  const std::uint32_t attempt,
                                  const bool fallback,
                                  const MechanicalReachabilityReport&)
        -> std::optional<ReachabilityIssue> {
        static_cast<void>(attempt);
        if (!fallback && !rejected) {
            rejected = true;
            return forced_issue();
        }
        return std::nullopt;
    };
    const CaveGenerationResult result{generate_cave({42U}, seams)};
    require(result.reachability.accepted, "retry did not produce an accepted cave");
    require(
        std::any_of(
            result.generation.diagnostics.begin(),
            result.generation.diagnostics.end(),
            [](const GenerationDiagnostic& diagnostic) {
                return diagnostic.mechanical_failure.has_value();
            }),
        "mechanical retry did not retain a typed diagnostic");
}

void eight_rejections_use_checked_fallback(const std::filesystem::path&)
{
    CaveGenerationTestSeams seams;
    seams.reject_mechanical = [](
                                  const std::uint32_t,
                                  const bool fallback,
                                  const MechanicalReachabilityReport&)
        -> std::optional<ReachabilityIssue> {
        return fallback ? std::nullopt
                        : std::optional<ReachabilityIssue>{forced_issue()};
    };
    const CaveGenerationResult result{generate_cave({42U}, seams)};
    require(result.generation.used_fallback, "eight rejections did not use fallback");
    require(
        result.generation.diagnostics.size()
            == topology_limits.normal_attempt_count + 1U,
        "normal attempt limit changed");
    require(result.reachability.accepted, "checked fallback was not accepted");
}

void invalid_mechanical_fallback_fails_atomically(const std::filesystem::path&)
{
    CaveGenerationTestSeams seams;
    seams.reject_mechanical = [](
                                  const std::uint32_t,
                                  const bool,
                                  const MechanicalReachabilityReport&)
        -> std::optional<ReachabilityIssue> {
        return forced_issue();
    };
    require_throws<GenerationError>(
        [&] {
            static_cast<void>(generate_cave({42U}, seams));
        },
        "fallback cave failed mechanical reachability",
        "invalid mechanical fallback did not fail atomically");
}

void known_good_fallback_is_mechanically_valid(const std::filesystem::path&)
{
    const TopologyData topology{known_good_fallback_topology()};
    const CaveSceneData scene{
        build_cave_scene(topology, fallback_effective_seed)};
    const CollisionWorld collision{build_collision_world(scene)};
    require(
        validate_mechanical_reachability(topology, scene, collision).accepted,
        "known-good fallback failed mechanical validation");
}

void same_seed_repeats_complete_acceptance(const std::filesystem::path&)
{
    const CaveGenerationResult first{generate_cave({42U})};
    const CaveGenerationResult second{generate_cave({42U})};
    require(
        first.generation.effective_seed == second.generation.effective_seed
            && first.generation.attempt_seed == second.generation.attempt_seed
            && first.generation.used_fallback == second.generation.used_fallback
            && first.generation.fingerprint == second.generation.fingerprint
            && first.scene.fingerprint == second.scene.fingerprint
            && first.reachability == second.reachability
            && diagnostic_sequences_equal(first.generation, second.generation),
        "same requested seed changed complete acceptance");
}

void fixed_seed_corpus_is_bounded_and_repeatable(const std::filesystem::path&)
{
    constexpr std::array<Seed, 5> corpus{
        Seed{1U}, Seed{2U}, Seed{3U}, Seed{42U}, Seed{123'456'789U}};
    for (const Seed seed : corpus) {
        const CaveGenerationResult first{generate_cave(seed)};
        const CaveGenerationResult second{generate_cave(seed)};
        require(
            first.reachability.accepted && second.reachability.accepted,
            "corpus cave failed reachability");
        require(
            first.generation.effective_seed == second.generation.effective_seed
                && first.generation.used_fallback == second.generation.used_fallback
                && first.generation.diagnostics.size()
                    == second.generation.diagnostics.size()
                && first.reachability == second.reachability,
            "corpus acceptance is not deterministic");
    }
}

void accepted_and_fallback_golden_seeds_hold(const std::filesystem::path&)
{
    const CaveGenerationResult accepted{generate_cave({42U})};
    const CaveGenerationResult fallback{generate_cave({123'456'789U})};
    require(!accepted.generation.used_fallback, "seed 42 no longer accepts normally");
    require(
        accepted.scene.fingerprint == 0x9fb15c446b74730dULL,
        "seed 42 scene fingerprint changed");
    require(
        fallback.generation.used_fallback
            && fallback.generation.effective_seed == fallback_effective_seed
            && fallback.generation.diagnostics.size()
                == topology_limits.normal_attempt_count + 1U,
        "fallback golden seed contract changed");
}

}  // namespace

std::vector<TestCase> reachability_test_cases()
{
    return {
        {"default mechanical reachability succeeds", default_report_succeeds},
        {"directed traversal graph is stable", directed_graph_is_stable},
        {"repeated reachability validation is identical", repeated_validation_is_identical},
        {"Start reaches required chamber roles", required_roles_are_reachable},
        {"Neutral is required when present", neutral_is_required_when_present},
        {"disconnected required chamber is rejected", disconnected_required_chamber_is_rejected},
        {"missing Start is rejected", missing_start_is_rejected},
        {"multiple Starts are rejected", multiple_starts_are_rejected},
        {"every generated route is bidirectional", every_route_is_bidirectional},
        {"guaranteed loop is bidirectional", guaranteed_loop_is_bidirectional},
        {"guaranteed loop has alternate paths", guaranteed_loop_has_alternate_paths},
        {"wooden bridge is bidirectional", bridge_is_bidirectional},
        {"unsafe bridge is rejected", unsafe_bridge_is_rejected},
        {"chamber-junction seam is required", chamber_junction_seam_is_required},
        {"junction-route seam is required", junction_route_seam_is_required},
        {"route-chamber seam is required", route_chamber_seam_is_required},
        {"slope boundary is exact", slope_boundary_is_exact},
        {"step boundary is exact", step_boundary_is_exact},
        {"gap boundary is exact", gap_boundary_is_exact},
        {"landing boundary is exact", landing_boundary_is_exact},
        {"clearance boundaries are exact", clearance_boundaries_are_exact},
        {"non-finite sample is rejected", non_finite_sample_is_rejected},
        {"invalid bounds are rejected", invalid_bounds_are_rejected},
        {"unsafe respawn is rejected", unsafe_respawn_is_rejected},
        {"failure ordering and format are deterministic", failure_order_and_format_are_deterministic},
        {"mechanical rejection retries", mechanical_rejection_retries},
        {"eight mechanical rejections use fallback", eight_rejections_use_checked_fallback},
        {"invalid mechanical fallback fails atomically", invalid_mechanical_fallback_fails_atomically},
        {"known-good fallback is mechanically valid", known_good_fallback_is_mechanically_valid},
        {"same seed repeats complete acceptance", same_seed_repeats_complete_acceptance},
        {"fixed-seed corpus is bounded and repeatable", fixed_seed_corpus_is_bounded_and_repeatable},
        {"accepted and fallback golden seeds hold", accepted_and_fallback_golden_seeds_hold},
    };
}

}  // namespace crystalbound::test
