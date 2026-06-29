#include "editor.h"
#include "application.h"
#include "application_colors.h"
#include "project_manager.h"
#include "spl/enum_names.h"
#include "help_messages.h"
#include "fonts/IconsFontAwesome6.h"
#include "imgui/extensions.h"
#include "spl/spl_resource.h"

#include <array>
#include <cinttypes>
#include <ranges>
#include <fmt/format.h>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/integer.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <im_anim.h>
#include <SDL3/SDL_misc.h>


Editor::Editor() {
    m_gridRenderer = std::make_shared<GridRenderer>(s_gridDimensions, s_gridSpacing);
    m_settingsWindowId = ImHashStr("Settings##Editor");
    m_tutorialWindowId = ImHashStr("Tutorial##Editor");
}

void Editor::update(float deltaTime) {
    (void)deltaTime;
    const auto now = EditorInstance::Clock::now();
    const auto backupInterval = g_application->getSettings().backupInterval;

    for (const auto& editor : g_projectManager->getOpenEditors()) {
        // Temporary, recovered, or unmodified editors don't get backed up
        if (editor->isTemp() || editor->isRecovered() || !editor->isModified()) {
            continue;
        }

        const auto lastBackup = editor->getLastBackupTime();
        const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastBackup);
        if (elapsed >= backupInterval) {
            editor->saveBackup();
        }
    }
}

