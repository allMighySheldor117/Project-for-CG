#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace crystalbound {

struct Seed {
    std::uint64_t value{};
};

constexpr bool operator==(const Seed left, const Seed right) noexcept
{
    return left.value == right.value;
}

constexpr bool operator!=(const Seed left, const Seed right) noexcept
{
    return !(left == right);
}

struct GeneratorVersion {
    std::uint32_t value{};
};

inline constexpr GeneratorVersion current_generator_version{1U};

struct TopologyLimits {
    std::int32_t horizontal_anchor_millimetres{};
    std::int32_t elevation_anchor_millimetres{};
    std::int32_t route_lateral_offset_millimetres{};
    std::int32_t route_elevation_offset_millimetres{};
    std::int32_t full_turn_millidegrees{};
    std::uint32_t normal_attempt_count{};
};

inline constexpr TopologyLimits topology_limits{
    30'000,
    5'000,
    4'000,
    2'000,
    360'000,
    8U,
};

inline constexpr Seed fallback_effective_seed{0x4352595354414C42ULL};

struct NodeId {
    std::uint32_t value{};
};

constexpr bool operator==(const NodeId left, const NodeId right) noexcept
{
    return left.value == right.value;
}

constexpr bool operator!=(const NodeId left, const NodeId right) noexcept
{
    return !(left == right);
}

constexpr bool operator<(const NodeId left, const NodeId right) noexcept
{
    return left.value < right.value;
}

enum class ChamberRole : std::uint8_t {
    start,
    elemental,
    exit,
    neutral,
};

enum class Element : std::uint8_t {
    fire,
    water,
    earth,
    air,
    aether,
};

struct Anchor {
    std::int32_t x_millimetres{};
    std::int32_t elevation_millimetres{};
    std::int32_t z_millimetres{};
    std::int32_t heading_millidegrees{};
};

bool operator==(const Anchor& left, const Anchor& right) noexcept;

struct ChamberNode {
    NodeId id{};
    ChamberRole role{ChamberRole::neutral};
    std::optional<Element> element{};
    Anchor anchor{};
};

bool operator==(const ChamberNode& left, const ChamberNode& right) noexcept;

struct Edge {
    NodeId first{};
    NodeId second{};
};

enum class RouteDirection : std::uint8_t {
    first_to_second,
    second_to_first,
};

enum class ReachabilityFailure : std::uint8_t {
    missing_start,
    multiple_starts,
    invalid_collision_world,
    missing_chamber_collision,
    missing_route_collision,
    non_finite_collision_data,
    insufficient_clearance_width,
    insufficient_clearance_height,
    excessive_slope,
    excessive_step,
    excessive_gap,
    insufficient_landing_width,
    unsupported_chamber_junction_seam,
    unsupported_junction_route_seam,
    unsupported_route_chamber_seam,
    unsafe_bridge,
    unsafe_respawn,
    unstable_respawn,
    unreachable_required_chamber,
    protected_route_not_bidirectional,
    guaranteed_loop_invalid,
};

struct ReachabilityIssue {
    ReachabilityFailure failure{ReachabilityFailure::invalid_collision_world};
    std::optional<NodeId> chamber_id{};
    std::optional<Edge> edge{};
    std::optional<RouteDirection> direction{};
    std::uint64_t stable_object_id{};
};

bool operator==(const ReachabilityIssue& left, const ReachabilityIssue& right) noexcept;

struct DirectedRouteTraversal {
    Edge edge{};
    NodeId from{};
    NodeId to{};
    RouteDirection direction{RouteDirection::first_to_second};
    std::uint64_t stable_object_id{};
    bool bridge{};
    bool traversable{};
    std::vector<ReachabilityFailure> failures{};
};

bool operator==(
    const DirectedRouteTraversal& left,
    const DirectedRouteTraversal& right) noexcept;

struct ChamberRespawnVerdict {
    NodeId chamber_id{};
    std::uint64_t stable_object_id{};
    bool safe{};
    std::vector<ReachabilityFailure> failures{};
};

bool operator==(
    const ChamberRespawnVerdict& left,
    const ChamberRespawnVerdict& right) noexcept;

struct MechanicalReachabilityReport {
    bool accepted{};
    std::optional<NodeId> start_chamber{};
    std::vector<NodeId> reachable_chambers{};
    std::vector<NodeId> required_unreachable_chambers{};
    std::vector<DirectedRouteTraversal> directed_routes{};
    std::vector<ChamberRespawnVerdict> respawns{};
    std::vector<ReachabilityIssue> issues{};
    std::vector<std::string> diagnostics{};
};

