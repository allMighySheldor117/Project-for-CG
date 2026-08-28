#pragma once

#include <cstdint>
#include <vector>

#include "crystalbound/CaveScene.hpp"

namespace crystalbound {

struct ProfileTraversalWorkload {
    std::vector<NodeId> chamber_visit_order{};
    std::vector<GeometryVector3> waypoints_metres{};
    std::uint64_t fingerprint{};
};

[[nodiscard]] ProfileTraversalWorkload build_profile_traversal_workload(
    const CaveGenerationResult& generation);

}  // namespace crystalbound
