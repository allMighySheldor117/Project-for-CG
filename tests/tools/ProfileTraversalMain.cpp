#include <charconv>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "crystalbound/Application.hpp"
#include "crystalbound/CaveScene.hpp"
#include "crystalbound/Generation.hpp"
#include "crystalbound/ProfileTraversal.hpp"

namespace {

struct Options {
    std::uint64_t seed{42U};
    std::uint32_t profile_seconds{300U};
    bool vsync{true};
    bool help{};
};

template <typename Value>
[[nodiscard]] Value parse_unsigned(
    const std::string_view text,
    const std::string_view option)
{
    Value value{};
    const auto parsed{std::from_chars(text.data(), text.data() + text.size(), value)};
    if (text.empty() || parsed.ec != std::errc{}
        || parsed.ptr != text.data() + text.size()) {
        throw std::invalid_argument{std::string{option} + " requires an unsigned integer"};
    }
    return value;
}

[[nodiscard]] Options parse_options(const int argument_count, char* arguments[])
{
    Options options;
    for (int index{1}; index < argument_count; ++index) {
        const std::string_view argument{arguments[index]};
        if (argument == "--help" || argument == "-h") {
            options.help = true;
        } else if (argument == "--seed") {
            if (++index >= argument_count) {
                throw std::invalid_argument{"--seed requires a value"};
            }
            options.seed = parse_unsigned<std::uint64_t>(arguments[index], "--seed");
        } else if (argument == "--profile-seconds") {
            if (++index >= argument_count) {
                throw std::invalid_argument{"--profile-seconds requires a value"};
            }
            options.profile_seconds = parse_unsigned<std::uint32_t>(
                arguments[index], "--profile-seconds");
            if (options.profile_seconds == 0U || options.profile_seconds > 3'600U) {
                throw std::invalid_argument{
                    "--profile-seconds must be in the range 1..3600"};
            }
        } else if (argument == "--no-vsync") {
            options.vsync = false;
        } else {
            throw std::invalid_argument{"unknown option: " + std::string{argument}};
        }
    }
    return options;
}

void print_help()
{
    std::cout
        << "Usage: crystalbound_profile_runner [--seed N] [--profile-seconds N]"
           " [--no-vsync]\n"
        << "Runs the tests-only deterministic all-room production traversal profile.\n";
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
        crystalbound::CaveGenerationResult generation{
            crystalbound::generate_cave({options.seed})};
        const crystalbound::ProfileTraversalWorkload workload{
            crystalbound::build_profile_traversal_workload(generation)};
        std::cout << "Crystalbound deterministic traversal profile\n"
                  << "  Requested seed: " << options.seed << '\n'
                  << "  Effective seed: "
                  << generation.generation.effective_seed.value << '\n'
                  << "  Generator version: "
                  << generation.generation.generator_version.value << '\n'
                  << "  Workload fingerprint: "
                  << crystalbound::format_fingerprint(workload.fingerprint) << '\n';
        crystalbound::Application application{
            std::move(generation), crystalbound::os_entropy_seed,
            options.profile_seconds, options.vsync, true};
        return application.run();
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
