#include "project_manager.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <SDL3/SDL_messagebox.h>
#include <spdlog/spdlog.h>
#include <narc/narc.h>
#include <narc/defs/fimg.h>

#include "imgui/extensions.h"
#include "fonts/IconsFontAwesome6.h"


void ProjectManager::openProject(const std::filesystem::path& path) {
    if (hasProject()) {
        constexpr SDL_MessageBoxButtonData buttons[] = {
            { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "No" },
            { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Yes" }
        };
        const SDL_MessageBoxData data = {
            SDL_MESSAGEBOX_INFORMATION,
            nullptr,
            "Close project?",
            "You already have a project open. Do you want to close it?",
            2,
            buttons,
            nullptr
        };

        int button = 0;
        const auto result = SDL_ShowMessageBox(&data, &button);
        if (!result || button == 0) {
            return;
        }

        closeProject(true);
    }

    m_projectPath = path;
}

void ProjectManager::closeProject(bool force) {
    // TODO: Check if any editors are modified and prompt to save
    bool canClose = true;
    for (const auto& editor : m_openEditors) {
        canClose &= editor->notifyClosing();
    }

    if (canClose || force) {
        m_activeEditor.reset();
        m_projectPath.clear();
        m_openEditors.clear();
    }
}

void ProjectManager::openEditor(const std::filesystem::path& path) {
    const auto existing = getEditor(path);
    if (existing) {
        m_activeEditor = existing;
        m_forceActivate = true;
        existing->makePermanent();
        return;
    }

    if (!SPLArchive::isValid(path)) {
       spdlog::error("Invalid SPL archive: {}", path.string());
       return;
    }

    const auto editor = std::make_shared<EditorInstance>(path);
    if (m_openEditors.empty()) {
        m_activeEditor = editor;
    }

    m_openEditors.push_back(editor);
}

void ProjectManager::openEditor() {
    const auto editor = std::make_shared<EditorInstance>();
    m_openEditors.push_back(editor);

    m_activeEditor = editor;
    m_forceActivate = true;
}

void ProjectManager::openTempEditor(const std::filesystem::path& path) {
    const auto existing = getEditor(path);
    if (existing) {
        m_activeEditor = existing;
        m_forceActivate = true;
        existing->makePermanent();
        return;
    }

    if (!SPLArchive::isValid(path)) {
        spdlog::error("Invalid SPL archive: {}", path.string());
        return;
    }

    closeTempEditor();
    const auto editor = std::make_shared<EditorInstance>(path, true);
    m_openEditors.push_back(editor);
    m_activeEditor = editor;
}

void ProjectManager::openNarcProject(const std::filesystem::path& path) {
    narc_error err = narc_load(path.string().c_str(), &m_narc, &m_vfsCtx);
    if (err != NARCERR_NONE) {
        spdlog::error("Failed to load NARC archive: {} (error: {})", path.string(), narc_strerror(err));
        return;
    }

    m_fatbMeta = (fatb_meta*)(m_narc->vfs + m_vfsCtx.fatb_ofs);
    m_fatb = (fatb_entry*)(m_narc->vfs + m_vfsCtx.fatb_ofs + sizeof(*m_fatbMeta));
    m_fimg = (char*)(m_narc->vfs + m_vfsCtx.fimg_ofs + sizeof(fimg_meta));

    m_isNarc = true;
}

void ProjectManager::closeEditor(const std::shared_ptr<EditorInstance>& editor, bool force) {
    if (!force && editor->isModified()) {
        m_unsavedEditors.push_back(editor);
        return;
    }

    if (force || editor->notifyClosing()) {
        std::erase(m_openEditors, editor);
        if (m_activeEditor == editor) {
            m_activeEditor.reset();
        }
    }
}

void ProjectManager::closeTempEditor() {
    const auto it = std::ranges::find_if(m_openEditors, [](const auto& editor) {
        return editor->isTemp();
    });

    if (it != m_openEditors.end()) {
        // Force shouldn't be necessary because temp editors are never modified but just in case
        closeEditor(*it, true);
    }
}

void ProjectManager::closeAllEditors() {
    const auto editorList = m_openEditors; // Copy the list to avoid modifying it while iterating
    for (const auto& editor : editorList) {
        closeEditor(editor);
    }
}

void ProjectManager::saveAllEditors() {
    for (const auto& editor : m_openEditors) {
        editor->save();
    }
}

void ProjectManager::open() {
    m_open = true;
}

void ProjectManager::render() {
    if (!m_open) {
        return;
    }

    if (ImGui::Begin("Project Manager##ProjectManager", &m_open)) {
        if (m_projectPath.empty() && !m_narc) {
            ImGui::Text("No project open");
        } else {
            if (ImGui::CollapsingHeader("Settings")) {
                ImGui::Checkbox("Hide non .spa files", &m_hideOtherFiles);
            }
            
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##Filter", "Search by name...", &m_searchString);

            ImGui::BeginChild("##ProjectManagerFiles", {}, ImGuiChildFlags_Border);

            if (m_isNarc) {
                for (size_t i = 0; i < m_fatbMeta->num_files; i++) {
                    const auto name = fmt::format("{}{}", i, narc_files_getext(m_fimg + m_fatb[i].start));
                    if (m_searchString.empty() || name.contains(m_searchString)) {
                        renderNarcFile(name, i);
                    }
                }
            } else {
                for (const auto& entry : std::filesystem::directory_iterator(m_projectPath)) {
                    if (entry.is_directory()) {
                        renderDirectory(entry.path());
                    } else {
                        if (m_searchString.empty() || entry.path().filename().string().contains(m_searchString)) {
                            renderFile(entry.path());
                        }
                    }
                }
            }

            ImGui::EndChild();
        }
    }

    ImGui::End();
}

void ProjectManager::handleEvent(const SDL_Event& event) {
    switch (event.type) {
    case SDL_EVENT_DROP_FILE: {
        const std::filesystem::path path = event.drop.data;
        if (std::filesystem::is_directory(path)) {
            openProject(path);
        } else if (path.extension() == ".spa") {
            openEditor(path);
        }
    } break;
    default:
        break;
    }
}

void ProjectManager::openEditor(size_t narcIndex) {
    const auto existing = getNarcEditor(narcIndex);
    if (existing) {
        m_activeEditor = existing;
        m_forceActivate = true;
        existing->makePermanent();
        return;
    }

    const auto& fatb = m_fatb[narcIndex];
    const std::span data(m_fimg + fatb.start, fatb.end - fatb.start);
    const auto editor = std::make_shared<EditorInstance>(narcIndex, data);

    m_activeEditor = editor;
    m_openEditors.push_back(editor);
}

void ProjectManager::openTempEditor(size_t narcIndex) {
    const auto existing = getNarcEditor(narcIndex);
    if (existing) {
        m_activeEditor = existing;
        m_forceActivate = true;
        existing->makePermanent();
        return;
    }

    const auto& fatb = m_fatb[narcIndex];
    const std::span data(m_fimg + fatb.start, fatb.end - fatb.start);
    closeTempEditor();

    const auto editor = std::make_shared<EditorInstance>(narcIndex, data, true);
    m_openEditors.push_back(editor);
    m_activeEditor = editor;
}

void ProjectManager::renderDirectory(const std::filesystem::path& path) {
    const auto text = fmt::format(ICON_FA_FOLDER " {}", path.filename().string());
    const bool open = ImGui::TreeNodeEx(text.c_str(), ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth);

    if (ImGui::BeginPopupContextItem(nullptr, ImGuiPopupFlags_MouseButtonRight)) {
        if (ImGui::MenuItem("New file")) {
            ImGui::CloseCurrentPopup();
            ImGui::OpenPopup("New file##ProjectManager");
        }

        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("New file##ProjectManager")) {
        ImGui::Text("New file");

        ImGui::EndPopup();
    }

    if (open) {
        for (const auto& entry : std::filesystem::directory_iterator(path)) {
            if (entry.is_directory()) {
                renderDirectory(entry.path());
            } else {
                if (m_searchString.empty() || entry.path().string().contains(m_searchString)) {
                    renderFile(entry.path());
                }
            }
        }

        ImGui::TreePop();
    }
}

void ProjectManager::renderFile(const std::filesystem::path& path) {
    const auto text = fmt::format(ICON_FA_FILE " {}", path.filename().string());
    const bool isSplFile = path.extension().string() == ".spa";
    if (!isSplFile) {
        if (m_hideOtherFiles) {
            return;
        }

        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    }

    ImGui::Indent(40.0f);
    if (ImGui::Selectable(text.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick) && isSplFile) {
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            openEditor(path);
        } else {
            openTempEditor(path);
        }
    }
    ImGui::Unindent(40.0f);

    if (!isSplFile) {
        ImGui::PopStyleColor();
        return;
    }

    if (ImGui::BeginPopupContextItem(nullptr, ImGuiPopupFlags_MouseButtonRight)) {
        if (ImGui::MenuItemIcon(ICON_FA_FILE_IMPORT, "Open")) {
            openEditor(path);
        }

        if (ImGui::MenuItemIcon(ICON_FA_TRASH, "Delete")) {
            spdlog::info("Deleting file: {}", path.string());
            std::filesystem::remove(path);
        }

        ImGui::EndPopup();
    }
}

