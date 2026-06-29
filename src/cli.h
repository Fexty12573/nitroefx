#pragma once

#include <filesystem>
#include <optional>
#include <variant>
#include <vector>

namespace cli {

struct GuiCommand {
    std::optional<std::filesystem::path> openPath;
};

struct ExportCommand {
    std::filesystem::path path;
    std::vector<int> indices; // empty means all textures
    std::optional<std::filesystem::path> output;
};

struct InfoCommand {
    std::filesystem::path path;
};

struct UpdateCommand {
    std::filesystem::path src;
    std::filesystem::path dst;
    unsigned long pid;
    bool relaunch;
};

using Command = std::variant<GuiCommand, ExportCommand, InfoCommand, UpdateCommand>;

Command parse(int argc, char** argv);

// Headless operations
int runExport(const ExportCommand& cmd);
int runInfo(const InfoCommand& cmd);

}
