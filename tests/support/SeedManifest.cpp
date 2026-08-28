#include "SeedCorpus.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace crystalbound::test {
namespace {

constexpr std::uint64_t fnv_offset_basis{14'695'981'039'346'656'037ULL};
constexpr std::uint64_t fnv_prime{1'099'511'628'211ULL};

struct PlanarPoint {
    std::int32_t x{};
    std::int32_t z{};
};

[[nodiscard]] std::uint8_t semantic_key(const ChamberNode& node)
{
    switch (node.role) {
    case ChamberRole::start:
        return 0U;
    case ChamberRole::elemental:
        if (!node.element.has_value()) {
            throw SeedCorpusError{"elemental node lacks semantic identity"};
        }
        return static_cast<std::uint8_t>(1U + static_cast<std::uint8_t>(*node.element));
    case ChamberRole::exit:
        return 6U;
    case ChamberRole::neutral:
        return 7U;
    }
    throw SeedCorpusError{"unknown chamber role in normalized layout"};
}

[[nodiscard]] std::map<std::uint32_t, std::uint8_t> semantic_keys(
    const TopologyData& topology)
{
    std::map<std::uint32_t, std::uint8_t> keys;
    for (const ChamberNode& node : topology.nodes) {
        keys.emplace(node.id.value, semantic_key(node));
    }
    return keys;
}

[[nodiscard]] const ChamberNode& start_node(const TopologyData& topology)
{
    const auto found{std::find_if(
        topology.nodes.begin(), topology.nodes.end(), [](const ChamberNode& node) {
            return node.role == ChamberRole::start;
        })};
    if (found == topology.nodes.end()) {
        throw SeedCorpusError{"normalized layout has no Start chamber"};
    }
    return *found;
}

[[nodiscard]] PlanarPoint transform_point(
    const std::int32_t x,
    const std::int32_t z,
    const std::uint32_t transform)
{
    switch (transform) {
    case 0U:
        return {x, z};
    case 1U:
        return {-z, x};
    case 2U:
        return {-x, -z};
    case 3U:
        return {z, -x};
    case 4U:
        return {-x, z};
    case 5U:
        return {x, -z};
    case 6U:
        return {z, x};
    case 7U:
        return {-z, -x};
    default:
        throw SeedCorpusError{"unknown planar canonicalization transform"};
    }
}

[[nodiscard]] std::uint64_t fnv1a(const std::string_view text) noexcept
{
    std::uint64_t hash{fnv_offset_basis};
    for (const unsigned char byte : text) {
        hash ^= byte;
        hash *= fnv_prime;
    }
    return hash;
}

[[nodiscard]] std::string canonical_layout_text(
    const CaveGenerationResult& result,
    const std::uint32_t transform)
{
    const TopologyData& topology{result.generation.topology};
    const CaveSceneData& scene{result.scene};
    const ChamberNode& start{start_node(topology)};
    const auto keys{semantic_keys(topology)};
    std::vector<const ChamberNode*> nodes;
    nodes.reserve(topology.nodes.size());
    for (const ChamberNode& node : topology.nodes) {
        nodes.push_back(&node);
    }
    std::sort(nodes.begin(), nodes.end(), [](const ChamberNode* left, const ChamberNode* right) {
        return semantic_key(*left) < semantic_key(*right);
    });

    std::ostringstream output;
    output << "v=" << current_generator_version.value << ";nodes=";
    for (const ChamberNode* node : nodes) {
        const PlanarPoint point{transform_point(
            node->anchor.x_millimetres - start.anchor.x_millimetres,
            node->anchor.z_millimetres - start.anchor.z_millimetres,
            transform)};
        const ChamberDimensionBand band{planned_chamber_dimension_band(*node)};
        output << static_cast<unsigned int>(semantic_key(*node)) << ','
               << point.x << ','
               << node->anchor.elevation_millimetres
                      - start.anchor.elevation_millimetres
               << ',' << point.z << ','
               << band.minimum_ring_radius_millimetres << ','
               << band.maximum_ring_radius_millimetres << ','
               << band.minimum_interior_height_millimetres << ','
               << band.maximum_interior_height_millimetres << ';';
    }

    std::vector<std::pair<std::uint8_t, std::uint8_t>> edges;
    edges.reserve(topology.edges.size());
    for (const Edge edge : topology.edges) {
        const auto [minimum, maximum]{std::minmax(
            keys.at(edge.first.value), keys.at(edge.second.value))};
        edges.emplace_back(minimum, maximum);
    }
    std::sort(edges.begin(), edges.end());
    output << "edges=";
    for (const auto [first, second] : edges) {
        output << static_cast<unsigned int>(first) << '-'
               << static_cast<unsigned int>(second) << ';';
    }

    struct RouteRecord {
        std::uint8_t first{};
        std::uint8_t second{};
        const RouteGeometryContract* route{};
    };
    std::vector<RouteRecord> routes;
    routes.reserve(scene.routes.size());
    for (const RouteGeometryContract& route : scene.routes) {
        const std::uint8_t first{keys.at(route.edge.first.value)};
        const std::uint8_t second{keys.at(route.edge.second.value)};
        routes.push_back({std::min(first, second), std::max(first, second), &route});
    }
    std::sort(routes.begin(), routes.end(), [](const RouteRecord& left, const RouteRecord& right) {
        return std::tie(left.first, left.second) < std::tie(right.first, right.second);
    });
    output << "routes=";
    for (const RouteRecord& record : routes) {
        const RouteGeometryContract& route{*record.route};
        const bool reverse{
            keys.at(route.edge.first.value) > keys.at(route.edge.second.value)};
        output << static_cast<unsigned int>(record.first) << '-'
               << static_cast<unsigned int>(record.second) << ','
               << (route.bridge ? 1 : 0) << ':';
        for (std::size_t step{}; step < route.spline.control_points.size(); ++step) {
            const std::size_t index{reverse
                    ? route.spline.control_points.size() - 1U - step
                    : step};
            const IntegerPoint3& control{route.spline.control_points[index]};
            const PlanarPoint point{transform_point(
                control.x_millimetres - start.anchor.x_millimetres,
                control.z_millimetres - start.anchor.z_millimetres,
                transform)};
            output << point.x << ','
                   << control.y_millimetres - start.anchor.elevation_millimetres
                   << ',' << point.z << '/';
        }
        output << ';';
    }
    return output.str();
}

[[nodiscard]] std::string outcome_name(const AttemptOutcome outcome)
{
    switch (outcome) {
    case AttemptOutcome::accepted:
        return "accepted";
    case AttemptOutcome::rejected:
        return "rejected";
    case AttemptOutcome::fallback_accepted:
        return "fallback_accepted";
    }
    return "unknown";
}

[[nodiscard]] std::string rejection_kind(const GenerationDiagnostic& diagnostic)
{
    if (diagnostic.outcome != AttemptOutcome::rejected) {
        return "none";
    }
    constexpr std::array<std::string_view, 5> prefixes{
        "layout rejected:",
        "geometry rejected:",
        "collision rejected:",
        "mechanical reachability rejected:",
        "topology",
    };
    constexpr std::array<std::string_view, 5> names{
        "layout", "geometry", "collision", "mechanical", "topology"};
    for (std::size_t index{}; index < prefixes.size(); ++index) {
        if (diagnostic.message.rfind(prefixes[index], 0U) == 0U) {
            return std::string{names[index]};
        }
    }
    return "other";
}

[[nodiscard]] std::uint64_t checked_percent_product(
    const std::size_t count,
    const std::uint32_t percent)
{
    if (percent > 100U
        || count > std::numeric_limits<std::uint64_t>::max() / 100U) {
        throw std::invalid_argument{"corpus percentage arithmetic is out of range"};
    }
    return static_cast<std::uint64_t>(count) * percent;
}

}  // namespace