void Editor::render() {
    const auto& instances = g_projectManager->getOpenEditors();

    ImGuiWindowClass windowClass;
    windowClass.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_NoTabBar
        | ImGuiDockNodeFlags_NoDockingOverCentralNode
        | ImGuiDockNodeFlags_NoUndocking;

    ImGui::SetNextWindowClass(&windowClass);
    ImGui::Begin("Work Area##Editor", nullptr, ImGuiWindowFlags_NoDecoration);

    std::vector<std::shared_ptr<BaseEditor>> toClose;
    if (ImGui::BeginTabBar("Editor Instances", ImGuiTabBarFlags_Reorderable 
        | ImGuiTabBarFlags_FittingPolicyShrink | ImGuiTabBarFlags_AutoSelectNewTabs)) {
        for (const auto& instance : instances) {
            const auto [open, active] = instance->render();
            if (!open) {
                toClose.push_back(instance);
            }
            if (active) {
                g_projectManager->setActiveEditor(instance);
            }
        }

        ImGui::EndTabBar();
    }

    g_projectManager->clearForceActivate();

    for (const auto& instance : toClose) {
        g_projectManager->closeEditor(instance);
    }

    ImGui::End();

    if (const auto& active = g_projectManager->getActiveEditor()) {
        active->renderPanels();
    }

    if (m_settingsOpen) {
        renderSettings();
    }

    renderTutorial();

    const auto editors = g_projectManager->getUnsavedEditors();
    if (!editors.empty()) {
        const auto popupPos = ImGui::GetMainViewport()->GetCenter();

        ImGui::SetNextWindowSize({ 370, 310 }, ImGuiCond_Once);
        ImGui::SetNextWindowPos(popupPos, ImGuiCond_Appearing, { 0.5f, 0.5f });
        ImGui::OpenPopup("Unsaved Changes##Editor");
    }

    if (ImGui::BeginPopupModal("Unsaved Changes##Editor", nullptr, ImGuiWindowFlags_NoResize)) {
        ImGui::Text("You have unsaved changes in the following files:");
        ImGui::Separator();
        if (ImGui::BeginListBox("##Unsaved Files")) {
            for (const auto& editor : editors) {
                ImGui::Text("%s", editor->getPath().filename().string().c_str());
            }

            ImGui::EndListBox();
        }

        ImGui::Separator();

        if (ImGui::Button("Save")) {
            for (const auto& editor : editors) {
                editor->save();
                g_projectManager->closeEditor(editor);
            }

            g_projectManager->clearUnsavedEditors();
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();

        if (ImGui::Button("Don't Save")) {
            for (const auto& editor : editors) {
                g_projectManager->closeEditor(editor, true);
            }

            g_projectManager->clearUnsavedEditors();
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();

        if (ImGui::Button("Cancel")) {
            g_projectManager->clearUnsavedEditors();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void Editor::renderParticles() {
    const auto editor = g_projectManager->getActiveEditorAs<EditorInstance>();
    if (!editor) {
        return;
    }

    std::vector<Renderer*> renderers = { m_gridRenderer.get() };

    editor->renderDebugShapes(renderers);
    editor->renderParticles(renderers);
}

void Editor::renderMenu(std::string_view name) {
    if (name == "View") {
        bool saveConfig = false;
        saveConfig |= ImGui::MenuItemIcon(ICON_FA_BRUSH, "Display Active Emitters", nullptr, & m_settings.displayActiveEmitters);
        saveConfig |= ImGui::MenuItemIcon(ICON_FA_BRUSH, "Display Edited Emitter", nullptr, &m_settings.displayEditedEmitter);
        if (ImGui::MenuItemIcon(ICON_FA_EYE, "Use Ortho Camera", nullptr, &m_settings.useOrthographicCamera)) {
            saveConfig = true;
            for (const auto& instance : g_projectManager->getOpenEditors()) {
                const auto particleEditor = std::dynamic_pointer_cast<EditorInstance>(instance);
                if (!particleEditor) {
                    continue;
                }

                particleEditor->getCamera().setProjection(
                    m_settings.useOrthographicCamera ? CameraProjection::Orthographic : CameraProjection::Perspective
                );
            }
        }

        if (saveConfig) {
            g_application->saveConfig();
        }
    }

    if (name == "Edit") {
        if (ImGui::MenuItemIcon(ICON_FA_GEAR, "Editor Settings", nullptr)) {
            openSettings();
        }
    }

    if (name == "Help") {
        if (ImGui::MenuItemIcon(ICON_FA_BOOK, "Quick Tutorial", nullptr)) {
            openTutorial();
        }
    }
}

void Editor::renderToolbar(float itemHeight) {
    ImGui::VerticalSeparator(itemHeight);

    constexpr float framePadding = 2.0f;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { framePadding, framePadding });
    ImGui::PushStyleColor(ImGuiCol_Header, m_settings.useFixedDsResolution ? AppColors::DarkGray : 0);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, AppColors::DarkGray);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, AppColors::DarkGray2);

    ImVec2 size = { ImGui::CalcTextSize("DS Resolution").x + 2.0f * framePadding, itemHeight };
    ImGui::Selectable("DS Resolution", &m_settings.useFixedDsResolution, ImGuiSelectableFlags_None, size);

    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();

    ImGui::VerticalSeparator(itemHeight);

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { framePadding, framePadding });
    ImGui::PushStyleColor(ImGuiCol_Header, m_settings.useLegacyParticleRenderer ? AppColors::DarkGray : 0);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, AppColors::DarkGray);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, AppColors::DarkGray2);

    size = { ImGui::CalcTextSize("Accurate Renderer").x + 2.0f * framePadding, itemHeight };
    ImGui::Selectable("Accurate Renderer", &m_settings.useLegacyParticleRenderer, ImGuiSelectableFlags_None, size);

    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
}

void Editor::openPicker() {
    m_pickerOpen = true;
}

void Editor::openEditor() {
    m_editorOpen = true;
}

void Editor::openTextureManager() {
    m_textureManagerOpen = true;
}


void Editor::openSettings() {
    if (m_settingsOpen) {
        return;
    }

    m_settingsBackup = m_settings;
    m_settingsOpen = true;
    ImGui::PushOverrideID(m_settingsWindowId);
    ImGui::OpenPopup("Settings##Editor");
    ImGui::ResetPopupFade("Settings##Editor");
    ImGui::PopID();
}

void Editor::openTutorial() {
    ImGui::PushOverrideID(m_tutorialWindowId);
    ImGui::OpenPopup("##ViewportTutorial");
    ImGui::ResetPopupFade("##ViewportTutorial");
    ImGui::PopID();
}

void Editor::onEditorOpened(const std::shared_ptr<EditorInstance> &editor) {
    if (m_showTutorial) {
        openTutorial();
        m_showTutorial = false;
    }
}

void Editor::onEditorRenamed(const std::filesystem::path& oldPath, const std::filesystem::path& newPath) {
    const auto editor = g_projectManager->getEditor(oldPath);
    if (const auto particleEditor = std::dynamic_pointer_cast<EditorInstance>(editor)) {
        particleEditor->rename(newPath);
    }
}