bool operator==(
    const MechanicalReachabilityReport& left,
    const MechanicalReachabilityReport& right) noexcept;

bool operator==(const Edge& left, const Edge& right) noexcept;
bool operator!=(const Edge& left, const Edge& right) noexcept;
bool operator<(const Edge& left, const Edge& right) noexcept;
[[nodiscard]] Edge make_edge(NodeId left, NodeId right) noexcept;
[[nodiscard]] std::uint64_t stable_edge_id(const Edge& edge) noexcept;

struct RouteDescriptor {
    Edge edge{};
    std::int32_t lateral_offset_millimetres{};
    std::int32_t elevation_offset_millimetres{};
    std::int32_t heading_millidegrees{};
    bool on_guaranteed_cycle{};
};

bool operator==(const RouteDescriptor& left, const RouteDescriptor& right) noexcept;

struct TopologyData {
    std::vector<ChamberNode> nodes{};
    std::vector<Edge> edges{};
    std::vector<RouteDescriptor> routes{};
    std::vector<NodeId> guaranteed_cycle{};
};

bool operator==(const TopologyData& left, const TopologyData& right) noexcept;

struct MovementEnvelope {
    std::int32_t capsule_radius_millimetres{};
    std::int32_t total_height_millimetres{};
    std::int32_t camera_height_millimetres{};
    std::int32_t walk_speed_millimetres_per_second{};
    std::int32_t sprint_speed_millimetres_per_second{};
    std::int32_t step_height_millimetres{};
    std::int32_t maximum_slope_millidegrees{};
    std::int32_t gravity_millimetres_per_second_squared{};
    std::int32_t jump_impulse_millimetres_per_second{};
    std::int32_t maximum_gap_millimetres{};
    std::int32_t minimum_landing_width_millimetres{};
    std::int32_t minimum_clearance_width_millimetres{};
    std::int32_t minimum_clearance_height_millimetres{};
    std::int32_t safety_margin_millimetres{};
    std::uint32_t fixed_simulation_hertz{};
    std::uint32_t maximum_catch_up_ticks{};
    std::uint32_t frame_delta_clamp_milliseconds{};
};

inline constexpr MovementEnvelope movement_envelope{
    350,
    1'800,
    1'620,
    3'500,
    5'500,
    300,
    35'000,
    18'000,
    5'400,
    1'200,
    1'500,
    1'400,
    2'200,
    100,
    120U,
    8U,
    250U,
};

enum class AttemptOutcome : std::uint8_t {
    accepted,
    rejected,
    fallback_accepted,
};

struct GenerationDiagnostic {
    std::uint32_t attempt_index{};
    Seed attempt_seed{};
    AttemptOutcome outcome{AttemptOutcome::rejected};
    std::string message{};
    std::optional<ReachabilityIssue> mechanical_failure{};
};

struct GenerationResult {
    Seed requested_seed{};
    Seed attempt_seed{};
    Seed effective_seed{};
    GeneratorVersion generator_version{current_generator_version};
    TopologyData topology{};
    std::uint64_t fingerprint{};
    std::vector<GenerationDiagnostic> diagnostics{};
    bool used_fallback{};
};

class GenerationError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

using AttemptRejection = std::function<std::optional<std::string>(
    std::uint32_t attempt_index,
    const TopologyData& topology)>;
using FallbackFactory = std::function<TopologyData()>;

struct GenerationTestSeams {
    AttemptRejection reject_attempt{};
    FallbackFactory fallback_factory{};
};

[[nodiscard]] Seed derive_attempt_seed(Seed requested_seed, std::uint32_t attempt_index);
[[nodiscard]] TopologyData generate_topology_attempt(Seed attempt_seed);
[[nodiscard]] TopologyData known_good_fallback_topology();
[[nodiscard]] std::vector<std::string> validate_topology(const TopologyData& topology);
[[nodiscard]] std::uint64_t topology_fingerprint(
    Seed requested_seed,
    Seed attempt_seed,
    Seed effective_seed,
    const TopologyData& topology);
[[nodiscard]] std::string format_fingerprint(std::uint64_t fingerprint);
[[nodiscard]] GenerationResult generate_topology(
    Seed requested_seed,
    const GenerationTestSeams& seams = {});

}  // namespace crystalbound
