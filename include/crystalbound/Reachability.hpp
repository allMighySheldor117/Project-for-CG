#pragma once

#include <string>

#include "crystalbound/PlayerController.hpp"

namespace crystalbound {

[[nodiscard]] MechanicalReachabilityReport validate_mechanical_reachability(
    const TopologyData& topology,
    const CaveSceneData& scene,
    const CollisionWorld& collision_world);
[[nodiscard]] const char* reachability_failure_name(ReachabilityFailure failure) noexcept;
[[nodiscard]] std::string format_reachability_issue(const ReachabilityIssue& issue);

}  // namespace crystalbound
