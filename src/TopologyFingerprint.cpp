#include "crystalbound/Generation.hpp"

#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <vector>

namespace crystalbound {
namespace {

constexpr std::uint64_t fnv_offset_basis{14'695'981'039'346'656'037ULL};
constexpr std::uint64_t fnv_prime{1'099'511'628'211ULL};

void append_u8(std::vector<std::uint8_t>& bytes, const std::uint8_t value)
{
    bytes.push_back(value);
}

void append_u32(std::vector<std::uint8_t>& bytes, const std::uint32_t value)
{
    for (unsigned int shift{}; shift < 32U; shift += 8U) {
        bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

void append_i32(std::vector<std::uint8_t>& bytes, const std::int32_t value)
{
    append_u32(bytes, static_cast<std::uint32_t>(value));
}

void append_u64(std::vector<std::uint8_t>& bytes, const std::uint64_t value)
{
    for (unsigned int shift{}; shift < 64U; shift += 8U) {
        bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
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

std::uint64_t topology_fingerprint(
    const Seed requested_seed,
    const Seed attempt_seed,
    const Seed effective_seed,
    const TopologyData& topology)
{
    std::vector<std::uint8_t> bytes;
    append_u32(bytes, current_generator_version.value);
    append_u64(bytes, requested_seed.value);
    append_u64(bytes, attempt_seed.value);
    append_u64(bytes, effective_seed.value);
    append_u32(bytes, static_cast<std::uint32_t>(topology.nodes.size()));
    for (const ChamberNode& node : topology.nodes) {
        append_u32(bytes, node.id.value);
        append_u8(bytes, static_cast<std::uint8_t>(node.role));
        append_u8(
            bytes,
            node.element.has_value()
                ? static_cast<std::uint8_t>(*node.element)
                : std::numeric_limits<std::uint8_t>::max());
        append_i32(bytes, node.anchor.x_millimetres);
        append_i32(bytes, node.anchor.elevation_millimetres);
        append_i32(bytes, node.anchor.z_millimetres);
        append_i32(bytes, node.anchor.heading_millidegrees);
    }
    append_u32(bytes, static_cast<std::uint32_t>(topology.edges.size()));
    for (const Edge edge : topology.edges) {
        append_u32(bytes, edge.first.value);
        append_u32(bytes, edge.second.value);
    }
    append_u32(bytes, static_cast<std::uint32_t>(topology.routes.size()));
    for (const RouteDescriptor& route : topology.routes) {
        append_u32(bytes, route.edge.first.value);
        append_u32(bytes, route.edge.second.value);
        append_i32(bytes, route.lateral_offset_millimetres);
        append_i32(bytes, route.elevation_offset_millimetres);
        append_i32(bytes, route.heading_millidegrees);
        append_u8(bytes, route.on_guaranteed_cycle ? 1U : 0U);
    }
    append_u32(bytes, static_cast<std::uint32_t>(topology.guaranteed_cycle.size()));
    for (const NodeId id : topology.guaranteed_cycle) {
        append_u32(bytes, id.value);
    }
    return fnv1a(bytes);
}

std::string format_fingerprint(const std::uint64_t fingerprint)
{
    std::ostringstream output;
    output << std::hex << std::setfill('0') << std::setw(16) << fingerprint;
    return output.str();
}

}  // namespace crystalbound
