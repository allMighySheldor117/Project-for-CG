#include "FrameProfileTests.hpp"

#include <cmath>
#include <filesystem>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>

#include "crystalbound/FrameProfile.hpp"
#include "crystalbound/ProfileTraversal.hpp"

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

void profile_excludes_warmup_and_computes_percentiles(const std::filesystem::path&)
{
    FrameProfiler profiler{1.0, 0.06, 16U};
    require(!profiler.record({600.0, 100.0, 400.0}), "profile finished during warm-up");
    require(!profiler.record({400.0, 100.0, 200.0}), "warm-up frame finished the profile");
    require(!profiler.record({10.0, 4.0, 5.0}), "profile finished before its measured duration");
    require(!profiler.record({20.0, 8.0, 10.0}), "profile finished before its measured duration");
    require(profiler.record({30.0, 12.0, 15.0}), "profile did not finish at its duration");
    const FrameProfileSummary summary{profiler.summary()};
    require(summary.sample_count == 3U, "warm-up frames leaked into measured samples");
    require(std::abs(summary.median_milliseconds - 20.0) < 1.0e-9, "median is incorrect");
    require(std::abs(summary.p95_milliseconds - 30.0) < 1.0e-9, "p95 is incorrect");
    require(std::abs(summary.median_cpu_before_present_milliseconds - 8.0) < 1.0e-9,
        "CPU-before-present median is incorrect");
    require(std::abs(summary.p95_present_milliseconds - 15.0) < 1.0e-9,
        "present p95 is incorrect");
}

void profile_rejects_invalid_configuration_and_samples(const std::filesystem::path&)
{
    bool invalid_duration{};
    try {
        static_cast<void>(FrameProfiler{5.0, 0.0, 1U});
    } catch (const std::invalid_argument&) {
        invalid_duration = true;
    }
    require(invalid_duration, "zero measured duration was accepted");

    FrameProfiler profiler{0.0, 1.0, 1U};
    bool invalid_sample{};
    try {
        static_cast<void>(profiler.record({1.0, -1.0, 0.0}));
    } catch (const std::invalid_argument&) {
        invalid_sample = true;
    }
    require(invalid_sample, "negative frame time was accepted");
}

void traversal_workload_covers_every_chamber_and_repeats(const std::filesystem::path&)
{
    const CaveGenerationResult generation{generate_cave({42U})};
    const ProfileTraversalWorkload first{
        build_profile_traversal_workload(generation)};
    const ProfileTraversalWorkload second{
        build_profile_traversal_workload(generation)};
    require(
        first.fingerprint == second.fingerprint,
        "profile traversal workload fingerprint changed on replay");
    require(
        first.chamber_visit_order.size()
            == generation.generation.topology.nodes.size() * 2U - 1U,
        "profile workload does not cover the linear route in both directions");
    require(
        first.chamber_visit_order.front() == first.chamber_visit_order.back(),
        "profile workload does not return to Start");
    const std::set<std::uint32_t> visited = [&] {
            std::set<std::uint32_t> ids;
            for (std::size_t index{};
                 index + 1U < first.chamber_visit_order.size(); ++index) {
                ids.insert(first.chamber_visit_order[index].value);
            }
            return ids;
        }();
    require(
        visited.size() == generation.generation.topology.nodes.size(),
        "profile workload omits a generated chamber");
    require(
        first.waypoints_metres.size() > first.chamber_visit_order.size(),
        "profile workload omitted production route samples");
}

}  // namespace

std::vector<TestCase> frame_profile_test_cases()
{
    return {
        {"frame profile excludes warm-up and computes percentiles", profile_excludes_warmup_and_computes_percentiles},
        {"frame profile rejects invalid inputs", profile_rejects_invalid_configuration_and_samples},
        {"profile traversal covers every chamber and repeats", traversal_workload_covers_every_chamber_and_repeats},
    };
}

}  // namespace crystalbound::test
