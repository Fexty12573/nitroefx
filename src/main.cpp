#include "application.h"
#include "cli.h"

#include <variant>

namespace {
template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
}

int nitroefx_main(int argc, char** argv) {
    return std::visit(overloaded{
        [](const cli::GuiCommand& cmd) {
            Application app;
            return app.run(cmd.openPath);
        },
        [](const cli::ExportCommand& cmd) {
            return cli::runExport(cmd);
        },
        [](const cli::InfoCommand& cmd) {
            return cli::runInfo(cmd);
        },
        [](const cli::UpdateCommand& cmd) {
            return Application::update(cmd.src, cmd.dst, cmd.pid, cmd.relaunch);
        },
    }, cli::parse(argc, argv));
}
