#include "crystalbound/Generation.hpp"

#include <cstddef>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace crystalbound {
namespace {

[[nodiscard]] std::string join_errors(const std::vector<std::string>& errors)
{
    std::ostringstream message;
    for (std::size_t index{}; index < errors.size(); ++index) {
        if (index != 0U) {
            message << "; ";
        }
        message << errors[index];
    }
    return message.str();
}

}  // namespace

GenerationResult generate_topology(
    const Seed requested_seed,
    const GenerationTestSeams& seams)
{
    std::vector<GenerationDiagnostic> diagnostics;
    diagnostics.reserve(topology_limits.normal_attempt_count + 1U);
    for (std::uint32_t attempt_index{};
         attempt_index < topology_limits.normal_attempt_count;
         ++attempt_index) {
        const Seed attempt_seed{derive_attempt_seed(requested_seed, attempt_index)};
        TopologyData topology{generate_topology_attempt(attempt_seed)};
        std::vector<std::string> errors{validate_topology(topology)};
        if (errors.empty() && seams.reject_attempt) {
            const std::optional<std::string> forced_rejection{
                seams.reject_attempt(attempt_index, topology)};
            if (forced_rejection.has_value()) {
                errors.push_back(*forced_rejection);
            }
        }
        if (errors.empty()) {
            diagnostics.push_back({
                attempt_index,
                attempt_seed,
                AttemptOutcome::accepted,
                "accepted validated topology",
            });
            const std::uint64_t fingerprint{topology_fingerprint(
                requested_seed, attempt_seed, attempt_seed, topology)};
            return GenerationResult{
                requested_seed,
                attempt_seed,
                attempt_seed,
                current_generator_version,
                std::move(topology),
                fingerprint,
                std::move(diagnostics),
                false,
            };
        }
        diagnostics.push_back({
            attempt_index,
            attempt_seed,
            AttemptOutcome::rejected,
            join_errors(errors),
        });
    }

    TopologyData fallback = seams.fallback_factory
        ? seams.fallback_factory()
        : known_good_fallback_topology();
    const std::vector<std::string> fallback_errors{validate_topology(fallback)};
    if (!fallback_errors.empty()) {
        throw GenerationError{
            "Known-good fallback topology failed validation after eight rejected attempts: "
            + join_errors(fallback_errors)};
    }
    diagnostics.push_back({
        topology_limits.normal_attempt_count,
        fallback_effective_seed,
        AttemptOutcome::fallback_accepted,
        "accepted validated fallback topology",
    });
    const std::uint64_t fingerprint{topology_fingerprint(
        requested_seed,
        fallback_effective_seed,
        fallback_effective_seed,
        fallback)};
    return GenerationResult{
        requested_seed,
        fallback_effective_seed,
        fallback_effective_seed,
        current_generator_version,
        std::move(fallback),
        fingerprint,
        std::move(diagnostics),
        true,
    };
}

}  // namespace crystalbound
