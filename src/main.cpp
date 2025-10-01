#include "application.h"

#include <argparse/argparse.hpp>
#include <spdlog/spdlog.h>

int nitroefx_main(int argc, char** argv) {
    constexpr auto EXPORT_CMD = "export";
    constexpr auto INFO_CMD = "info";
    const auto isSubCmd = [](std::string_view arg) {
        return arg == EXPORT_CMD || arg == INFO_CMD;
    };

    if (argc <= 1 || std::filesystem::exists(argv[1])) {
        // No subcommands used, launch GUI
        Application app;
        return app.run(argc, argv);
    }

    argparse::ArgumentParser program("nitroefx", Application::VERSION);
    
    program.add_argument("--apply-update").nargs(3).help("Internal use only.");
    program.add_argument("--relaunch").flag().help("Internal use only.");

    argparse::ArgumentParser export_cmd(EXPORT_CMD);
    export_cmd.add_argument("path").help("Path to a .spa file").required().nargs(1);
    export_cmd.add_argument("-i", "--index").help("Texture index to export").nargs(argparse::nargs_pattern::at_least_one).scan<'i', int>();
    export_cmd.add_argument("-o", "--output").help("Output path."
        " Can be a directory (always) or a file path (only when used with a single index -i)").nargs(1);

    export_cmd.add_description("\nExamples:\n"
        "  nitroefx export path/to/file.spa\n"
        "  nitroefx export -i 1 3 4 -o /output/directory path/to/file.spa \n"
        "  nitroefx export -i 0 -o /output/directory/texture.png path/to/file.spa \n");

    argparse::ArgumentParser info_cmd(INFO_CMD);
    info_cmd.add_argument("path").help("Path to a .spa file").required().nargs(1);
    info_cmd.add_description("\nExamples:\n"
        "  nitroefx info path/to/file.spa\n");

    program.add_subparser(export_cmd);
    program.add_subparser(info_cmd);

    try {
        program.parse_args(argc, argv);
    } catch (const std::runtime_error &err) {
        spdlog::error("Error parsing arguments: {}", err.what());
        std::exit(1);
    }

    if (program.is_used("--apply-update")) {
        const auto args = program.get<std::vector<std::string>>("--apply-update");

        const auto srcPath = std::filesystem::path(args[0]);
        const auto dstPath = std::filesystem::path(args[1]);
        const auto parentPid = std::stoul(args[2]);
        const auto relaunch = program.get<bool>("--relaunch");

        return Application::update(srcPath, dstPath, parentPid, relaunch);
    }

    Application app;
    if (!isSubCmd(argv[1])) {
        return app.run(argc, argv);
    }

    return app.runCli(program);
}
