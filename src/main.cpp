#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string_view>

#include "crystalbound/Application.hpp"

namespace {

int run_application(const std::optional<std::filesystem::path>& model_path)
{
    try {
        crystalbound::Application application{model_path};
        return application.run();
    } catch (const std::exception& error) {
        std::cerr << "Fatal error: " << error.what() << '\n';
        return 1;
    }
}

}  // namespace

namespace {

#if defined(_WIN32)
using ArgumentView = std::wstring_view;
constexpr ArgumentView help_argument{L"--help"};
constexpr ArgumentView short_help_argument{L"-h"};
#else
using ArgumentView = std::string_view;
constexpr ArgumentView help_argument{"--help"};
constexpr ArgumentView short_help_argument{"-h"};
#endif

void print_usage()
{
    std::cout
        << "Crystalbound development foundation\n"
        << "Usage: crystalbound [model.obj]\n\n"
        << "With no model path, Crystalbound loads the bundled Suzanne development model.\n"
        << "An optional OBJ path is supported for mesh-pipeline development.\n"
        << "Options:\n"
        << "  -h, --help  Show this help text without opening a window.\n";
}

}  // namespace

#if defined(_WIN32)
int wmain(const int argument_count, wchar_t* arguments[])
#else
int main(const int argument_count, char* arguments[])
#endif
{
    if (argument_count == 2) {
        const ArgumentView argument{arguments[1]};
        if (argument == help_argument || argument == short_help_argument) {
            print_usage();
            return 0;
        }
        if (!argument.empty() && argument.front() == static_cast<ArgumentView::value_type>('-')) {
            std::cerr << "Unknown option. Use --help for current usage.\n";
            return 2;
        }
    }
    if (argument_count > 2) {
        std::cerr << "Usage: crystalbound [model.obj]\n";
        return 2;
    }
    const std::optional<std::filesystem::path> model_path = argument_count == 2
        ? std::optional<std::filesystem::path>{std::filesystem::path{arguments[1]}}
        : std::nullopt;
    return run_application(model_path);
}
