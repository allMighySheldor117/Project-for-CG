#include "crystalbound/CaveScene.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace crystalbound {
namespace {

constexpr double millimetres_per_metre{1'000.0};

struct IntegerDirection2 {
    std::int32_t x{};
    std::int32_t z{};
};

constexpr std::array<IntegerDirection2, 16U> chamber_directions{{
    {10'000, 0}, {9'239, 3'827}, {7'071, 7'071}, {3'827, 9'239},
    {0, 10'000}, {-3'827, 9'239}, {-7'071, 7'071}, {-9'239, 3'827},
    {-10'000, 0}, {-9'239, -3'827}, {-7'071, -7'071}, {-3'827, -9'239},
    {0, -10'000}, {3'827, -9'239}, {7'071, -7'071}, {9'239, -3'827},
}};

}  // namespace

GeometryVector3 chamber_ring_position(
    const ChamberGeometryContract& chamber,
    const std::size_t ring_index,
    const std::uint32_t side)
{
    if (chamber.side_count != chamber_directions.size()
        || ring_index >= chamber.rings.size()
        || chamber.rings[ring_index].radii_millimetres.size() != chamber.side_count) {
        throw std::invalid_argument{"Invalid chamber ring contract."};
    }
    const IntegerDirection2 direction{chamber_directions[side % chamber.side_count]};
    const double radius{static_cast<double>(
        chamber.rings[ring_index].radii_millimetres[side % chamber.side_count])
        / millimetres_per_metre};
    return {
        static_cast<double>(chamber.center_millimetres.x_millimetres)
                / millimetres_per_metre
            + static_cast<double>(direction.x) / 10'000.0 * radius,
        static_cast<double>(chamber.center_millimetres.y_millimetres
            + chamber.rings[ring_index].height_millimetres) / millimetres_per_metre,
        static_cast<double>(chamber.center_millimetres.z_millimetres)
                / millimetres_per_metre
            + static_cast<double>(direction.z) / 10'000.0 * radius,
    };
}

bool chamber_portal_opens_side(
    const std::vector<PortalContract>& portals,
    const NodeId chamber_id,
    const std::uint32_t side,
    const std::uint32_t side_count) noexcept
{
    if (side_count == 0U) {
        return false;
    }
    for (const PortalContract& portal : portals) {
        if (portal.chamber_id != chamber_id) {
            continue;
        }
        const std::uint32_t previous{
            (portal.opening_side_index + side_count - 1U) % side_count};
        const std::uint32_t next{(portal.opening_side_index + 1U) % side_count};
        if (side == previous || side == portal.opening_side_index || side == next) {
            return true;
        }
    }
    return false;
}

std::vector<ChamberStructuralTriangle> chamber_structure_triangles(
    const ChamberGeometryContract& chamber,
    const std::vector<PortalContract>& portals)
{
    if (chamber.rings.size() < 4U || chamber.side_count != chamber_directions.size()) {
        throw std::invalid_argument{"Chamber structure requires at least four valid rings."};
    }
    std::vector<ChamberStructuralTriangle> triangles;
    triangles.reserve(chamber.side_count * chamber.rings.size() * 2U);
    std::uint64_t stable_id{static_cast<std::uint64_t>(chamber.node_id.value) << 32U};
    constexpr std::int32_t portal_roof_height_millimetres{3'000};
    for (std::size_t ring{}; ring + 1U < chamber.rings.size(); ++ring) {
        const bool below_portal_roof{
            chamber.rings[ring].height_millimetres < portal_roof_height_millimetres};
        for (std::uint32_t side{}; side < chamber.side_count; ++side) {
            const std::uint32_t next{(side + 1U) % chamber.side_count};
            const bool opening{below_portal_roof && chamber_portal_opens_side(
                portals, chamber.node_id, side, chamber.side_count)};
            if (!opening) {
                const GeometryVector3 lower_first{chamber_ring_position(chamber, ring, side)};
                const GeometryVector3 lower_second{chamber_ring_position(chamber, ring, next)};
                const GeometryVector3 upper_first{chamber_ring_position(chamber, ring + 1U, side)};
                const GeometryVector3 upper_second{chamber_ring_position(chamber, ring + 1U, next)};
                triangles.push_back({stable_id++, lower_first, lower_second, upper_second});
                triangles.push_back({stable_id++, lower_first, upper_second, upper_first});
            } else {
                stable_id += 2U;
            }
        }
    }
    const GeometryVector3 cap_center{
        static_cast<double>(chamber.center_millimetres.x_millimetres) / millimetres_per_metre,
        static_cast<double>(chamber.center_millimetres.y_millimetres
            + chamber.rings.back().height_millimetres) / millimetres_per_metre,
        static_cast<double>(chamber.center_millimetres.z_millimetres) / millimetres_per_metre,
    };
    for (std::uint32_t side{}; side < chamber.side_count; ++side) {
        const std::uint32_t next{(side + 1U) % chamber.side_count};
        triangles.push_back({stable_id++,
            chamber_ring_position(chamber, chamber.rings.size() - 1U, side),
            chamber_ring_position(chamber, chamber.rings.size() - 1U, next),
            cap_center});
    }
    return triangles;
}

}  // namespace crystalbound
