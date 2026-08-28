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
#include "crystalbound/AuthoredChamber.hpp"
#include "crystalbound/ChamberTemplates.hpp"
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
    const CommandLineOptions profile{
        parse_command_line({"--seed", "42", "--profile-seconds", "300", "--profile-no-vsync"})};
    require(
        profile.profile_seconds == 300U,
        "profile duration must accept a strict positive integer");
    require(profile.profile_no_vsync, "uncapped profile mode was not recorded");

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
        {"--profile-seconds"},
        {"--profile-seconds", "0"},
        {"--profile-seconds", "-1"},
        {"--profile-seconds", "3601"},
        {"--profile-seconds", "1", "--profile-seconds", "2"},
        {"--profile-no-vsync"},
        {"--profile-seconds", "1", "--profile-no-vsync", "--profile-no-vsync"},
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
    constexpr std::uint64_t expected_fingerprint{0x93D42AEFF939F0CBULL};
    require(
        result.fingerprint == expected_fingerprint,
        "fixed topology fingerprint changed; actual="
            + format_fingerprint(result.fingerprint));
}

void seed_corpus_satisfies_graph_contract(const std::filesystem::path&)
{
    const TopologyData expected{generate_topology({0U}).topology};
    std::set<std::uint64_t> fingerprints;

    for (std::uint64_t seed{}; seed < 256U; ++seed) {
        const GenerationResult result{generate_topology({seed})};
        const TopologyData& topology = result.topology;
        const std::vector<std::string> errors{validate_topology(topology)};
        require(
            errors.empty(),
            "seed " + std::to_string(seed) + " failed structural validation");
        require(!result.used_fallback, "ordinary corpus seed unexpectedly used fallback");
        require(topology == expected,
            "seed changed the fixed macro layout");
        require(topology.nodes.size() == 7U,
            "fixed corpus topology must contain seven chambers");
        require(topology.guaranteed_cycle.empty(),
            "fixed corpus topology unexpectedly contains a cycle");
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
        const std::optional<std::uint32_t> exit_distance{
            distance_between(topology, start.id, exit.id)};
        require(exit_distance.has_value() && *exit_distance == 6U,
            "Exit must follow all five elemental chambers");
        std::uint32_t farthest{};
        for (const ChamberNode& node : topology.nodes) {
            const std::optional<std::uint32_t> distance{
                distance_between(topology, start.id, node.id)};
            require(distance.has_value(), "Start must reach every corpus chamber");
            farthest = std::max(farthest, *distance);
        }
        require(*exit_distance == farthest,
            "Exit must be the final chamber in the fixed route");
        fingerprints.insert(result.fingerprint);
    }

    require(fingerprints.size() > 240U, "seed corpus lacks meaningful variation");
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

void generated_layout_reserves_final_room_envelopes(const std::filesystem::path&)
{
    const std::int64_t minimum_separation{
        static_cast<std::int64_t>(
            layout_capacity_contract.maximum_chamber_outer_radius_millimetres)
            * 2
        + layout_capacity_contract.chamber_safety_separation_millimetres};
    const std::int64_t minimum_squared{minimum_separation * minimum_separation};
    for (std::uint64_t seed{}; seed < 256U; ++seed) {
        const TopologyData topology{generate_topology_attempt({seed})};
        for (std::size_t left{}; left < topology.nodes.size(); ++left) {
            for (std::size_t right{left + 1U}; right < topology.nodes.size(); ++right) {
                const std::int64_t dx{
                    static_cast<std::int64_t>(topology.nodes[right].anchor.x_millimetres)
                    - topology.nodes[left].anchor.x_millimetres};
                const std::int64_t dz{
                    static_cast<std::int64_t>(topology.nodes[right].anchor.z_millimetres)
                    - topology.nodes[left].anchor.z_millimetres};
                require(
                    dx * dx + dz * dz >= minimum_squared,
                    "generated anchors do not reserve final chamber envelopes");
            }
        }
    }

    TopologyData invalid{generate_topology_attempt({42U})};
    invalid.nodes[1].anchor.x_millimetres = invalid.nodes[0].anchor.x_millimetres;
    invalid.nodes[1].anchor.z_millimetres = invalid.nodes[0].anchor.z_millimetres;
    require(
        contains_error(validate_layout_capacity(invalid), "final-size separation"),
        "layout-only feasibility accepted overlapping final chamber envelopes");
}

void template_extrema_math_is_exact(const std::filesystem::path&)
{
    const CrystalScaleExtrema fire{crystal_scale_extrema(
        {310, 1'050, {-20, 20, -7, 11, 3}, 1'000U, 940U, 1'080U})};
    require(fire.minimum_diameter_millimetres == 620,
        "crystal minimum diameter must use mathematical floor");
    require(fire.maximum_diameter_millimetres == 713,
        "crystal maximum diameter must use mathematical ceiling");
    require(fire.minimum_height_millimetres == 987
            && fire.maximum_height_millimetres == 1'134,
        "crystal height extrema changed");

    const CrystalScaleExtrema scaled{crystal_scale_extrema(
        {400, 900, {-30, -20, -10}, 875U, 940U, 1'080U})};
    require(scaled.minimum_diameter_millimetres == 641
            && scaled.maximum_diameter_millimetres == 738,
        "non-1000 scale or all-negative offsets were clamped");
    require_throws<std::invalid_argument>(
        [] { static_cast<void>(crystal_scale_extrema(
            {10, 900, {-10, -20}, 1'000U, 940U, 1'080U})); },
        "positive", "non-positive mesh radius must fail");
    require_throws<std::overflow_error>(
        [] { static_cast<void>(crystal_scale_extrema(
            {std::numeric_limits<std::int32_t>::max(),
                std::numeric_limits<std::int32_t>::max(), {0},
                std::numeric_limits<std::uint32_t>::max(), 940U,
                std::numeric_limits<std::uint32_t>::max()})); },
        "overflow", "crystal scale multiplication must fail closed on overflow");
}

void route_socket_assignment_is_canonical(const std::filesystem::path&)
{
    const TopologyData topology{generate_topology_attempt({42U})};
    const auto first{assign_template_sockets(topology)};
    const auto second{assign_template_sockets(topology)};
    require(template_gameplay_fingerprint(topology, first)
            == template_gameplay_fingerprint(topology, second),
        "canonical socket assignment changed on replay");
    require(validate_template_socket_assignments(topology, first).empty(),
        "canonical socket assignment is not realizable");

    const TemplatePoint2 fallback_vector{-14'059, -33'941};
    require(template_socket_accepts_vector({0, -1'000'000}, fallback_vector),
        "22,501-millidegree fallback adapter allowance was lost");
    const std::int64_t dot{33'941'000'000LL};
    const std::int64_t cross{14'059'000'000LL};
    require(cross * 1'000'000LL <= dot * 414'235LL,
        "committed socket tangent inequality changed");
    require(cross * 1'000'000LL > dot * 414'214LL,
        "fallback adapter allowance is broader than its one-millidegree correction");

    const TopologyData fallback{known_good_fallback_topology()};
    const auto fallback_assignments{assign_template_sockets(fallback)};
    require(validate_template_socket_assignments(fallback, fallback_assignments).empty(),
        "checked fallback socket adapters are not realizable");
}

void all_room_templates_connect_legal_sockets(const std::filesystem::path&)
{
    require(validate_all_chamber_templates().empty(),
        "authoritative chamber template table failed validation");
    for (const ChamberTemplate& chamber : chamber_templates()) {
        require(chamber.sockets.size() == 8U,
            "room template omitted a canonical route socket");
        require(chamber.navigation_edges.size() >= chamber.sockets.size(),
            "room template does not connect every socket landing");
    }
}

void template_hazards_and_clear_zones_never_overlap(const std::filesystem::path&)
{
    for (const ChamberTemplate& chamber : chamber_templates()) {
        const auto errors{validate_chamber_template(chamber)};
        require(!contains_error(errors, "hazards and clear zones overlap"),
            "safe landing, checkpoint, or interaction zone overlaps a hazard");
    }
    ChamberTemplate mutated{chamber_template(ChamberTemplateRole::fire)};
    mutated.hazards.front().clockwise_polygon =
        {{-500, -500}, {-500, 500}, {500, 500}, {500, -500}};
    mutated.hazards.front().maximum_y_millimetres = 600;
    require(contains_error(validate_chamber_template(mutated),
                "hazards and clear zones overlap"),
        "hazard mutation did not make template validation red");
}

void rotated_template_footprints_reserve_separation(const std::filesystem::path&)
{
    require(rotate_template_point({1'000, 0}, 1U)
            == TemplatePoint2{707, 707},
        "Q30 octant rotation changed");
    require(rotate_template_point({1'000, 0}, 7U)
            == TemplatePoint2{707, -707},
        "signed round-half-away Q30 rotation changed");
    for (std::uint64_t seed{}; seed < 256U; ++seed) {
        require(validate_layout_capacity(generate_topology_attempt({seed})).empty(),
            "rotated final template envelopes exceeded layout capacity");
    }
}

void authored_water_playable_core_reserves_world_space(const std::filesystem::path&)
{
    for (std::uint64_t seed{}; seed < 256U; ++seed) {
        const TopologyData topology{generate_topology_attempt({seed})};
        const auto water{std::find_if(
            topology.nodes.begin(), topology.nodes.end(),
            [](const ChamberNode& node) {
                return node.element == std::optional<Element>{Element::water};
            })};
        require(water != topology.nodes.end(), "generated topology has no Water chamber");
        const std::int64_t water_radius_squared{
            static_cast<std::int64_t>(water->anchor.x_millimetres)
                    * water->anchor.x_millimetres
                + static_cast<std::int64_t>(water->anchor.z_millimetres)
                    * water->anchor.z_millimetres};
        const std::int64_t required_anchor_radius{
            authored_water_anchor_radius_millimetres - 2};
        require(
            water_radius_squared
                >= required_anchor_radius * required_anchor_radius,
            "authored Water chamber was not moved to its reserved outer anchor");
        for (const ChamberNode& other : topology.nodes) {
            if (other.id == water->id) {
                continue;
            }
            const std::int64_t x{
                static_cast<std::int64_t>(other.anchor.x_millimetres)
                - water->anchor.x_millimetres};
            const std::int64_t z{
                static_cast<std::int64_t>(other.anchor.z_millimetres)
                - water->anchor.z_millimetres};
            const std::int64_t required{
                authored_water_playable_radius_millimetres
                + (other.element == std::optional<Element>{Element::earth}
                        ? authored_earth_playable_radius_millimetres
                        : other.element == std::optional<Element>{Element::air}
                            ? authored_air_playable_radius_millimetres
                            : layout_capacity_contract.maximum_chamber_outer_radius_millimetres)
                + layout_capacity_contract.chamber_safety_separation_millimetres};
            require(x * x + z * z >= required * required,
                "authored Water playable core overlaps another chamber reservation");
        }
        require(validate_layout_capacity(topology).empty(),
            "authored Water placement invalidated deterministic layout capacity");
    }
}

void authored_earth_playable_core_reserves_world_space(
    const std::filesystem::path&)
{
    for (std::uint64_t seed{}; seed < 256U; ++seed) {
        const TopologyData topology{generate_topology_attempt({seed})};
        const auto earth{std::find_if(
            topology.nodes.begin(), topology.nodes.end(),
            [](const ChamberNode& node) {
                return node.element == std::optional<Element>{Element::earth};
            })};
        require(earth != topology.nodes.end(),
            "generated topology has no Earth chamber");
        const std::int64_t earth_radius_squared{
            static_cast<std::int64_t>(earth->anchor.x_millimetres)
                    * earth->anchor.x_millimetres
                + static_cast<std::int64_t>(earth->anchor.z_millimetres)
                    * earth->anchor.z_millimetres};
        const std::int64_t required_anchor_radius{
            authored_earth_anchor_radius_millimetres - 2};
        require(earth_radius_squared
                >= required_anchor_radius * required_anchor_radius,
            "authored Earth chamber was not moved to its reserved outer anchor");
        for (const ChamberNode& other : topology.nodes) {
            if (other.id == earth->id) {
                continue;
            }
            const std::int64_t x{
                static_cast<std::int64_t>(other.anchor.x_millimetres)
                - earth->anchor.x_millimetres};
            const std::int64_t z{
                static_cast<std::int64_t>(other.anchor.z_millimetres)
                - earth->anchor.z_millimetres};
            const std::int64_t other_radius{
                other.element == std::optional<Element>{Element::water}
                    ? authored_water_playable_radius_millimetres
                    : other.element == std::optional<Element>{Element::air}
                        ? authored_air_playable_radius_millimetres
                        : layout_capacity_contract.maximum_chamber_outer_radius_millimetres};
            const std::int64_t required{
                authored_earth_playable_radius_millimetres
                + other_radius
                + layout_capacity_contract.chamber_safety_separation_millimetres};
            require(x * x + z * z >= required * required,
                "authored Earth playable core overlaps another chamber reservation");
        }
        require(validate_layout_capacity(topology).empty(),
            "authored Earth placement invalidated deterministic layout capacity");
    }
}

void authored_air_playable_core_reserves_world_space(
    const std::filesystem::path&)
{
    for (std::uint64_t seed{}; seed < 256U; ++seed) {
        const TopologyData topology{generate_topology_attempt({seed})};
        const auto air{std::find_if(
            topology.nodes.begin(), topology.nodes.end(),
            [](const ChamberNode& node) {
                return node.element == std::optional<Element>{Element::air};
            })};
        require(air != topology.nodes.end(),
            "generated topology has no Air chamber");
        const std::int64_t air_radius_squared{
            static_cast<std::int64_t>(air->anchor.x_millimetres)
                    * air->anchor.x_millimetres
                + static_cast<std::int64_t>(air->anchor.z_millimetres)
                    * air->anchor.z_millimetres};
        const std::int64_t required_anchor_radius{
            authored_air_anchor_radius_millimetres - 2};
        require(air_radius_squared
                >= required_anchor_radius * required_anchor_radius,
            "authored Air chamber was not moved to its reserved outer anchor");
        require(validate_layout_capacity(topology).empty(),
            "authored Air placement invalidated deterministic layout capacity");
    }
}

void authored_fire_playable_core_reserves_world_space(
    const std::filesystem::path&)
{
    for (std::uint64_t seed{}; seed < 256U; ++seed) {
        const TopologyData topology{generate_topology_attempt({seed})};
        const auto fire{std::find_if(
            topology.nodes.begin(), topology.nodes.end(),
            [](const ChamberNode& node) {
                return node.element == std::optional<Element>{Element::fire};
            })};
        require(fire != topology.nodes.end(),
            "generated topology has no Fire chamber");
        const std::int64_t radius_squared{
            static_cast<std::int64_t>(fire->anchor.x_millimetres)
                    * fire->anchor.x_millimetres
                + static_cast<std::int64_t>(fire->anchor.z_millimetres)
                    * fire->anchor.z_millimetres};
        const std::int64_t required{
            authored_fire_anchor_radius_millimetres - 2};
        require(radius_squared >= required * required,
            "authored Fire chamber was not moved to its reserved outer anchor");
        require(validate_layout_capacity(topology).empty(),
            "authored Fire placement invalidated deterministic layout capacity");
    }
}

void generated_entrances_share_one_floor_elevation(const std::filesystem::path&)
{
    for (std::uint64_t seed{}; seed < 256U; ++seed) {
        const GenerationResult generation{generate_topology({seed})};
        require(std::all_of(generation.topology.nodes.begin(),
                    generation.topology.nodes.end(),
                    [](const ChamberNode& node) {
                        const std::int32_t landing_height{
                            node.element == std::optional<Element>{Element::fire}
                                ? authored_fire_landing_height_millimetres
                                : node.element
                                        == std::optional<Element>{Element::water}
                                    ? authored_water_landing_height_millimetres
                                    : 0};
                        return node.anchor.elevation_millimetres
                                + landing_height
                            == 0;
                    }),
            "generated chamber entrances do not share world Y=0");
    }
}

void fixed_linear_layout_matches_exported_plan(const std::filesystem::path&)
{
    struct ExpectedNode {
        ChamberRole role;
        std::optional<Element> element;
        Anchor anchor;
    };
    const std::array<ExpectedNode, 7U> expected{{
        {ChamberRole::start, std::nullopt, {0, 0, 0, 90'000}},
        {ChamberRole::elemental, Element::fire, {0, -850, 110'000, 270'000}},
        {ChamberRole::elemental, Element::air, {100'000, 0, 110'000, 90'000}},
        {ChamberRole::elemental, Element::earth, {205'000, 0, 110'000, 180'000}},
        {ChamberRole::elemental, Element::water, {205'000, -1'650, 20'000, 270'000}},
        {ChamberRole::elemental, Element::aether, {315'000, 0, 20'000, 0}},
        {ChamberRole::exit, std::nullopt, {415'000, 0, 20'000, 0}},
    }};
    const std::vector<Edge> expected_edges{
        make_edge({0U}, {1U}), make_edge({1U}, {2U}),
        make_edge({2U}, {3U}), make_edge({3U}, {4U}),
        make_edge({4U}, {5U}), make_edge({5U}, {6U})};

    for (const Seed seed : {Seed{0U}, Seed{42U}, Seed{987'654'321U}}) {
        const TopologyData topology{generate_topology_attempt(seed)};
        require(topology.nodes.size() == expected.size(),
            "fixed layout must contain exactly seven chambers");
        for (std::size_t index{}; index < expected.size(); ++index) {
            const ChamberNode& actual{topology.nodes[index]};
            require(actual.id == NodeId{static_cast<std::uint32_t>(index)}
                    && actual.role == expected[index].role
                    && actual.element == expected[index].element
                    && actual.anchor == expected[index].anchor,
                "fixed layout chamber order, position, or rotation changed");
        }
        require(topology.edges == expected_edges,
            "fixed layout must be one Start-to-Exit chain");
        require(topology.guaranteed_cycle.empty(),
            "fixed linear layout must not retain the old cycle");
        require(topology.routes.size() == expected_edges.size(),
            "fixed layout must have one connector per adjacent chamber pair");
        require(std::all_of(topology.routes.begin(), topology.routes.end(),
                    [](const RouteDescriptor& route) {
                        return route.lateral_offset_millimetres == 0
                            && route.elevation_offset_millimetres == 0
                            && !route.on_guaranteed_cycle;
                    }),
            "temporary fixed connectors must not contain maze or elevation offsets");
    }
}

void earth_template_preserves_authored_interior_envelope(
    const std::filesystem::path&)
{
    const ChamberTemplate& earth{chamber_template(ChamberTemplateRole::earth)};
    require(earth.outer_width_millimetres >= 47'000
            && earth.outer_depth_millimetres >= 47'000,
        "Earth template does not enclose the supplied two-gateway interior");
    require(earth.usable_diameter_millimetres >= 40'000,
        "Earth template walkable floor does not preserve the authored interior");
    require(earth.usable_height_millimetres >= 13'000,
        "Earth template ceiling is not high enough for the authored vault");
    require(earth.sockets[0].vestibule_outer_millimetres
                == TemplatePoint2{23'200, 0}
            && earth.sockets[2].vestibule_outer_millimetres
                == TemplatePoint2{0, 23'200},
        "Earth template sockets no longer match Gate0 and Gate1");

    const TopologyData topology{generate_topology_attempt({42U})};
    const auto earth_node{std::find_if(topology.nodes.begin(), topology.nodes.end(),
        [](const ChamberNode& node) {
            return node.element == std::optional<Element>{Element::earth};
        })};
    require(earth_node != topology.nodes.end(),
        "seed 42 has no Earth chamber");
    const ChamberDimensionBand dimensions{
        planned_chamber_dimension_band(*earth_node)};
    require(dimensions.minimum_ring_radius_millimetres >= 24'000
            && dimensions.minimum_interior_height_millimetres >= 13'000,
        "Earth generated shell would intersect the preserved authored interior");
}

void water_template_preserves_authored_interior_and_gateways(
    const std::filesystem::path&)
{
    const ChamberTemplate& water{chamber_template(ChamberTemplateRole::water)};
    require(water.outer_width_millimetres >= 50'600
            && water.outer_depth_millimetres >= 38'600,
        "Water template does not enclose the full-scale authored room");
    require(water.usable_diameter_millimetres >= 31'000
            && water.usable_height_millimetres >= 14'000,
        "Water template does not preserve the authored floor and ceiling envelope");
    require(water.sockets[4].vestibule_outer_millimetres
                == TemplatePoint2{-25'200, 0}
            && water.sockets[2].vestibule_outer_millimetres
                == TemplatePoint2{0, 19'200},
        "Water template gateway sockets no longer match the authored entrances");
}

void fire_template_preserves_authored_interior_lava_and_gateways(
    const std::filesystem::path&)
{
    const ChamberTemplate& fire{chamber_template(ChamberTemplateRole::fire)};
    require(fire.outer_width_millimetres >= 64'000
            && fire.outer_depth_millimetres >= 64'000,
        "Fire template does not enclose the supplied authored model");
    require(fire.usable_diameter_millimetres >= 55'000
            && fire.usable_height_millimetres >= 23'000,
        "Fire template does not preserve the authored room envelope");
    require(fire.sockets[0].vestibule_outer_millimetres
                == TemplatePoint2{31'500, 0}
            && fire.sockets[2].vestibule_outer_millimetres
                == TemplatePoint2{0, 31'500},
        "Fire template sockets no longer match the two authored tunnel floors");
    require(fire.hazards.size() == 4U
            && std::all_of(fire.hazards.begin(), fire.hazards.end(),
                [](const TemplateHazardVolume& hazard) {
                    return hazard.kind == TemplateHazardKind::lava
                        && hazard.respawn
                            == TemplateRespawnPolicy::last_safe_checkpoint;
                }),
        "Fire lava does not preserve entrance-checkpoint recovery");
}

void air_template_preserves_authored_interior_and_gateways(
    const std::filesystem::path&)
{
    const ChamberTemplate& air{chamber_template(ChamberTemplateRole::air)};
    require(air.outer_width_millimetres >= 50'000
            && air.outer_depth_millimetres >= 50'000,
        "Air template does not enclose the full-scale authored model");
    require(air.usable_diameter_millimetres >= 40'000
            && air.usable_height_millimetres >= 22'000,
        "Air template does not preserve the authored floor and vertical envelope");
    require(air.sockets[2].vestibule_outer_millimetres
                == TemplatePoint2{0, 24'750}
            && air.sockets[6].vestibule_outer_millimetres
                == TemplatePoint2{0, -24'750},
        "Air template gateway sockets no longer match the authored passages");
}

void template_gameplay_fingerprint_covers_structural_mutations(
    const std::filesystem::path&)
{
    std::set<std::uint64_t> signatures;
    for (const ChamberTemplate& chamber : chamber_templates()) {
        signatures.insert(chamber_template_structural_signature(chamber));
    }
    require(signatures.size() == 8U,
        "eight room roles do not have pairwise structural signatures");

    ChamberTemplate shifted{chamber_template(ChamberTemplateRole::fire)};
    const std::uint64_t before{chamber_template_structural_signature(shifted)};
    shifted.hazards.front().clockwise_polygon.front().x_millimetres += 1;
    require(chamber_template_structural_signature(shifted) != before,
        "template gameplay fingerprint omitted a shifted hazard");
    shifted = chamber_template(ChamberTemplateRole::fire);
    shifted.sockets.pop_back();
    require(contains_error(validate_chamber_template(shifted), "eight route sockets"),
        "omitted route socket did not make validation red");
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
        {"generated layout reserves final room envelopes", generated_layout_reserves_final_room_envelopes},
        {"template extrema math is exact", template_extrema_math_is_exact},
        {"route socket assignment is canonical", route_socket_assignment_is_canonical},
        {"all room templates connect legal sockets", all_room_templates_connect_legal_sockets},
        {"template hazards and clear zones never overlap", template_hazards_and_clear_zones_never_overlap},
        {"rotated template footprints reserve separation", rotated_template_footprints_reserve_separation},
        {"authored Water playable core reserves world space",
            authored_water_playable_core_reserves_world_space},
        {"authored Earth playable core reserves world space",
            authored_earth_playable_core_reserves_world_space},
        {"authored Air playable core reserves world space",
            authored_air_playable_core_reserves_world_space},
        {"authored Fire playable core reserves world space",
            authored_fire_playable_core_reserves_world_space},
        {"generated entrances share one floor elevation",
            generated_entrances_share_one_floor_elevation},
        {"fixed linear layout matches exported plan",
            fixed_linear_layout_matches_exported_plan},
        {"Water template preserves authored interior and gateways",
            water_template_preserves_authored_interior_and_gateways},
        {"Earth template preserves authored interior envelope",
            earth_template_preserves_authored_interior_envelope},
        {"Air template preserves authored interior and gateways",
            air_template_preserves_authored_interior_and_gateways},
        {"Fire template preserves authored interior lava and gateways",
            fire_template_preserves_authored_interior_lava_and_gateways},
        {"template gameplay fingerprint covers structural mutations", template_gameplay_fingerprint_covers_structural_mutations},
    };
}

}  // namespace crystalbound::test
