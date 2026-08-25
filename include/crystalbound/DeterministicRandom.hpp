#pragma once

#include <cstdint>

namespace crystalbound {

namespace random_domain {

inline constexpr std::uint64_t topology{0xA0761D6478BD642FULL};
inline constexpr std::uint64_t anchors{0xE7037ED1A0B428DBULL};
inline constexpr std::uint64_t routes{0x8EBC6AF09C88C6E3ULL};
inline constexpr std::uint64_t geometry{0x589965CC75374CC3ULL};
inline constexpr std::uint64_t materials{0x1D8E4E27C47D124FULL};
inline constexpr std::uint64_t decoration{0xEB44ACCAB455D165ULL};
inline constexpr std::uint64_t retry{0xD6E8FEB86659FD93ULL};

}  // namespace random_domain

class SplitMix64 {
public:
    explicit constexpr SplitMix64(const std::uint64_t initial_state) noexcept
        : state_(initial_state)
    {
    }

    [[nodiscard]] std::uint64_t next() noexcept;
    [[nodiscard]] std::uint64_t bounded(std::uint64_t bound);
    [[nodiscard]] bool boolean() noexcept;

private:
    std::uint64_t state_{};
};

[[nodiscard]] SplitMix64 make_substream(
    std::uint64_t seed,
    std::uint64_t domain,
    std::uint64_t stable_object_id) noexcept;

}  // namespace crystalbound
