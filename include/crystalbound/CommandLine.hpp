#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace crystalbound {

class CommandLineError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct CommandLineOptions {
    bool show_help{};
    std::optional<std::uint64_t> requested_seed{};
};

using SeedSource = std::function<std::uint64_t()>;

[[nodiscard]] CommandLineOptions parse_command_line(
    const std::vector<std::string_view>& arguments);
[[nodiscard]] std::uint64_t resolve_requested_seed(
    const CommandLineOptions& options,
    const SeedSource& entropy_source);
[[nodiscard]] std::uint64_t os_entropy_seed();

}  // namespace crystalbound
