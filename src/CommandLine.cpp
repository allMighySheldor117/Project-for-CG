#include "crystalbound/CommandLine.hpp"

#include <charconv>
#include <random>
#include <string>
#include <system_error>

namespace crystalbound {
namespace {

[[nodiscard]] std::uint64_t parse_seed(const std::string_view text)
{
    std::uint64_t value{};
    const char* const begin{text.data()};
    const char* const end{begin + text.size()};
    const auto result = std::from_chars(begin, end, value, 10);
    if (text.empty() || result.ec != std::errc{} || result.ptr != end) {
        throw CommandLineError{
            "--seed requires one strict unsigned decimal value in uint64 range."};
    }
    return value;
}

[[nodiscard]] std::uint32_t parse_profile_seconds(const std::string_view text)
{
    std::uint32_t value{};
    const char* const begin{text.data()};
    const char* const end{begin + text.size()};
    const auto result = std::from_chars(begin, end, value, 10);
    if (text.empty() || result.ec != std::errc{} || result.ptr != end
        || value == 0U || value > 3'600U) {
        throw CommandLineError{
            "--profile-seconds requires an integer in the range 1..3600."};
    }
    return value;
}

}  // namespace

CommandLineOptions parse_command_line(const std::vector<std::string_view>& arguments)
{
    CommandLineOptions options;
    for (std::size_t index{}; index < arguments.size(); ++index) {
        const std::string_view argument{arguments[index]};
        if (argument == "-h" || argument == "--help") {
            if (arguments.size() != 1U) {
                throw CommandLineError{"--help cannot be combined with other arguments."};
            }
            options.show_help = true;
            continue;
        }
        if (argument == "--seed") {
            if (options.requested_seed.has_value()) {
                throw CommandLineError{"--seed may be specified only once."};
            }
            if (index + 1U >= arguments.size()) {
                throw CommandLineError{"--seed requires a value."};
            }
            ++index;
            options.requested_seed = parse_seed(arguments[index]);
            continue;
        }
        if (argument == "--profile-seconds") {
            if (options.profile_seconds.has_value()) {
                throw CommandLineError{"--profile-seconds may be specified only once."};
            }
            if (index + 1U >= arguments.size()) {
                throw CommandLineError{"--profile-seconds requires a value."};
            }
            ++index;
            options.profile_seconds = parse_profile_seconds(arguments[index]);
            continue;
        }
        if (argument == "--profile-no-vsync") {
            if (options.profile_no_vsync) {
                throw CommandLineError{"--profile-no-vsync may be specified only once."};
            }
            options.profile_no_vsync = true;
            continue;
        }
        throw CommandLineError{"Unknown argument: " + std::string{argument}};
    }
    if (options.profile_no_vsync && !options.profile_seconds.has_value()) {
        throw CommandLineError{
            "--profile-no-vsync requires --profile-seconds."};
    }
    return options;
}

std::uint64_t resolve_requested_seed(
    const CommandLineOptions& options,
    const SeedSource& entropy_source)
{
    if (options.requested_seed.has_value()) {
        return *options.requested_seed;
    }
    if (!entropy_source) {
        throw std::invalid_argument("A seed source is required when --seed is omitted.");
    }
    return entropy_source();
}

std::uint64_t os_entropy_seed()
{
    std::random_device source;
    const std::uint64_t high{static_cast<std::uint64_t>(source())};
    const std::uint64_t low{static_cast<std::uint64_t>(source())};
    return (high << 32U) ^ low;
}

}  // namespace crystalbound
