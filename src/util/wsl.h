#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace WSLUtil {

struct WslMapping {
    std::string distro;
    std::string wslRoot; // Linux path, e.g. /home/user/project
    std::filesystem::path uncRoot; // UNC path prefix, e.g. \\wsl$\Ubuntu\home\user\project
};

std::optional<WslMapping> detectMapping(const std::filesystem::path& path);

// Fills out with pairs of (full Windows UNC path, relative path using '/').
bool enumerateFiles(const WslMapping& mapping, std::vector<std::pair<std::filesystem::path, std::string>>& outFiles);

// Detect if the current process runs under WSL
bool isRunningUnderWSL();

}
