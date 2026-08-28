#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "SeedCorpus.hpp"
#include "crystalbound/Generation.hpp"

namespace {

struct Options {
    std::size_t count{crystalbound::test::ci_seed_corpus_size};
    bool details{};
    bool help{};
    std::optional<std::uint32_t> maximum_fallback_percent{};
    std::optional<std::filesystem::path> manifest_path{};
};

[[nodiscard]] std::size_t parse_count(const std::string_view text)
{
    std::uint64_t value{};
    const auto parsed{std::from_chars(text.data(), text.data() + text.size(), value)};
    if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()
        || value > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        throw std::invalid_argument("--count requires an unsigned decimal integer");
    }
    return static_cast<std::size_t>(value);
}

[[nodiscard]] Options parse_options(const int argument_count, char* arguments[])
{
    Options options;
    for (int index{1}; index < argument_count; ++index) {
        const std::string_view argument{arguments[index]};
        if (argument == "--help" || argument == "-h") {
            options.help = true;
        } else if (argument == "--details") {
            options.details = true;
        } else if (argument == "--count") {
            if (++index >= argument_count) {
                throw std::invalid_argument("--count requires a value");
            }
            options.count = parse_count(arguments[index]);
        } else if (argument == "--max-fallback-percent") {
            if (++index >= argument_count) {
                throw std::invalid_argument("--max-fallback-percent requires a value");
            }
            const std::size_t value{parse_count(arguments[index])};
            if (value > 100U) {
                throw std::invalid_argument(
                    "--max-fallback-percent must be in the range 0..100");
            }
            options.maximum_fallback_percent = static_cast<std::uint32_t>(value);
        } else if (argument == "--manifest") {
            if (++index >= argument_count || std::string_view{arguments[index]}.empty()) {
                throw std::invalid_argument("--manifest requires a path");
            }
            options.manifest_path = std::filesystem::path{arguments[index]};
        } else {
            throw std::invalid_argument("unknown option: " + std::string{argument});
        }
    }
    return options;
}

void print_help()
{
    std::cout
        << "Usage: crystalbound_seed_corpus [--count N] [--details]"
           " [--max-fallback-percent P] [--manifest PATH]\n"
        << "\n"
        << "Stable selection: requested seeds 0 through N-2, followed by 123456789.\n"
        << "N must be at least 44 so reference seeds 42 and 123456789 are included.\n"
        << "CI uses 256; the opt-in local Release audit uses 10000.\n";
}

void print_details(const crystalbound::test::SeedCorpusReport& report)
{
    std::cout
        << "requested\tattempt\teffective\tfallback\trooms\tedges\texit_distance"
           "\tfarthest_distance\televation_mm\tbridges\tvertices\topaque_draws"
           "\ttransparent_draws\tparticles\tversion\tlayout_fp\tcomponent_fp"
           "\ttopology_fp\tscene_fp\telemental_fp\texit_fp\n";
    for (const crystalbound::test::SeedCorpusResult& result : report.results) {
        std::cout
            << result.requested_seed.value << '\t'
            << result.attempt_seed.value << '\t'
            << result.effective_seed.value << '\t'
            << (result.used_fallback ? "yes" : "no") << '\t'
            << result.room_count << '\t'
            << result.edge_count << '\t'
            << result.exit_distance << '\t'
            << result.farthest_distance << '\t'
            << result.elevation_span_millimetres << '\t'
            << result.bridge_count << '\t'
            << result.static_vertex_count << '\t'
            << result.opaque_draw_call_count << '\t'
            << result.transparent_effect_draw_count << '\t'
            << result.particle_count << '\t'
            << result.generator_version << '\t'
            << crystalbound::format_fingerprint(result.normalized_layout_fingerprint) << '\t'
            << crystalbound::format_fingerprint(result.structural_component_fingerprint) << '\t'
            << crystalbound::format_fingerprint(result.topology_fingerprint) << '\t'
            << crystalbound::format_fingerprint(result.scene_fingerprint) << '\t'
            << crystalbound::format_fingerprint(result.elemental_fingerprint) << '\t'
            << crystalbound::format_fingerprint(result.exit_arch_fingerprint) << '\n';
    }
}

}  // namespace

int main(const int argument_count, char* arguments[])
{
    try {
        const Options options{parse_options(argument_count, arguments)};
        if (options.help) {
            print_help();
            return 0;
        }
        const auto started{std::chrono::steady_clock::now()};
        const crystalbound::test::SeedCorpusReport report{
            crystalbound::test::run_seed_corpus(
                crystalbound::test::stable_seed_corpus(options.count))};
        const std::chrono::duration<double> elapsed{
            std::chrono::steady_clock::now() - started};
        if (options.details) {
            print_details(report);
        }
        if (options.manifest_path.has_value()) {
            std::ofstream manifest{
                *options.manifest_path,
                std::ios::binary | std::ios::trunc};
            if (!manifest) {
                throw std::runtime_error{"could not open manifest path"};
            }
            manifest << crystalbound::test::seed_corpus_manifest(report);
            if (!manifest) {
                throw std::runtime_error{"could not write complete manifest"};
            }
        }
        if (options.maximum_fallback_percent.has_value()
            && !crystalbound::test::fallback_rate_within_percent(
                report, *options.maximum_fallback_percent)) {
            throw std::runtime_error{
                "fallback percentage exceeds requested maximum"};
        }
        if (report.unique_normal_layout_count != 1U) {
            throw std::runtime_error{
                "fixed authored corpus produced more than one normalized layout"};
        }
        std::cout
            << "PASS requested_seeds=" << report.requested_seed_count
            << " normal=" << report.normal_acceptance_count
            << " fallback=" << report.fallback_acceptance_count
            << " unique_normal_layouts=" << report.unique_normal_layout_count
            << " repeated_complete_results=" << report.requested_seed_count
            << " elapsed_seconds=" << elapsed.count() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
