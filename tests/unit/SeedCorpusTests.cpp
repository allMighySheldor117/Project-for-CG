#include "SeedCorpusTests.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "SeedCorpus.hpp"
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

void stable_ci_corpus_has_exact_membership(const std::filesystem::path&)
{
    const std::vector<Seed> seeds{stable_seed_corpus(256U)};
    require(seeds.size() == 256U, "CI corpus must contain exactly 256 requested seeds");
    require(seeds.front() == Seed{0U}, "CI corpus must begin with requested seed 0");
    require(seeds[254] == Seed{254U}, "CI corpus sequential range changed");
    require(
        seeds.back() == Seed{123'456'789U},
        "CI corpus must end with reference seed 123456789");
    require(
        std::find(seeds.begin(), seeds.end(), Seed{42U}) != seeds.end(),
        "CI corpus must include normal requested seed 42");

    std::set<std::uint64_t> unique;
    for (const Seed seed : seeds) {
        unique.insert(seed.value);
    }
    require(unique.size() == seeds.size(), "CI corpus requested seeds must be unique");
}

void stable_corpus_rejects_counts_that_omit_golden_seeds(const std::filesystem::path&)
{
    bool rejected{};
    try {
        static_cast<void>(stable_seed_corpus(43U));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "undersized stable corpus must be rejected");
}

void complete_corpus_validates_reference_results(const std::filesystem::path&)
{
    const SeedCorpusReport report{run_seed_corpus({Seed{42U}, Seed{123'456'789U}})};
    require(report.requested_seed_count == 2U, "small corpus count changed");
    require(report.normal_acceptance_count == 2U, "normal corpus acceptance was not recorded");
    require(report.fallback_acceptance_count == 0U, "reference corpus unexpectedly used fallback");
    require(report.results.size() == 2U, "corpus result metadata is incomplete");
    require(
        report.results.front().scene_fingerprint == 0x52CCEB23A788803DULL,
        "seed 42 scene fingerprint changed in the complete corpus");
    require(
        !report.results.back().used_fallback
            && report.results.back().scene_fingerprint == 0x2489A9BDA8738BB0ULL,
        "reference seed result changed");
}

void corpus_percentage_gates_use_exact_integer_arithmetic(const std::filesystem::path&)
{
    SeedCorpusReport report;
    report.requested_seed_count = 100U;
    report.normal_acceptance_count = 95U;
    report.fallback_acceptance_count = 5U;
    report.unique_normal_layout_count = 86U;
    require(fallback_rate_within_percent(report, 5U), "exact five-percent fallback failed");
    require(
        normal_layout_uniqueness_at_least_percent(report, 90U),
        "integer uniqueness boundary failed");
    ++report.fallback_acceptance_count;
    require(
        !fallback_rate_within_percent(report, 5U),
        "fallback percentage gate accepted six percent");
}

void normalized_layout_ignores_world_transform(const std::filesystem::path&)
{
    const CaveGenerationResult original{generate_cave({42U})};
    CaveGenerationResult transformed{original};
    constexpr std::int32_t translate_x{12'345};
    constexpr std::int32_t translate_y{900};
    constexpr std::int32_t translate_z{-6'789};
    for (ChamberNode& node : transformed.generation.topology.nodes) {
        const std::int32_t old_x{node.anchor.x_millimetres};
        node.anchor.x_millimetres = -node.anchor.z_millimetres + translate_x;
        node.anchor.z_millimetres = old_x + translate_z;
        node.anchor.elevation_millimetres += translate_y;
    }
    for (RouteGeometryContract& route : transformed.scene.routes) {
        for (IntegerPoint3& control : route.spline.control_points) {
            const std::int32_t old_x{control.x_millimetres};
            control.x_millimetres = -control.z_millimetres + translate_x;
            control.z_millimetres = old_x + translate_z;
            control.y_millimetres += translate_y;
        }
    }
    require(
        normalized_layout_fingerprint(original)
            == normalized_layout_fingerprint(transformed),
        "normalized layout fingerprint changed under rotation and translation");
}

void repair_corpus_meets_fixed_layout_and_manifest_contracts(
    const std::filesystem::path&)
{
    const SeedCorpusReport report{run_seed_corpus(stable_seed_corpus(256U))};
    require(
        fallback_rate_within_percent(report, 5U),
        "repair corpus exceeds five-percent fallback");
    require(report.unique_normal_layout_count == 1U,
        "fixed authored corpus produced more than one normalized layout");
    const std::string first{seed_corpus_manifest(report)};
    const std::string second{seed_corpus_manifest(report)};
    require(first == second, "manifest serialization is not deterministic");
    require(
        first.find("elapsed") == std::string::npos
            && first.find("C:\\") == std::string::npos,
        "manifest leaked timing or a local path");
    require(
        first.find("unique_normal_layouts=") != std::string::npos
            && first.find("rejections=") != std::string::npos,
        "manifest omitted locked summary fields");
}

}  // namespace

std::vector<TestCase> seed_corpus_test_cases()
{
    return {
        {"stable CI corpus has exact membership", stable_ci_corpus_has_exact_membership},
        {"stable corpus rejects undersized counts", stable_corpus_rejects_counts_that_omit_golden_seeds},
        {"complete corpus validates reference results", complete_corpus_validates_reference_results},
        {"corpus percentages use exact integer gates", corpus_percentage_gates_use_exact_integer_arithmetic},
        {"normalized layout ignores world transform", normalized_layout_ignores_world_transform},
        {"repair corpus meets fixed-layout and manifest contracts", repair_corpus_meets_fixed_layout_and_manifest_contracts},
    };
}

}  // namespace crystalbound::test
