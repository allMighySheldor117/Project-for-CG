#include "crystalbound/ResourcePaths.hpp"

#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#include <Windows.h>
#elif defined(__linux__)
#include <unistd.h>
#else
#error "Executable-relative resources are implemented only for Windows and Linux."
#endif

namespace crystalbound {

std::filesystem::path executable_path()
{
#if defined(_WIN32)
    std::vector<wchar_t> buffer(512);
    for (;;) {
        SetLastError(ERROR_SUCCESS);
        const DWORD length = GetModuleFileNameW(
            nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            throw std::runtime_error(
                "GetModuleFileNameW failed with error "
                + std::to_string(GetLastError()));
        }
        if (length < buffer.size()) {
            return std::filesystem::path{std::wstring{buffer.data(), length}};
        }
        buffer.resize(buffer.size() * 2);
    }
#elif defined(__linux__)
    std::error_code error;
    const std::filesystem::path path = std::filesystem::read_symlink("/proc/self/exe", error);
    if (error) {
        throw std::runtime_error(
            "Unable to resolve /proc/self/exe: " + error.message());
    }
    return path;
#endif
}

std::filesystem::path resource_root()
{
    const std::filesystem::path path = executable_path().parent_path() / "resources";
    if (!std::filesystem::is_directory(path)) {
        throw std::runtime_error("Runtime resource directory is missing: " + path.u8string());
    }
    return path;
}

}  // namespace crystalbound