void Editor::save() {
    const auto& editor = g_projectManager->getActiveEditor();
    if (!editor) {
        return;
    }

    editor->save();
}

void Editor::saveAs(const std::filesystem::path& path) {
    const auto& editor = g_projectManager->getActiveEditor();
    if (!editor) {
        return;
    }

    editor->saveAs(path);
}

void Editor::loadConfig(const nlohmann::json& config) {
    if (!config.contains("settings")) {
        return;
    }

    constexpr auto loadVec4 = [](const nlohmann::json& j, const char* name, const glm::vec4& def = {}) {
        if (!j.contains(name)) {
            return def;
        }

        const auto& vec = j[name];
        if (vec.size() != 4) {
            return def;
        }

        return glm::vec4(
            vec[0].get<float>(),
            vec[1].get<float>(),
            vec[2].get<float>(),
            vec[3].get<float>()
        );
    };

    const auto& settings = config["settings"];
    m_settings.displayActiveEmitters = settings.value<bool>("displayActiveEmitters", m_settingsDefault.displayActiveEmitters);
    m_settings.displayEditedEmitter = settings.value<bool>("displayEditedEmitter", m_settingsDefault.displayEditedEmitter);
    m_settings.useOrthographicCamera = settings.value<bool>("useOrthographicCamera", m_settingsDefault.useOrthographicCamera);
    m_settings.activeEmitterColor = loadVec4(settings, "activeEmitterColor", m_settingsDefault.activeEmitterColor);
    m_settings.editedEmitterColor = loadVec4(settings, "editedEmitterColor", m_settingsDefault.editedEmitterColor);
    m_settings.collisionPlaneBounceColor = loadVec4(settings, "collisionPlaneBounceColor", m_settingsDefault.collisionPlaneBounceColor);
    m_settings.collisionPlaneKillColor = loadVec4(settings, "collisionPlaneKillColor", m_settingsDefault.collisionPlaneKillColor);
    m_settings.backgroundColor = loadVec4(settings, "backgroundColor", m_settingsDefault.backgroundColor);
    m_settings.maxParticles = settings.value("maxParticles", m_settingsDefault.maxParticles);
    m_settings.useFixedDsResolution = settings.value("useFixedDsResolution", m_settingsDefault.useFixedDsResolution);
    m_settings.fixedDsResolutionScale = settings.value("fixedDsResolutionScale", m_settingsDefault.fixedDsResolutionScale);
    m_settings.useLegacyParticleRenderer = settings.value("useLegacyParticleRenderer", m_settingsDefault.useLegacyParticleRenderer);
    m_showTutorial = settings.value("showTutorial", true);
}

void Editor::saveConfig(nlohmann::json& config) const {
    constexpr auto saveVec4 = [](const glm::vec4& vec) {
        return nlohmann::json::array({ vec.x, vec.y, vec.z, vec.w });
    };

    config["settings"] = nlohmann::json::object({
        { "displayActiveEmitters", m_settings.displayActiveEmitters },
        { "displayEditedEmitter", m_settings.displayEditedEmitter },
        { "useOrthographicCamera", m_settings.useOrthographicCamera },
        { "activeEmitterColor", saveVec4(m_settings.activeEmitterColor) },
        { "editedEmitterColor", saveVec4(m_settings.editedEmitterColor) },
        { "collisionPlaneBounceColor", saveVec4(m_settings.collisionPlaneBounceColor) },
        { "collisionPlaneKillColor", saveVec4(m_settings.collisionPlaneKillColor) },
        { "backgroundColor", saveVec4(m_settings.backgroundColor) },
        { "maxParticles", m_settings.maxParticles },
        { "useFixedDsResolution", m_settings.useFixedDsResolution },
        { "fixedDsResolutionScale", m_settings.fixedDsResolutionScale },
        { "useLegacyParticleRenderer", m_settings.useLegacyParticleRenderer },
        { "showTutorial", m_showTutorial }
    });
}

bool Editor::canUndo() const {
    const auto editor = g_projectManager->getActiveEditorAs<EditorInstance>();
    if (!editor) {
        return false;
    }

    return editor->canUndo();
}

bool Editor::canRedo() const {
    const auto editor = g_projectManager->getActiveEditorAs<EditorInstance>();
    if (!editor) {
        return false;
    }

    return editor->canRedo();
}