std::uint64_t normalized_layout_fingerprint(const CaveGenerationResult& result)
{
    std::array<std::string, 8> candidates;
    for (std::uint32_t transform{}; transform < candidates.size(); ++transform) {
        candidates[transform] = canonical_layout_text(result, transform);
    }
    return fnv1a(*std::min_element(candidates.begin(), candidates.end()));
}

std::uint64_t structural_component_fingerprint(const CaveGenerationResult& result)
{
    const auto keys{semantic_keys(result.generation.topology)};
    std::ostringstream output;
    output << "v=" << current_generator_version.value << ";chambers=";
    std::vector<const ChamberGeometryContract*> chambers;
    for (const ChamberGeometryContract& chamber : result.scene.chambers) {
        chambers.push_back(&chamber);
    }
    std::sort(chambers.begin(), chambers.end(), [&](const auto* left, const auto* right) {
        return keys.at(left->node_id.value) < keys.at(right->node_id.value);
    });
    for (const ChamberGeometryContract* chamber : chambers) {
        output << static_cast<unsigned int>(keys.at(chamber->node_id.value)) << ','
               << chamber->base_radius_millimetres << ','
               << chamber->wall_height_millimetres << ','
               << chamber->side_count << ','
               << chamber->minimum_safe_ring_radius_millimetres << ','
               << static_cast<unsigned int>(chamber->identity.floor) << ','
               << static_cast<unsigned int>(chamber->identity.shell) << ','
               << static_cast<unsigned int>(chamber->identity.entrance) << ','
               << static_cast<unsigned int>(chamber->identity.landmark) << ','
               << static_cast<unsigned int>(chamber->identity.vertical_profile) << ':';
        for (const std::int32_t offset : chamber->radial_offsets_millimetres) {
            output << offset << ',';
        }
        output << "rings=";
        for (const ChamberRingContract& ring : chamber->rings) {
            output << ring.height_millimetres << '[';
            for (const std::int32_t radius : ring.radii_millimetres) {
                output << radius << ',';
            }
            output << ']';
        }
        output << ';';
    }
    output << "routes=";
    std::vector<const RouteGeometryContract*> routes;
    for (const RouteGeometryContract& route : result.scene.routes) {
        routes.push_back(&route);
    }
    std::sort(routes.begin(), routes.end(), [&](const auto* left, const auto* right) {
        const auto left_key{std::minmax(
            keys.at(left->edge.first.value), keys.at(left->edge.second.value))};
        const auto right_key{std::minmax(
            keys.at(right->edge.first.value), keys.at(right->edge.second.value))};
        return std::tie(left_key.first, left_key.second)
            < std::tie(right_key.first, right_key.second);
    });
    for (const RouteGeometryContract* route : routes) {
        const auto [first, second]{std::minmax(
            keys.at(route->edge.first.value), keys.at(route->edge.second.value))};
        output << static_cast<unsigned int>(first) << '-'
               << static_cast<unsigned int>(second) << ','
               << route->spline.radius_millimetres << ','
               << (route->bridge ? 1 : 0) << ','
               << route->bridge_width_millimetres << ','
               << route->bridge_rail_height_millimetres << ':';
        for (const std::int32_t offset : route->ring_offsets_millimetres) {
            output << offset << ',';
        }
        output << ';';
    }
    return fnv1a(output.str());
}

