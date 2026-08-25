#include "crystalbound/Geometry.hpp"

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

#include "crystalbound/DeterministicRandom.hpp"

namespace crystalbound {
namespace {

constexpr std::uint64_t fnv_offset_basis{14695981039346656037ULL};
constexpr std::uint64_t fnv_prime{1099511628211ULL};
constexpr std::uint32_t geometry_contract_version{1U};

template <typename Integer>
void append_little_endian(std::vector<std::uint8_t>& bytes, const Integer value)
{
    static_assert(std::is_integral_v<Integer>);
    using Unsigned = std::make_unsigned_t<Integer>;
    Unsigned bits{static_cast<Unsigned>(value)};
    for (std::size_t index{}; index < sizeof(Integer); ++index) {
        bytes.push_back(static_cast<std::uint8_t>(bits & Unsigned{0xFFU}));
        bits >>= 8U;
    }
}

[[nodiscard]] std::uint64_t fnv1a(const std::vector<std::uint8_t>& bytes) noexcept
{
    std::uint64_t hash{fnv_offset_basis};
    for (const std::uint8_t byte : bytes) {
        hash ^= byte;
        hash *= fnv_prime;
    }
    return hash;
}

}  // namespace

std::uint64_t geometry_contract_fingerprint(
    const Seed effective_seed,
    const SplineRouteInput& input,
    const SplineSamplingContract& contract)
{
    validate_spline_route_input(input);
    validate_spline_sampling_contract(contract);
    std::vector<std::uint8_t> bytes;
    bytes.reserve(64U + input.control_points.size() * 12U);
    append_little_endian(bytes, geometry_contract_version);
    append_little_endian(bytes, current_generator_version.value);
    append_little_endian(bytes, random_domain::geometry);
    append_little_endian(bytes, effective_seed.value);
    append_little_endian(bytes, input.stable_object_id);
    append_little_endian(bytes, input.radius_millimetres);
    append_little_endian(bytes, input.ring_side_count);
    append_little_endian(bytes, static_cast<std::uint8_t>(input.facing));
    append_little_endian(bytes, contract.maximum_sample_spacing_millimetres);
    append_little_endian(bytes, contract.maximum_chord_error_millimetres);
    append_little_endian(bytes, contract.maximum_overshoot_millimetres);
    append_little_endian(bytes, contract.maximum_grade_millidegrees);
    append_little_endian(bytes, contract.maximum_frame_turn_millidegrees);
    append_little_endian(bytes, contract.maximum_subdivision_depth);
    append_little_endian(
        bytes,
        static_cast<std::uint32_t>(input.control_points.size()));
    for (const IntegerPoint3& point : input.control_points) {
        append_little_endian(bytes, point.x_millimetres);
        append_little_endian(bytes, point.y_millimetres);
        append_little_endian(bytes, point.z_millimetres);
    }
    return fnv1a(bytes);
}

}  // namespace crystalbound
