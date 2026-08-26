#include <exception>
#include <iostream>
#include <string_view>
#include <utility>
#include <vector>

#include "crystalbound/Application.hpp"
#include "crystalbound/CaveScene.hpp"
#include "crystalbound/CommandLine.hpp"
#include "crystalbound/Generation.hpp"

namespace {

int run_application(crystalbound::CaveGenerationResult generation)
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
        << "Crystalbound lit-cave development build\n"
        << "Usage: crystalbound [--seed <uint64>]\n\n"
        << "The current build generates and displays a deterministic low-poly cave\n"
        << "with curved tunnels, a wooden bridge, Phong lighting, procedural materials,\n"
        << "a warm camera lantern, and distance fog.\n"
        << "Options:\n"
        << "  --seed <uint64>  Use a strict unsigned decimal requested seed.\n"
        << "  -h, --help       Show this help text without opening a window.\n";
}

void print_generation_diagnostic(const crystalbound::CaveGenerationResult& result)
{
    const crystalbound::GenerationResult& topology{result.generation};
    std::cout << "Topology generation\n"
              << "  Requested seed: " << topology.requested_seed.value << '\n'
              << "  Attempt seed: " << topology.attempt_seed.value << '\n'
              << "  Effective seed: " << topology.effective_seed.value << '\n'
              << "  Generator version: " << topology.generator_version.value << '\n'
              << "  Fingerprint: "
              << crystalbound::format_fingerprint(topology.fingerprint) << '\n'
              << "  Scene fingerprint: "
              << crystalbound::format_fingerprint(result.scene.fingerprint) << '\n'
              << "  Mechanical reachability: "
              << (result.reachability.accepted ? "accepted" : "rejected") << '\n'
              << "  Reachable chambers: "
              << result.reachability.reachable_chambers.size() << '\n'
              << "  Directed route traversals: "
              << result.reachability.directed_routes.size() << '\n'
              << "  Fallback: " << (topology.used_fallback ? "yes" : "no") << '\n'
              << "  Attempts:\n";
    for (const crystalbound::GenerationDiagnostic& diagnostic : topology.diagnostics) {
        std::string_view outcome{"rejected"};
        if (diagnostic.outcome == crystalbound::AttemptOutcome::accepted) {
            outcome = "accepted";
        } else if (diagnostic.outcome == crystalbound::AttemptOutcome::fallback_accepted) {
            outcome = "fallback accepted";
        }
        std::cout << "    [" << diagnostic.attempt_index << "] seed "
                  << diagnostic.attempt_seed.value << ": " << outcome << " - "
                  << diagnostic.message << '\n';
    }
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
        crystalbound::CaveGenerationResult generation{
            crystalbound::generate_cave(requested_seed)};
        print_generation_diagnostic(generation);
        return run_application(std::move(generation));
    } catch (const crystalbound::CommandLineError& error) {
        std::cerr << "Command-line error: " << error.what()
                  << "\nUse --help for current usage.\n";
        return 2;
    } catch (const crystalbound::GenerationError& error) {
        std::cerr << "Cave generation failed: " << error.what() << '\n';
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "Fatal startup error: " << error.what() << '\n';
        return 1;
    }
}