bool fallback_rate_within_percent(
    const SeedCorpusReport& report,
    const std::uint32_t maximum_percent)
{
    if (report.requested_seed_count == 0U) {
        return false;
    }
    return checked_percent_product(report.fallback_acceptance_count, 100U)
        <= checked_percent_product(report.requested_seed_count, maximum_percent);
}

bool normal_layout_uniqueness_at_least_percent(
    const SeedCorpusReport& report,
    const std::uint32_t minimum_percent)
{
    if (report.normal_acceptance_count == 0U) {
        return false;
    }
    return checked_percent_product(report.unique_normal_layout_count, 100U)
        >= checked_percent_product(report.normal_acceptance_count, minimum_percent);
}

std::string seed_corpus_manifest(const SeedCorpusReport& report)
{
    std::array<std::size_t, 6> rejection_counts{};
    const auto rejection_index = [](const std::string_view kind) {
        if (kind == "layout") return 0U;
        if (kind == "geometry") return 1U;
        if (kind == "collision") return 2U;
        if (kind == "mechanical") return 3U;
        if (kind == "topology") return 4U;
        return 5U;
    };
    for (const SeedCorpusResult& result : report.results) {
        for (const GenerationDiagnostic& diagnostic : result.diagnostics) {
            if (diagnostic.outcome == AttemptOutcome::rejected) {
                ++rejection_counts[rejection_index(rejection_kind(diagnostic))];
            }
        }
    }

    std::ostringstream output;
    output << "crystalbound_seed_manifest\t1\n"
           << "summary\trequested=" << report.requested_seed_count
           << "\tnormal=" << report.normal_acceptance_count
           << "\tfallback=" << report.fallback_acceptance_count
           << "\tunique_normal_layouts=" << report.unique_normal_layout_count
           << "\trejections=layout:" << rejection_counts[0]
           << ",geometry:" << rejection_counts[1]
           << ",collision:" << rejection_counts[2]
           << ",mechanical:" << rejection_counts[3]
           << ",topology:" << rejection_counts[4]
           << ",other:" << rejection_counts[5] << '\n'
           << "requested\tattempt\teffective\tversion\tfallback\tvalidation"
              "\tlayout_fp\tcomponent_fp\tattempts\n";
    for (const SeedCorpusResult& result : report.results) {
        output << result.requested_seed.value << '\t'
               << result.attempt_seed.value << '\t'
               << result.effective_seed.value << '\t'
               << result.generator_version << '\t'
               << (result.used_fallback ? 1 : 0) << "\tvalid\t"
               << format_fingerprint(result.normalized_layout_fingerprint) << '\t'
               << format_fingerprint(result.structural_component_fingerprint) << '\t';
        for (std::size_t index{}; index < result.diagnostics.size(); ++index) {
            if (index != 0U) {
                output << ',';
            }
            const GenerationDiagnostic& diagnostic{result.diagnostics[index]};
            output << diagnostic.attempt_index << '/'
                   << diagnostic.attempt_seed.value << '/'
                   << outcome_name(diagnostic.outcome) << '/'
                   << rejection_kind(diagnostic);
        }
        output << '\n';
    }
    return output.str();
}

}  // namespace crystalbound::test
