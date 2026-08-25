#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>

#include "npr/Application.hpp"

namespace {

int run_application(const std::optional<std::filesystem::path>& model_path)
{
    try {
        npr::Application application{model_path};
        return application.run();
    } catch (const std::exception& error) {
        std::cerr << "Fatal error: " << error.what() << '\n';
        return 1;
    }
}

}  // namespace

#if defined(_WIN32)
int wmain(const int argument_count, wchar_t* arguments[])
#else
int main(const int argument_count, char* arguments[])
#endif
{
    if (argument_count > 2) {
        std::cerr << "Usage: npr_renderer [model.obj]\n";
        return 2;
    }
    const std::optional<std::filesystem::path> model_path = argument_count == 2
        ? std::optional<std::filesystem::path>{std::filesystem::path{arguments[1]}}
        : std::nullopt;
    return run_application(model_path);
}
