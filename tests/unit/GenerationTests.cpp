#include "GenerationTests.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <queue>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "crystalbound/CommandLine.hpp"
#include "crystalbound/DeterministicRandom.hpp"
#include "crystalbound/Generation.hpp"

namespace crystalbound::test {
namespace {

class TestFailure final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

void require(const bool condition, const std::string_view message)
{
    if (!condition) {
        throw TestFailure{std::string{message}};
    }
}

template <typename Exception, typename Function>
void require_throws(
    Function&& function,
    const std::string_view expected_message,
    const std::string_view failure_message)
{
    try {
        function();
    } catch (const Exception& error) {
        if (std::string_view{error.what()}.find(expected_message) == std::string_view::npos) {
            throw TestFailure{std::string{failure_message} + ": unexpected message: "
                + error.what()};
        }
        return;
    } catch (const std::exception& error) {
        throw TestFailure{std::string{failure_message} + ": wrong exception type: "
            + error.what()};
    }
    throw TestFailure{std::string{failure_message} + ": no exception was thrown"};
}

[[nodiscard]] const ChamberNode& node_with_role(
    const TopologyData& topology,
    const ChamberRole role)
{
    const auto found = std::find_if(
        topology.nodes.begin(), topology.nodes.end(), [role](const ChamberNode& node) {
            return node.role == role;
        });
    if (found == topology.nodes.end()) {
        throw TestFailure{"required chamber role is missing"};
    }
    return *found;
}

[[nodiscard]] std::vector<NodeId> neighbours(
    const TopologyData& topology,
    const NodeId id)
{
    std::vector<NodeId> result;
    for (const Edge edge : topology.edges) {
        if (edge.first == id) {
            result.push_back(edge.second);
        } else if (edge.second == id) {
            result.push_back(edge.first);
        }
    }
    return result;
}

[[nodiscard]] std::optional<std::uint32_t> distance_between(
    const TopologyData& topology,
    const NodeId start,
    const NodeId goal)
{
    std::queue<std::pair<NodeId, std::uint32_t>> pending;
    std::vector<NodeId> visited{start};
    pending.push({start, 0U});
    while (!pending.empty()) {
        const auto [current, distance] = pending.front();
        pending.pop();
        if (current == goal) {
            return distance;
        }
        for (const NodeId neighbour : neighbours(topology, current)) {
            if (std::find(visited.begin(), visited.end(), neighbour) == visited.end()) {
                visited.push_back(neighbour);
                pending.push({neighbour, distance + 1U});
            }
        }
    }
    return std::nullopt;
}

[[nodiscard]] bool contains_error(
    const std::vector<std::string>& errors,
    const std::string_view expected)
{
    return std::any_of(errors.begin(), errors.end(), [expected](const std::string& error) {
        return error.find(expected) != std::string::npos;
    });
}

void splitmix64_matches_golden_sequence(const std::filesystem::path&)
{
    SplitMix64 random{0U};
    constexpr std::array<std::uint64_t, 5> expected{
        0xE220A8397B1DCDAFULL,
        0x6E789E6AA1B965F4ULL,
        0x06C45D188009454FULL,
        0xF88BB8A8724C81ECULL,
        0x1B39896A51A8749BULL,
    };
    for (const std::uint64_t value : expected) {
        require(random.next() == value, "SplitMix64 golden sequence changed");
    }
}

void bounded_sampling_is_deterministic(const std::filesystem::path&)
{
    SplitMix64 first{77U};
    SplitMix64 second{77U};
    for (std::size_t draw{}; draw < 256U; ++draw) {
        const std::uint64_t left{first.bounded(37U)};
        const std::uint64_t right{second.bounded(37U)};
        require(left < 37U, "bounded sample escaped its range");
        require(left == right, "bounded sampling is not repeatable");
    }
    require(first.boolean() == second.boolean(), "boolean draws must consume a stable bit");
    require_throws<std::invalid_argument>(
        [&] { static_cast<void>(first.bounded(0U)); },
        "positive bound",
        "zero bounded-sample limit must fail");
}

void substreams_are_independent(const std::filesystem::path&)
{
    SplitMix64 first{make_substream(123U, random_domain::topology, 9U)};
    SplitMix64 repeated{make_substream(123U, random_domain::topology, 9U)};
    SplitMix64 decoration{make_substream(123U, random_domain::decoration, 9U)};
    const std::uint64_t topology_value{first.next()};
    require(topology_value == repeated.next(), "same substream identity must repeat");
    require(topology_value != decoration.next(), "different domains must not share streams");
    require(
        stable_edge_id(make_edge({7U}, {2U})) == stable_edge_id(make_edge({2U}, {7U})),
        "edge stable ID must ignore endpoint order");
    const std::set<std::uint64_t> domains{
        random_domain::topology,
        random_domain::anchors,
        random_domain::routes,
        random_domain::geometry,
        random_domain::materials,
        random_domain::decoration,
        random_domain::retry,
    };
    require(domains.size() == 7U, "generation domain constants must remain distinct");
}

[[nodiscard]] std::string structural_signature(const TopologyData& topology)
{
    std::ostringstream signature;
    for (const ChamberNode& node : topology.nodes) {
        signature << node.id.value << ':' << static_cast<unsigned int>(node.role) << ':';
        if (node.element.has_value()) {
            signature << static_cast<unsigned int>(*node.element);
        } else {
            signature << '-';
        }
        signature << ';';
    }
    signature << '|';
    for (const Edge edge : topology.edges) {
        signature << edge.first.value << '-' << edge.second.value << ';';
    }
    return signature.str();
}

void command_line_accepts_strict_valid_seeds(const std::filesystem::path&)
{
    const CommandLineOptions zero{parse_command_line({"--seed", "0"})};
    require(zero.requested_seed == 0U, "zero seed must be accepted");
    const CommandLineOptions maximum{
        parse_command_line({"--seed", "18446744073709551615"})};
    require(
        maximum.requested_seed == std::numeric_limits<std::uint64_t>::max(),
        "UINT64_MAX seed must be accepted");
    require(parse_command_line({"--help"}).show_help, "--help must be recognized");
    require(parse_command_line({"-h"}).show_help, "-h must be recognized");

    bool entropy_called{};
    require(
        resolve_requested_seed(zero, [&entropy_called] {
            entropy_called = true;
            return 99U;
        }) == 0U,
        "explicit seed must win over entropy");
    require(!entropy_called, "explicit seed must not evaluate entropy source");
    const CommandLineOptions omitted{parse_command_line({})};
    require(
        resolve_requested_seed(omitted, [] { return 42U; }) == 42U,
        "omitted seed must use injected entropy source");
}

void command_line_rejects_invalid_inputs(const std::filesystem::path&)
{
    const std::vector<std::vector<std::string_view>> invalid{
        {"--seed"},
        {"--seed", ""},
        {"--seed", " "},
        {"--seed", " 1"},
        {"--seed", "1 "},
        {"--seed", "-1"},
        {"--seed", "+1"},
        {"--seed", "1x"},
        {"--seed", "18446744073709551616"},
        {"--seed", "1", "--seed", "2"},
        {"--unknown"},
        {"model.obj"},
        {"--help", "--seed", "1"},
    };
    for (const auto& arguments : invalid) {
        require_throws<CommandLineError>(
            [&] { static_cast<void>(parse_command_line(arguments)); },
            "",
            "invalid command line must fail");
    }
    require_throws<std::invalid_argument>(
        [&] {
            static_cast<void>(resolve_requested_seed(parse_command_line({}), SeedSource{}));
        },
        "seed source",
        "omitted seed without an entropy source must fail");
}

void same_seed_repeats_complete_result(const std::filesystem::path&)
{
    const GenerationResult first{generate_topology({123456789U})};
    const GenerationResult second{generate_topology({123456789U})};
    require(first.topology == second.topology, "same seed changed complete topology data");
    require(first.fingerprint == second.fingerprint, "same seed changed fingerprint");
    require(first.attempt_seed == second.attempt_seed, "same seed changed attempt seed");
    require(!first.used_fallback && !second.used_fallback, "valid fixed seed used fallback");
}

void fixed_seed_matches_golden_fingerprint(const std::filesystem::path&)
{
    const GenerationResult result{generate_topology({123456789U})};
    constexpr std::uint64_t expected_fingerprint{0x2F307424C0DCD1F4ULL};
    require(
        result.fingerprint == expected_fingerprint,
        "fixed topology fingerprint changed; actual="
            + format_fingerprint(result.fingerprint));
}

void seed_corpus_satisfies_graph_contract(const std::filesystem::path&)
{
    bool saw_neutral{};
    bool saw_without_neutral{};
    bool saw_exit_not_farthest{};
    std::set<std::uint64_t> fingerprints;
    std::set<std::string> structural_signatures;
    std::set<std::uint32_t> exit_ids;

    for (std::uint64_t seed{}; seed < 256U; ++seed) {
        const GenerationResult result{generate_topology({seed})};
        const TopologyData& topology = result.topology;
        const std::vector<std::string> errors{validate_topology(topology)};
        require(
            errors.empty(),
            "seed " + std::to_string(seed) + " failed structural validation");
        require(!result.used_fallback, "ordinary corpus seed unexpectedly used fallback");
        require(
            topology.nodes.size() == 7U || topology.nodes.size() == 8U,
            "corpus topology violated node-count bound");
        require(
            topology.guaranteed_cycle.size() >= 3U,
            "corpus topology lost its guaranteed loop");
        require(
            std::is_sorted(topology.edges.begin(), topology.edges.end()),
            "corpus edges are not canonical");

        std::set<Element> elements;
        for (const ChamberNode& node : topology.nodes) {
            if (node.element.has_value()) {
                elements.insert(*node.element);
            }
        }
        require(elements.size() == 5U, "corpus topology must contain five distinct elements");

        const ChamberNode& start{node_with_role(topology, ChamberRole::start)};
        const ChamberNode& exit{node_with_role(topology, ChamberRole::exit)};
        exit_ids.insert(exit.id.value);
        const std::optional<std::uint32_t> exit_distance{
            distance_between(topology, start.id, exit.id)};
        require(exit_distance.has_value() && *exit_distance >= 1U, "Exit is unreachable");
        std::uint32_t farthest{};
        for (const ChamberNode& node : topology.nodes) {
            const std::optional<std::uint32_t> distance{
                distance_between(topology, start.id, node.id)};
            require(distance.has_value(), "Start must reach every corpus chamber");
            farthest = std::max(farthest, *distance);
        }
        saw_exit_not_farthest = saw_exit_not_farthest || *exit_distance < farthest;
        saw_neutral = saw_neutral || topology.nodes.size() == 8U;
        saw_without_neutral = saw_without_neutral || topology.nodes.size() == 7U;
        fingerprints.insert(result.fingerprint);
        structural_signatures.insert(structural_signature(topology));
    }

    require(saw_neutral && saw_without_neutral, "corpus must exercise optional Neutral");
    require(saw_exit_not_farthest, "Exit must not always be the farthest chamber");
    require(exit_ids.size() > 1U, "Exit stable ID must vary across seeds");
    require(fingerprints.size() > 240U, "seed corpus lacks meaningful variation");
    require(
        structural_signatures.size() > 200U,
        "different seeds must vary roles or graph structure, not only fingerprints");
}

void integer_parameters_stay_in_bounds(const std::filesystem::path&)
{
    for (std::uint64_t seed{}; seed < 64U; ++seed) {
        const TopologyData topology{generate_topology({seed}).topology};
        for (const ChamberNode& node : topology.nodes) {
            require(
                node.anchor.x_millimetres >= -topology_limits.horizontal_anchor_millimetres
                    && node.anchor.x_millimetres
                        <= topology_limits.horizontal_anchor_millimetres,
                "anchor x escaped integer limits");
            require(
                node.anchor.elevation_millimetres
                        >= -topology_limits.elevation_anchor_millimetres
                    && node.anchor.elevation_millimetres
                        <= topology_limits.elevation_anchor_millimetres,
                "anchor elevation escaped integer limits");
            require(
                node.anchor.heading_millidegrees >= 0
                    && node.anchor.heading_millidegrees
                        < topology_limits.full_turn_millidegrees,
                "anchor heading escaped millidegree limits");
        }
        for (const RouteDescriptor& route : topology.routes) {
            require(
                route.lateral_offset_millimetres
                        >= -topology_limits.route_lateral_offset_millimetres
                    && route.lateral_offset_millimetres
                        <= topology_limits.route_lateral_offset_millimetres,
                "route lateral offset escaped limits");
            require(
                route.elevation_offset_millimetres
                        >= -topology_limits.route_elevation_offset_millimetres
                    && route.elevation_offset_millimetres
                        <= topology_limits.route_elevation_offset_millimetres,
                "route elevation offset escaped limits");
        }
    }
}

void validator_rejects_malformed_topologies(const std::filesystem::path&)
{
    const TopologyData valid{generate_topology({17U}).topology};

    TopologyData duplicate_id{valid};
    duplicate_id.nodes[1].id = duplicate_id.nodes[0].id;
    require(
        contains_error(validate_topology(duplicate_id), "unique"),
        "validator accepted duplicate node IDs");

    TopologyData disconnected{valid};
    disconnected.edges.clear();
    disconnected.routes.clear();
    require(
        contains_error(validate_topology(disconnected), "reach"),
        "validator accepted a disconnected graph");

    TopologyData invalid_cycle{valid};
    invalid_cycle.guaranteed_cycle.resize(2U);
    require(
        contains_error(validate_topology(invalid_cycle), "at least three"),
        "validator accepted a two-node guaranteed cycle");

    TopologyData missing_route{valid};
    missing_route.routes.pop_back();
    require(
        contains_error(validate_topology(missing_route), "exactly one route"),
        "validator accepted missing route data");

    TopologyData self_edge{valid};
    self_edge.edges.push_back(make_edge(self_edge.nodes.front().id, self_edge.nodes.front().id));
    std::sort(self_edge.edges.begin(), self_edge.edges.end());
    require(
        contains_error(validate_topology(self_edge), "distinct endpoints"),
        "validator accepted a self-edge");

    TopologyData duplicate_edge{valid};
    duplicate_edge.edges.push_back(duplicate_edge.edges.front());
    std::sort(duplicate_edge.edges.begin(), duplicate_edge.edges.end());
    require(
        contains_error(validate_topology(duplicate_edge), "duplicate edges"),
        "validator accepted a duplicate edge");

    TopologyData invalid_anchor{valid};
    invalid_anchor.nodes.front().anchor.x_millimetres =
        topology_limits.horizontal_anchor_millimetres + 1;
    require(
        contains_error(validate_topology(invalid_anchor), "anchor"),
        "validator accepted an out-of-bounds anchor");
}

void attempt_seeds_and_diagnostics_repeat(const std::filesystem::path&)
{
    const Seed requested{99U};
    require(derive_attempt_seed(requested, 0U) == requested, "attempt zero must use request");
    require(
        derive_attempt_seed(requested, 1U) == derive_attempt_seed(requested, 1U),
        "retry seed derivation must repeat");
    require(
        derive_attempt_seed(requested, 1U) != requested,
        "retry seed must differ from requested seed");
    require_throws<std::out_of_range>(
        [&] {
            static_cast<void>(derive_attempt_seed(
                requested, topology_limits.normal_attempt_count));
        },
        "0..7",
        "ninth normal attempt must be rejected");

    const GenerationResult first{generate_topology(requested)};
    const GenerationResult second{generate_topology(requested)};
    require(first.diagnostics.size() == 1U, "valid attempt should record one diagnostic");
    require(
        first.diagnostics.front().outcome == AttemptOutcome::accepted,
        "valid attempt diagnostic must be accepted");
    require(
        first.diagnostics.front().attempt_seed == second.diagnostics.front().attempt_seed
            && first.diagnostics.front().message == second.diagnostics.front().message,
        "attempt diagnostics must repeat");
}

[[nodiscard]] GenerationTestSeams reject_every_attempt()
{
    GenerationTestSeams seams;
    seams.reject_attempt = [](const std::uint32_t, const TopologyData&) {
        return std::optional<std::string>{"forced deterministic rejection"};
    };
    return seams;
}

void eight_rejections_use_valid_fallback(const std::filesystem::path&)
{
    const GenerationResult result{generate_topology({555U}, reject_every_attempt())};
    require(result.used_fallback, "forced rejections must activate fallback");
    require(
        result.attempt_seed == fallback_effective_seed
            && result.effective_seed == fallback_effective_seed,
        "fallback must expose its effective seed");
    require(
        result.diagnostics.size() == topology_limits.normal_attempt_count + 1U,
        "fallback must follow exactly eight normal attempts");
    for (std::uint32_t index{}; index < topology_limits.normal_attempt_count; ++index) {
        const GenerationDiagnostic& diagnostic{result.diagnostics[index]};
        require(diagnostic.attempt_index == index, "fallback diagnostics lost attempt index");
        require(
            diagnostic.attempt_seed == derive_attempt_seed({555U}, index),
            "fallback diagnostics lost derived attempt seed");
        require(
            diagnostic.outcome == AttemptOutcome::rejected,
            "forced normal attempt must be recorded as rejected");
    }
    require(
        result.diagnostics.back().outcome == AttemptOutcome::fallback_accepted,
        "fallback acceptance must be explicit");
    require(
        result.topology == known_good_fallback_topology(),
        "forced fallback must return the checked topology atomically");
    require(validate_topology(result.topology).empty(), "checked fallback must validate");
}

void invalid_fallback_fails_atomically(const std::filesystem::path&)
{
    GenerationTestSeams seams{reject_every_attempt()};
    seams.fallback_factory = [] {
        TopologyData invalid{known_good_fallback_topology()};
        invalid.nodes.clear();
        return invalid;
    };
    require_throws<GenerationError>(
        [&] { static_cast<void>(generate_topology({555U}, seams)); },
        "fallback topology failed validation",
        "invalid fallback must fail without returning partial state");
}

void movement_envelope_matches_approved_contract(const std::filesystem::path&)
{
    require(movement_envelope.capsule_radius_millimetres == 350, "capsule radius changed");
    require(movement_envelope.total_height_millimetres == 1'800, "height changed");
    require(movement_envelope.camera_height_millimetres == 1'620, "camera height changed");
    require(
        movement_envelope.walk_speed_millimetres_per_second == 3'500,
        "walk speed changed");
    require(
        movement_envelope.sprint_speed_millimetres_per_second == 5'500,
        "sprint speed changed");
    require(movement_envelope.step_height_millimetres == 300, "step height changed");
    require(movement_envelope.maximum_slope_millidegrees == 35'000, "slope changed");
    require(
        movement_envelope.gravity_millimetres_per_second_squared == 18'000,
        "gravity changed");
    require(
        movement_envelope.jump_impulse_millimetres_per_second == 5'400,
        "jump impulse changed");
    require(movement_envelope.maximum_gap_millimetres == 1'200, "gap changed");
    require(
        movement_envelope.minimum_landing_width_millimetres == 1'500,
        "landing width changed");
    require(
        movement_envelope.minimum_clearance_width_millimetres == 1'400
            && movement_envelope.minimum_clearance_height_millimetres == 2'200,
        "clearance changed");
    require(movement_envelope.safety_margin_millimetres == 100, "margin changed");
    require(movement_envelope.fixed_simulation_hertz == 120U, "fixed tick rate changed");
    require(movement_envelope.maximum_catch_up_ticks == 8U, "catch-up bound changed");
    require(
        movement_envelope.frame_delta_clamp_milliseconds == 250U,
        "frame delta clamp changed");
}

}  // namespace

std::vector<TestCase> generation_test_cases()
{
    return {
        {"SplitMix64 matches golden sequence", splitmix64_matches_golden_sequence},
        {"bounded sampling is deterministic", bounded_sampling_is_deterministic},
        {"generation substreams are independent", substreams_are_independent},
        {"CLI accepts strict valid seeds", command_line_accepts_strict_valid_seeds},
        {"CLI rejects invalid seed inputs", command_line_rejects_invalid_inputs},
        {"same seed repeats complete result", same_seed_repeats_complete_result},
        {"fixed seed matches topology fingerprint", fixed_seed_matches_golden_fingerprint},
        {"seed corpus satisfies graph contract", seed_corpus_satisfies_graph_contract},
        {"integer generation parameters stay bounded", integer_parameters_stay_in_bounds},
        {"validator rejects malformed topology", validator_rejects_malformed_topologies},
        {"attempt seeds and diagnostics repeat", attempt_seeds_and_diagnostics_repeat},
        {"eight rejections use checked fallback", eight_rejections_use_valid_fallback},
        {"invalid fallback fails atomically", invalid_fallback_fails_atomically},
        {"movement envelope matches approved contract", movement_envelope_matches_approved_contract},
    };
}

}  // namespace crystalbound::test
