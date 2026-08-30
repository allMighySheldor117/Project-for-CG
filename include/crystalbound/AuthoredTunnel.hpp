#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "crystalbound/CaveScene.hpp"
#include "crystalbound/ObjLoader.hpp"

namespace crystalbound {

enum class TunnelDecorationKind : std::uint8_t {
    pillar,
    rock,
    broken_stones,
    lantern,
    puddle,
    wood_support,
};

struct AuthoredTunnelAssets {
    MaterialModelLoadResult segment{};
    MaterialModelLoadResult entrance{};
    MaterialModelLoadResult pillar{};
    MaterialModelLoadResult rock{};
    MaterialModelLoadResult broken_stones{};
    MaterialModelLoadResult lantern{};
    MaterialModelLoadResult puddle{};
    MaterialModelLoadResult wood_support{};
};

struct TunnelTransform {
    GeometryVector3 translation_metres{};
    double yaw_radians{};
    GeometryVector3 scale{1.0, 1.0, 1.0};
};

struct AuthoredTunnelSegmentInstance {
    std::uint64_t stable_object_id{};
    Edge route{};
    std::uint8_t run_ordinal{};
    std::uint32_t tile_index{};
    std::uint32_t tile_count{};
    TunnelTransform transform{};
};

struct AuthoredTunnelEntranceInstance {
    std::uint64_t stable_object_id{};
    NodeId chamber_id{};
    Edge route{};
    TunnelTransform transform{};
};

struct AuthoredTunnelDecorationInstance {
    TunnelDecorationKind kind{TunnelDecorationKind::pillar};
    std::uint64_t stable_object_id{};
    Edge route{};
    std::uint8_t run_ordinal{};
    TunnelTransform transform{};
};

struct AuthoredTunnelLayout {
    std::vector<AuthoredTunnelSegmentInstance> segments{};
    std::vector<AuthoredTunnelEntranceInstance> entrances{};
    std::vector<AuthoredTunnelDecorationInstance> decorations{};
};

[[nodiscard]] AuthoredTunnelAssets load_authored_tunnel_assets(
    const std::filesystem::path& resource_directory);
[[nodiscard]] const MaterialModelLoadResult& authored_tunnel_decoration_asset(
    const AuthoredTunnelAssets& assets,
    TunnelDecorationKind kind);
[[nodiscard]] std::vector<std::string> validate_authored_tunnel_assets(
    const AuthoredTunnelAssets& assets);
[[nodiscard]] AuthoredTunnelLayout build_authored_tunnel_layout(
    const CaveSceneData& scene);
[[nodiscard]] std::vector<std::string> validate_authored_tunnel_layout(
    const CaveSceneData& scene,
    const AuthoredTunnelLayout& layout);

}  // namespace crystalbound