void Editor::undo() {
    const auto editor = g_projectManager->getActiveEditorAs<EditorInstance>();
    if (!editor) {
        return;
    }

    if (editor->undo() == EditorActionType::ResourceAdd) {
        editor->ensureValidSelection();
    }
}

void Editor::redo() {
    const auto editor = g_projectManager->getActiveEditorAs<EditorInstance>();
    if (!editor) {
        return;
    }

    if (editor->redo() == EditorActionType::ResourceRemove) {
        editor->ensureValidSelection();
    }
}

void Editor::pushClipboard(const std::string& source, const SPLResource& res) {
    if (m_clipboardHistory.size() >= 10) {
        m_clipboardHistory.pop();
    }

    m_clipboardHistory.emplace(
        source,
        std::make_unique<SPLResource>(res.duplicate()),
        nullptr
    );
}

void Editor::pushClipboard(const std::string& source, const SPLTexture& tex) {
    if (m_clipboardHistory.size() >= 10) {
        m_clipboardHistory.pop();
    }

    m_clipboardHistory.emplace(
        source,
        nullptr,
        std::make_unique<SPLTextureCopy>(tex.copy())
    );
}

void Editor::pushClipboard(const std::string& source, const SPLResource& res, const SPLTexture& tex) {
    if (m_clipboardHistory.size() >= 10) {
        m_clipboardHistory.pop();
    }

    m_clipboardHistory.emplace(
        source,
        std::make_unique<SPLResource>(res.duplicate()),
        std::make_unique<SPLTextureCopy>(tex.copy())
    );
}

void Editor::playEmitter(EmitterSpawnType spawnType) {
    if (const auto editor = g_projectManager->getActiveEditorAs<EditorInstance>()) {
        editor->playEmitter(spawnType);
    }
}

void Editor::playAllEmitters(EmitterSpawnType spawnType) {
    if (const auto editor = g_projectManager->getActiveEditorAs<EditorInstance>()) {
        editor->playAllEmitters(spawnType);
    }
}

void Editor::killEmitters() {
    if (const auto editor = g_projectManager->getActiveEditorAs<EditorInstance>()) {
        editor->killEmitters();
    }
}

void Editor::resetCamera() {
    if (const auto editor = g_projectManager->getActiveEditorAs<EditorInstance>()) {
        editor->resetCamera();
    }
}

void Editor::handleEvent(const SDL_Event& event) {
    const auto& editor = g_projectManager->getActiveEditor();
    if (!editor) {
        return;
    }

    editor->handleEvent(event);
}

