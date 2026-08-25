#include <exception>
#include <iostream>
#include <string_view>
#include <utility>
#include <vector>

#include "crystalbound/Application.hpp"
#include "crystalbound/CommandLine.hpp"
#include "crystalbound/Generation.hpp"

namespace {

int run_application(crystalbound::GenerationResult generation)
{
    try {
        crystalbound::Application application{std::move(generation)};
        return application.run();
    } catch (const std::exception& error) {
        std::cerr << "Fatal error: " << error.what() << '\n';
        return 1;
    }
}

void print_usage()
{
    std::cout
        << "Crystalbound deterministic-topology foundation\n"
        << "Usage: crystalbound [--seed <uint64>]\n\n"
        << "The current renderer still displays bundled Suzanne while generating and\n"
        << "validating an abstract cave topology for later construction steps.\n"
        << "Options:\n"
        << "  --seed <uint64>  Use a strict unsigned decimal requested seed.\n"
        << "  -h, --help       Show this help text without opening a window.\n";
}

void print_generation_diagnostic(const crystalbound::GenerationResult& result)
{
    std::cout << "Topology generation\n"
              << "  Requested seed: " << result.requested_seed.value << '\n'
              << "  Attempt seed: " << result.attempt_seed.value << '\n'
              << "  Effective seed: " << result.effective_seed.value << '\n'
              << "  Generator version: " << result.generator_version.value << '\n'
              << "  Fingerprint: "
              << crystalbound::format_fingerprint(result.fingerprint) << '\n'
              << "  Fallback: " << (result.used_fallback ? "yes" : "no") << '\n';
}

}  // namespace

int main(const int argument_count, char* arguments[])
{
    std::vector<std::string_view> argument_views;
    argument_views.reserve(
        argument_count > 0 ? static_cast<std::size_t>(argument_count - 1) : 0U);
    for (int index{1}; index < argument_count; ++index) {
        argument_views.emplace_back(arguments[index]);
    }

    try {
        const crystalbound::CommandLineOptions options{
            crystalbound::parse_command_line(argument_views)};
        if (options.show_help) {
            print_usage();
            return 0;
        }
        const crystalbound::Seed requested_seed{crystalbound::resolve_requested_seed(
            options, crystalbound::os_entropy_seed)};
        crystalbound::GenerationResult generation{
            crystalbound::generate_topology(requested_seed)};
        print_generation_diagnostic(generation);
        return run_application(std::move(generation));
    } catch (const crystalbound::CommandLineError& error) {
        std::cerr << "Command-line error: " << error.what()
                  << "\nUse --help for current usage.\n";
        return 2;
    } catch (const crystalbound::GenerationError& error) {
        std::cerr << "Topology generation failed: " << error.what() << '\n';
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "Fatal startup error: " << error.what() << '\n';
        return 1;
    }
}
