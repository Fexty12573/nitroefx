#include "application.h"
#include "library_versions.h"

#include <curl/curlver.h>
#include <GL/glew.h>
#include <glm/detail/setup.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <implot.h>
#include <libimagequant.h>
#include <microtar.h>
#include <minizip-ng/mz.h>
#include <nlohmann/json.hpp>
#include <SDL3/SDL_version.h>
#include <spdlog/version.h>
#include <spng.h>
#include <tinyfiledialogs.h>
#include <zlib.h>
#include <fmt/format.h>

void Application::renderAboutWindow() {
    ImGui::PushOverrideID(m_aboutWindowId);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 16.0f));

    if (ImGui::BeginPopupModal("About NitroEFX", &m_aboutWindowOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
        const auto windowSize = ImGui::GetWindowSize();
        if (m_icon) {
            constexpr auto iconSize = 128.0f;
            ImGui::SetCursorPosX((windowSize.x - iconSize) * 0.5f);
            ImGui::Image(m_icon->getHandle(), { iconSize, iconSize });
        }

        ImGui::PushFont(getFont("Large"));

        const auto appStr = fmt::format("NitroEFX {}", Application::VERSION);
        const auto size = ImGui::CalcTextSize(appStr.c_str());
        ImGui::SetCursorPosX((windowSize.x - size.x) * 0.5f);

        ImGui::TextUnformatted(appStr.c_str());

        ImGui::PopFont();

        ImGui::Separator();
        ImGui::Text("A particle editor for the Nintendo DS Pokémon games.");
        ImGui::Text("Created by Fexty12573");
        ImGui::TextLinkOpenURL("https://github.com/Fexty12573/nitroefx");

        ImGui::Separator();

        if (ImGui::CollapsingHeader("Third-Party Libraries")) {
            ImGui::BulletText("argparse - v%s", ARGPARSE_VERSION);
            ImGui::BulletText("cURL - v%s", LIBCURL_VERSION);
            ImGui::BulletText("efsw - v%s", EFSW_VERSION);
            ImGui::BulletText("fmt - v%d.%d.%d", FMT_VERSION / 10000, (FMT_VERSION / 100) % 100, FMT_VERSION % 100);
            ImGui::BulletText("GLEW - v%d.%d.%d", GLEW_VERSION_MAJOR, GLEW_VERSION_MINOR, GLEW_VERSION_MICRO);
            ImGui::BulletText("glm - v%d.%d.%d", GLM_VERSION_MAJOR, GLM_VERSION_MINOR, GLM_VERSION_PATCH);
            ImGui::BulletText("Dear ImGui - v%s", IMGUI_VERSION);
            ImGui::BulletText("ImPlot - v%s", IMPLOT_VERSION);
            ImGui::BulletText("libimagequant - v%s", LIQ_VERSION_STRING);
            ImGui::BulletText("libspng - v%d.%d.%d", SPNG_VERSION_MAJOR, SPNG_VERSION_MINOR, SPNG_VERSION_PATCH);
            ImGui::BulletText("microtar - v%s", MTAR_VERSION);
            ImGui::BulletText("minizip-ng - v%s", MZ_VERSION);
            ImGui::BulletText("narc - v%s", NARC_VERSION);
            ImGui::BulletText("nlohmann/json - v%d.%d.%d", NLOHMANN_JSON_VERSION_MAJOR, NLOHMANN_JSON_VERSION_MINOR, NLOHMANN_JSON_VERSION_PATCH);
            ImGui::BulletText("SDL3 - v%d.%d.%d", SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_MICRO_VERSION);
            ImGui::BulletText("spdlog - v%d.%d.%d", SPDLOG_VER_MAJOR, SPDLOG_VER_MINOR, SPDLOG_VER_PATCH);
            ImGui::BulletText("tinyfiledialogs - v%s", tinyfd_version);
            ImGui::BulletText("zlib - v%s", ZLIB_VERSION);
        }

        ImGui::EndPopup();
    }

    ImGui::PopStyleVar(2);
    ImGui::PopID();
}