void Editor::renderSettings() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushOverrideID(m_settingsWindowId);
    ImGui::BeginPopupFade("Settings##Editor");

    bool closedThroughButton = false;
    if (ImGui::BeginPopupModal("Settings##Editor", &m_settingsOpen)) {
        // Reserve a footer area for action buttons and make the main content scrollable
        const ImGuiStyle& style = ImGui::GetStyle();
        const float footerReserve = ImGui::GetFrameHeightWithSpacing() + style.ItemSpacing.y; // reserve approx one button row

        ImGui::BeginChild(
            ImGui::GetID("##SettingsFrame"),
            ImVec2(0, -footerReserve),
            ImGuiChildFlags_Borders,
            ImGuiWindowFlags_AlwaysVerticalScrollbar
        );

        if (ImGui::IsWindowAppearing()) {
            iam_scroll_to_top();
        }

        ImGui::SeparatorText("General");
        ImGui::InputScalar("Max Particles", ImGuiDataType_U32, &m_settings.maxParticles);
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("The maximum number of particles that can be processed/rendered at once per editor.\n"
                              "Note that games using SPL usually have a limit of around 1000.");
        }

        ImGui::SeparatorText("Colors");
        ImGui::ColorEdit4("Active Emitter Color", glm::value_ptr(m_settings.activeEmitterColor));
        ImGui::ColorEdit4("Edited Emitter Color", glm::value_ptr(m_settings.editedEmitterColor));
        ImGui::ColorEdit4("Collision Plane Bounce Color", glm::value_ptr(m_settings.collisionPlaneBounceColor));
        ImGui::ColorEdit4("Collision Plane Kill Color", glm::value_ptr(m_settings.collisionPlaneKillColor));
        ImGui::ColorEdit4("Background Color", glm::value_ptr(m_settings.backgroundColor));

        ImGui::SeparatorText("Rendering");
        bool changed = false;
        bool swapRenderer = false;

        changed |= ImGui::Checkbox("Use DS Resolution", &m_settings.useFixedDsResolution);
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("If enabled, particles will render at the Nintendo DS' native resolution of "
                              "256x192 * <scale>");
        }

        if (m_settings.useFixedDsResolution) {
            changed |= ImGui::SliderInt("DS Resolution Scale", &m_settings.fixedDsResolutionScale, 1, 8);
            m_settings.fixedDsResolutionScale = glm::clamp(m_settings.fixedDsResolutionScale, 1, 8);
        }

        if (ImGui::Checkbox("Use Accurate Renderer", &m_settings.useLegacyParticleRenderer)) {
            changed = true;
            swapRenderer = true;
        }

        if (changed) {
            updateRenderSettings(swapRenderer); // Update all open editors
        }

        ImGui::EndChild();

        if (ImGui::Button("Reset to Defaults")) {
            m_settings = m_settingsDefault;
        }

        ImGui::SameLine();

        if (ImGui::Button("Save")) {
            if (m_settings.maxParticles != m_settingsBackup.maxParticles) {
                updateMaxParticles(); // Apply the setting to all open editors
            }

            m_settingsBackup = m_settings;
            m_settingsOpen = false;
            closedThroughButton = true;
            ImGui::CloseCurrentPopup();

            g_application->saveConfig();
        }

        ImGui::SameLine();

        if (ImGui::Button("Cancel")) {
            m_settings = m_settingsBackup;
            m_settingsOpen = false;
            closedThroughButton = true;
            ImGui::CloseCurrentPopup();
        }

        const auto avail = ImGui::GetContentRegionAvail();
        const auto buttonWidth = ImGui::CalcTextSize("Open Config Directory").x + ImGui::GetStyle().FramePadding.x * 2;
        ImGui::SameLine(0.0f, avail.x - buttonWidth * 2.63f);

        if (ImGui::Button("Open Config Directory")) {
            const auto path = Application::getConfigPath();
            if (!std::filesystem::exists(path)) {
                std::filesystem::create_directories(path);
            }
            SDL_OpenURL(("file://" + path.string()).c_str());
        }

        ImGui::EndPopup();
    }

    // If the window was closed through the 'x' button, we want to restore the settings
    if (!m_settingsOpen && !closedThroughButton) {
        m_settings = m_settingsBackup;
    }

    ImGui::EndPopupFade();
    ImGui::PopID();
    ImGui::PopStyleVar();
}

