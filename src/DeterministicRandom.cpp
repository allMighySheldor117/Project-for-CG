#include "crystalbound/DeterministicRandom.hpp"

#include <stdexcept>

namespace crystalbound {
namespace {

constexpr std::uint64_t increment{0x9E3779B97F4A7C15ULL};
constexpr std::uint64_t first_multiplier{0xBF58476D1CE4E5B9ULL};
constexpr std::uint64_t second_multiplier{0x94D049BB133111EBULL};

}  // namespace

std::uint64_t SplitMix64::next() noexcept
{
    state_ += increment;
    std::uint64_t value{state_};
    value = (value ^ (value >> 30U)) * first_multiplier;
    value = (value ^ (value >> 27U)) * second_multiplier;
    return value ^ (value >> 31U);
}

std::uint64_t SplitMix64::bounded(const std::uint64_t bound)
{
    if (bound == 0U) {
        throw std::invalid_argument("SplitMix64 bounded sampling requires a positive bound.");
    }

    const std::uint64_t threshold{(std::uint64_t{0} - bound) % bound};
    while (true) {
        const std::uint64_t value{next()};
        if (value >= threshold) {
            return value % bound;
        }
    }
}

bool SplitMix64::boolean() noexcept
{
    return (next() & std::uint64_t{1}) != 0U;
}

SplitMix64 make_substream(
    const std::uint64_t seed,
    const std::uint64_t domain,
    const std::uint64_t stable_object_id) noexcept
{
    return SplitMix64{seed ^ domain ^ stable_object_id};
}

}  // namespace crystalbound
