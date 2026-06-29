#include "cli.h"

#include "version.h"
#include "spl/spl_archive.h"

#include <argparse/argparse.hpp>
#include <spdlog/spdlog.h>
#include <fmt/format.h>

#include <string_view>

namespace cli {

namespace {

constexpr auto EXPORT_CMD = "export";
constexpr auto INFO_CMD = "info";

}

Command parse(int argc, char** argv) {
    if (argc <= 1) {
        return GuiCommand{};
    }

    const std::string_view first = argv[1];
    const bool isFlag = first.starts_with('-');
    const bool isSubcommand = first == EXPORT_CMD || first == INFO_CMD;

    if (!isFlag && !isSubcommand) {
        std::filesystem::path path = argv[1];
        if (std::filesystem::exists(path)) {
            return GuiCommand{ std::move(path) };
        }
    }

    argparse::ArgumentParser program("nitroefx", NITROEFX_VERSION);
    program.add_description(
        "Launch the GUI, optionally opening a file or folder:\n"
        "  nitroefx [path]\n");

    program.add_argument("--apply-update")
        .nargs(3)
        .help("Internal use only.");

    program.add_argument("--relaunch")
        .flag()
        .help("Internal use only.");

    argparse::ArgumentParser export_cmd(EXPORT_CMD);
    export_cmd.add_argument("path")
        .help("Path to a .spa file")
        .required()
        .nargs(1);

    export_cmd.add_argument("-i", "--index")
        .help("Texture index to export")
        .nargs(argparse::nargs_pattern::at_least_one)
        .scan<'i', int>();

    export_cmd.add_argument("-o", "--output")
        .help("Output path. Can be a directory (always) or a file path (only when used with a single index -i)")
        .nargs(1);

    export_cmd.add_description("\nExamples:\n"
        "  nitroefx export path/to/file.spa\n"
        "  nitroefx export -i 1 3 4 -o /output/directory path/to/file.spa \n"
        "  nitroefx export -i 0 -o /output/directory/texture.png path/to/file.spa \n");

    argparse::ArgumentParser info_cmd(INFO_CMD);
    info_cmd.add_argument("path").help("Path to a .spa file")
        .required()
        .nargs(1);

    info_cmd.add_description("\nExamples:\n"
        "  nitroefx info path/to/file.spa\n");

    program.add_subparser(export_cmd);
    program.add_subparser(info_cmd);

    try {
        program.parse_args(argc, argv);
    } catch (const std::exception& err) {
        spdlog::error("Error parsing arguments: {}", err.what());
        std::exit(1);
    }

    if (program.is_used("--apply-update")) {
        const auto args = program.get<std::vector<std::string>>("--apply-update");
        return UpdateCommand{
            .src = args[0],
            .dst = args[1],
            .pid = std::stoul(args[2]),
            .relaunch = program.get<bool>("--relaunch"),
        };
    }

    if (program.is_subcommand_used(EXPORT_CMD)) {
        const auto& cmd = program.at<argparse::ArgumentParser>(EXPORT_CMD);
        ExportCommand result;
        result.path = cmd.get<std::string>("path");
        if (auto indices = cmd.present<std::vector<int>>("--index")) {
            result.indices = std::move(*indices);
        }
        if (auto output = cmd.present<std::string>("--output")) {
            result.output = std::filesystem::path(*output);
        }
        return result;
    }

    if (program.is_subcommand_used(INFO_CMD)) {
        const auto& cmd = program.at<argparse::ArgumentParser>(INFO_CMD);
        return InfoCommand{ .path = cmd.get<std::string>("path") };
    }

    // Fall back to GUI
    return GuiCommand{};
}

int runExport(const ExportCommand& cmd) {
    if (!SPLArchive::isValid(cmd.path)) {
        spdlog::error("Invalid SPL file: {}", cmd.path.string());
        return 1;
    }

    SPLArchive archive(cmd.path, /* createGpuTextures */ false);

    std::filesystem::path outputPath = cmd.output.value_or(std::filesystem::current_path());

    if (cmd.indices.empty()) {
        std::filesystem::create_directories(outputPath);
        archive.exportTextures(outputPath);
        spdlog::info("Exported {} textures to {}", archive.getTextureCount(), outputPath.string());
        return 0;
    }

    if (cmd.indices.size() == 1) {
        const int index = cmd.indices[0];
        if (index < 0 || static_cast<size_t>(index) >= archive.getTextureCount()) {
            spdlog::error("Invalid texture index: {}", index);
            return 1;
        }

        if (std::filesystem::is_directory(outputPath)) {
            outputPath /= fmt::format("texture_{}.png", index);
        }

        archive.exportTexture(index, outputPath);
        spdlog::info("Exported texture {} to {}", index, outputPath.string());
        return 0;
    }

    if (std::filesystem::is_regular_file(outputPath)) {
        spdlog::error("Output path must be a directory when exporting multiple textures");
        return 1;
    }

    std::filesystem::create_directories(outputPath);
    archive.exportTextures(outputPath);
    return 0;
}

int runInfo(const InfoCommand& cmd) {
    if (!SPLArchive::isValid(cmd.path)) {
        spdlog::error("Invalid SPL file: {}", cmd.path.string());
        return 1;
    }

    SPLArchive archive(cmd.path, /* createGpuTextures */ false);
    archive.printInfo(cmd.path.filename().string());
    return 0;
}

}