void Editor::renderTutorial() {
    ImGui::PushOverrideID(m_tutorialWindowId);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 16, 16 });
    ImGui::BeginPopupFade("##ViewportTutorial");

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, { 0.5f, 0.5f });
    ImGui::SetNextWindowSize({ 900, 0 }, ImGuiCond_Once);

    if (ImGui::BeginPopupModal("##ViewportTutorial", nullptr, ImGuiWindowFlags_NoDecoration)) {
        const auto icon = g_application->getIcon();
        const auto windowSize = ImGui::GetWindowSize();
        if (icon) {
            constexpr float iconSize = 64.0f;
            ImGui::SetCursorPosX((windowSize.x - iconSize) * 0.5f);
            ImGui::Image(icon->getHandle(), { iconSize, iconSize });
        }

        ImGui::PushFont(nullptr, Application::getFontSizeLarge());

        constexpr auto title = "NitroEFX Quick Tutorial";
        const auto titleSize = ImGui::CalcTextSize(title);

        ImGui::SetCursorPosX((windowSize.x - titleSize.x) * 0.5f);
        ImGui::TextUnformatted(title);

        ImGui::PopFont();

        ImGui::Separator();

        ImGui::TextDisabled("Tips to navigate the viewport and get started.");

        ImGui::Spacing();

        if (ImGui::BeginTable("##tutorial_layout", 2, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("left", ImGuiTableColumnFlags_WidthStretch, 1.3f);
            ImGui::TableSetupColumn("right", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableNextRow();

            // Left: Camera controls
            ImGui::TableSetColumnIndex(0);
            ImGui::SeparatorText("Camera Controls");
            if (ImGui::BeginTable("##controls_table", 2, ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersH | ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("How", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Move Camera");
                ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted("Alt + Left-Drag");

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Zoom Camera");
                ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted("Alt + Right-Drag or Mouse Wheel");

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Pan Camera");
                ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted("Alt + Middle-Drag");

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Reset Camera");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%s", g_application->getKeybind(ApplicationAction::ResetCamera)->toString().c_str());

                ImGui::EndTable();
            }

            ImGui::Spacing();
            ImGui::SeparatorText("Hints");
            ImGui::BulletText("Use DS Resolution to preview at DS aspect.");
            ImGui::BulletText("Use the Global Time Scale to slow down fast effects.");
            ImGui::BulletText("Right-click in the Resource List to add new resources.");
            ImGui::BulletText("Use Ctrl+C and Ctrl+V to copy/paste resources and textures.");
            ImGui::BulletText("Open the Texture Manager to import and manage textures.");

            // Right: Viewport options and links
            ImGui::TableSetColumnIndex(1);
            bool changed = false;
            bool swapRenderer = false;

            ImGui::SeparatorText("Viewport Options");
            changed |= ImGui::Checkbox("Display Active Emitters", &m_settings.displayActiveEmitters);
            changed |= ImGui::Checkbox("Display Edited Emitter", &m_settings.displayEditedEmitter);

            if (ImGui::Checkbox("Use DS Resolution", &m_settings.useFixedDsResolution)) {
                changed = true;
            }
            if (m_settings.useFixedDsResolution) {
                ImGui::SetNextItemWidth(-FLT_MIN);
                changed |= ImGui::SliderInt("DS Resolution Scale", &m_settings.fixedDsResolutionScale, 1, 8);
                m_settings.fixedDsResolutionScale = glm::clamp(m_settings.fixedDsResolutionScale, 1, 8);
            }

            if (ImGui::Checkbox("Use Accurate Renderer", &m_settings.useLegacyParticleRenderer)) {
                changed = true;
                swapRenderer = true;
            }

            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::SliderFloat("Global Time Scale", &m_timeScale, 0.0f, 2.0f, "%.2f");

            if (changed) {
                updateRenderSettings(swapRenderer);
                g_application->saveConfig();
            }

            ImGui::Spacing();
            ImGui::SeparatorText("Resources");
            ImGui::TextLinkOpenURL("GitHub Repository", "https://github.com/Fexty12573/nitroefx");
            ImGui::TextLinkOpenURL("Report an Issue", "https://github.com/Fexty12573/nitroefx/issues/new");
            ImGui::TextLinkOpenURL("Releases", "https://github.com/Fexty12573/nitroefx/releases");

            ImGui::EndTable();
        }

        ImGui::Spacing();
        ImGui::Separator();

        ImGui::TextDisabled("You can always open this window via Help > Quick Tutorial.");
        ImGui::SameLine();

        constexpr float buttonWidth = 100.0f;
        ImGui::SetCursorPosX(windowSize.x - buttonWidth - ImGui::GetStyle().WindowPadding.x);
        if (ImGui::Button("Got it!", { buttonWidth, 0 })) {
            g_application->saveConfig();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    ImGui::EndPopupFade();
    ImGui::PopStyleVar();
    ImGui::PopID();
}

void Editor::updateRenderSettings(bool swapRenderer) {
    const auto legacyRenderer = m_settings.useLegacyParticleRenderer;
    const auto editors = g_projectManager->getOpenEditors();

    for (const auto& editor : editors) {
        const auto particleEditor = std::dynamic_pointer_cast<EditorInstance>(editor);
        if (!particleEditor) {
            continue;
        }

        particleEditor->updateViewportSize();

        if (swapRenderer) {
            if (legacyRenderer) {
                particleEditor->useLegacyRenderer();
            } else {
                particleEditor->useModernRenderer();
            }
        }
    }
}

void Editor::updateMaxParticles() {
    const auto editors = g_projectManager->getOpenEditors();
    for (const auto& editor : editors) {
        if (const auto particleEditor = std::dynamic_pointer_cast<EditorInstance>(editor)) {
            particleEditor->setMaxParticles(m_settings.maxParticles);
        }
    }
}
