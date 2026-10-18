#pragma once

#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>

// Allocation policy: these helpers build strings inside noexcept functions;
// allocation failure terminates by design (the engine treats OOM as fatal).
namespace log_detail {
    // Passes ordinary values through unchanged.
    template<typename T>
    const T& narrow(const T& v) noexcept {return v;}

    // Converts a path to its narrow string form (MinGW paths are wchar_t).
    inline std::string narrow(const std::filesystem::path& p) noexcept {return p.string();}

    // Converts a wide string to its narrow form via the path machinery.
    inline std::string narrow(const std::wstring& s) noexcept {return std::filesystem::path(s).string();}

    // Converts a wide C string to its narrow form via the path machinery.
    inline std::string narrow(const wchar_t* s) noexcept {return std::filesystem::path(s).string();}

    // Concatenates all arguments into one string, narrowing wide inputs.
    template<typename... Args>
    std::string join(const Args&... args) noexcept {
        std::ostringstream oss;
        (oss << ... << narrow(args));
        return oss.str();
    }
}

// Writes an error line to stderr.
template<typename... Args>
void log_error(const Args&... args) noexcept {
    std::cerr << "[ERROR]: " << log_detail::join(args...) << '\n';
}

// Writes a warning line to stderr.
template<typename... Args>
void log_warn(const Args&... args) noexcept {
    std::cerr << "[WARNING]: " << log_detail::join(args...) << '\n';
}

// Writes an informational line to stdout.
template<typename... Args>
void log_info(const Args&... args) noexcept {
    std::cout << "[INFO]: " << log_detail::join(args...) << '\n';
}
