#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "crystalbound/CaveScene.hpp"
#include "crystalbound/Generation.hpp"

namespace crystalbound::test {

inline constexpr std::size_t ci_seed_corpus_size{256U};
inline constexpr std::size_t local_seed_corpus_size{10'000U};
inline constexpr Seed known_normal_seed{42U};
inline constexpr Seed corpus_reference_seed{123'456'789U};

struct SeedCorpusResult {
    Seed requested_seed{};
    Seed attempt_seed{};
    Seed effective_seed{};
    bool used_fallback{};
    std::size_t diagnostic_count{};
    std::uint64_t topology_fingerprint{};
    std::uint64_t scene_fingerprint{};
    std::uint64_t elemental_fingerprint{};
    std::uint64_t exit_arch_fingerprint{};
    std::uint32_t room_count{};
    std::uint32_t edge_count{};
    std::uint32_t exit_distance{};
    std::uint32_t farthest_distance{};
    std::int32_t elevation_span_millimetres{};
    std::uint32_t bridge_count{};
    std::uint32_t static_vertex_count{};
    std::uint32_t opaque_draw_call_count{};
    std::uint32_t transparent_effect_draw_count{};
    std::uint32_t particle_count{};
    std::uint32_t generator_version{};
    std::uint64_t normalized_layout_fingerprint{};
    std::uint64_t structural_component_fingerprint{};
    std::vector<GenerationDiagnostic> diagnostics{};
};

struct SeedCorpusReport {
    std::size_t requested_seed_count{};
    std::size_t normal_acceptance_count{};
    std::size_t fallback_acceptance_count{};
    std::size_t unique_normal_layout_count{};
    std::vector<SeedCorpusResult> results{};
};

class SeedCorpusError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

[[nodiscard]] std::vector<Seed> stable_seed_corpus(std::size_t count);
[[nodiscard]] SeedCorpusReport run_seed_corpus(const std::vector<Seed>& seeds);
[[nodiscard]] std::uint64_t normalized_layout_fingerprint(
    const CaveGenerationResult& result);
[[nodiscard]] std::uint64_t structural_component_fingerprint(
    const CaveGenerationResult& result);
[[nodiscard]] bool fallback_rate_within_percent(
    const SeedCorpusReport& report,
    std::uint32_t maximum_percent);
[[nodiscard]] bool normal_layout_uniqueness_at_least_percent(
    const SeedCorpusReport& report,
    std::uint32_t minimum_percent);
[[nodiscard]] std::string seed_corpus_manifest(const SeedCorpusReport& report);

}  // namespace crystalbound::test