void ProjectManager::renderNarcFile(const std::string& name, size_t index) {
    if (index >= m_fatbMeta->num_files) {
        spdlog::error("Invalid NARC file index: {}", index);
        return;
    }

    const auto& fatb = m_fatb[index];
    const char* data = m_fimg + fatb.start;
    const auto isSplFile = SPLArchive::isValid(std::span(data, fatb.end - fatb.start));

    if (!isSplFile) {
        if (m_hideOtherFiles) {
            return;
        }

        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    }

    ImGui::Indent(40.0f);
    if (ImGui::Selectable(name.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick) && isSplFile) {
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            openEditor(index);
        } else {
            openTempEditor(index);
        }
    }
    ImGui::Unindent(40.0f);

    if (!isSplFile) {
        ImGui::PopStyleColor();
        return;
    }

    if (ImGui::BeginPopupContextItem(nullptr, ImGuiPopupFlags_MouseButtonRight)) {
        if (ImGui::MenuItemIcon(ICON_FA_FILE_IMPORT, "Open")) {
            openEditor(index);
        }

        if (ImGui::MenuItemIcon(ICON_FA_TRASH, "Delete", nullptr, false, 0, false)) {
            spdlog::warn("Deleting NARC files not supported");
        }

        ImGui::EndPopup();
    }
}
