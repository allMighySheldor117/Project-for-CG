#pragma once

#include <filesystem>
#include <string_view>
#include <vector>

namespace crystalbound::test {

struct TestCase {
    std::string_view name;
    void (*function)(const std::filesystem::path&);
};

[[nodiscard]] std::vector<TestCase> generation_test_cases();

}  // namespace crystalbound::test
