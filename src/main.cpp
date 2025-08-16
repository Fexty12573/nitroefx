#include "application.h"

#include <argparse/argparse.hpp>
#include <spdlog/spdlog.h>

int nitroefx_main(int argc, char** argv) {
    // -- GUI Mode
    // $ nitroefx
    // $ nitroefx <path>

    // -- CLI Mode
    // $ nitroefx cli <options>

    if (argc == 1) {
        // No arguments provided, start in GUI mode
        Application app;
        return app.run(argc, argv);
    }

    argparse::ArgumentParser program("nitroefx");
    
    // Optional path for GUI mode
    //program.add_argument("path").help("Optional path for GUI mode");

    program.add_argument("--apply-update").nargs(3);
    program.add_argument("--relaunch").default_value(false).implicit_value(true);

    // Subcommand for CLI
    argparse::ArgumentParser cli("cli", "Command Line Interface for nitroefx");
    cli.add_argument("path").help("Path to a .spa file").required().nargs(1);
    cli.add_argument("-e", "--export").help("Export textures").default_value(false).implicit_value(true);
    cli.add_argument("-i", "--index").help("Texture index to export").nargs(argparse::nargs_pattern::at_least_one).scan<'i', int>();
    cli.add_argument("-f", "--format").help("Export format (png, bmp, tga). Default is png").nargs(1);
    cli.add_argument("-o", "--output").help("Output path."
        " Can be a directory (always) or a file path (only when used with a single index -i)").nargs(1);

    program.add_subparser(cli);

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
    if (program.is_subcommand_used("cli")) {
        return app.runCli(cli);
    } else {
        return app.run(argc, argv);
    }
}
