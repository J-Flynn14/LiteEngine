#pragma once
#include "platform.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Paths {

    // Directory containing the running executable, resolved once.
    inline const std::filesystem::path& exe_dir() noexcept {
        static const std::filesystem::path dir = Platform::compute_exe_dir();
        return dir;
    }

    // The shaders directory beside the executable.
    inline const std::filesystem::path& shader_dir() noexcept {
        static const std::filesystem::path dir = exe_dir() / "shaders";
        return dir;
    }

    // The assets directory beside the executable.
    inline const std::filesystem::path& asset_dir() noexcept {
        static const std::filesystem::path dir = exe_dir() / "assets";
        return dir;
    }

    // Path of a file that sits directly beside the executable.
    inline std::filesystem::path localFile(const std::string& filename) noexcept {
        return exe_dir() / filename;
    }

    // Path of a file inside the shaders directory.
    inline std::filesystem::path shader(const std::string& filename) noexcept {
        return shader_dir() / filename;
    }

    // Path of a file inside the assets directory.
    inline std::filesystem::path asset(const std::string& filename) noexcept {
        return asset_dir() / filename;
    }

    // Checks the expected directories exist, collecting any that are missing.
    inline bool check_valid(std::vector<std::filesystem::path>& missing, bool requireAssets = false) noexcept {
        missing.clear();
        std::error_code ec;
        if (!std::filesystem::is_directory(shader_dir(), ec)) missing.push_back(shader_dir());
        if (requireAssets && !std::filesystem::is_directory(asset_dir(), ec)) {
            missing.push_back(asset_dir());
        }
        return missing.empty();
    }

    // True when the assets directory exists.
    inline bool has_assets() noexcept {
        std::error_code ec;
        return std::filesystem::is_directory(asset_dir(), ec);
    }

    // Checks the expected directories exist, discarding the missing list.
    inline bool check_valid() noexcept {
        std::vector<std::filesystem::path> missing;
        return check_valid(missing);
    }

    // UTF-8 form of a path; needed on MinGW where path::value_type is wchar_t.
    inline std::string toUtf8(const std::filesystem::path& p) noexcept {
        const auto s = p.u8string();
        return std::string(s.begin(), s.end());
    }
}
