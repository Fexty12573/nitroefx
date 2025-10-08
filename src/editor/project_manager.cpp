#include "project_manager.h"
#include "application.h"
#include "imgui/extensions.h"
#include "fonts/IconsFontAwesome6.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <SDL3/SDL_messagebox.h>
#include <spdlog/spdlog.h>
#include <narc/narc.h>
#include <narc/defs/fimg.h>
#include <fstream>


void ProjectManager::init(Editor* editor) {
    m_mainEditor = editor;
    m_watcher = std::make_unique<efsw::FileWatcher>();
    m_listener = std::make_unique<FileWatchListener>(this);
}

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

    // Reset cache and watcher
    m_directoryCache.clear();
    if (std::filesystem::exists(m_projectPath)) {
        buildDirectoryCache(m_projectPath);
        try {
            m_watcher->addWatch(m_projectPath.string(), m_listener.get(), true);
            m_watcher->watch();
        } catch (const std::exception& e) {
            spdlog::error("Failed to add file watch: {}", e.what());
        }
    }
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
        m_directoryCache.clear();
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
    
    m_mainEditor->onEditorOpened(editor);
}

void ProjectManager::openEditor() {
    const auto editor = std::make_shared<EditorInstance>();
    m_openEditors.push_back(editor);

    m_activeEditor = editor;
    m_forceActivate = true;

    m_mainEditor->onEditorOpened(editor);
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

    m_mainEditor->onEditorOpened(editor);
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
                ensureDirectoryCached(m_projectPath);
                auto& entries = m_directoryCache[m_projectPath];
                for (const auto& entry : entries) {
                    if (!m_searchString.empty() && !entry.isDirectory) {
                        const auto name = entry.path.filename().string();
                        if (!name.contains(m_searchString)) {
                            continue;
                        }
                    }

                    if (entry.isDirectory) {
                        renderDirectory(entry.path);
                    } else {
                        renderFile(entry.path);
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
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;

    const bool open = ImGui::TreeNodeEx(text.c_str(), flags);

    if (ImGui::BeginPopupContextItem(nullptr, ImGuiPopupFlags_MouseButtonRight)) {
        if (ImGui::MenuItem("New file")) {
            m_inlineMode = InlineEditMode::CreateFile;
            m_inlineEditTargetDir = path;
            m_inlineEditBuffer[0] = '\0';
            m_inlineEditFocusRequested = true;
        }

        ImGui::EndPopup();
    }

    if (open) {
        ensureDirectoryCached(path);
        auto& children = m_directoryCache[path];
        for (const auto& child : children) {
            if (!m_searchString.empty() && !child.isDirectory) {
                const auto name = child.path.filename().string();
                if (!name.contains(m_searchString)) {
                    continue;
                }
            }

            if (child.isDirectory) {
                renderDirectory(child.path);
            } else {
                renderFile(child.path);
            }
        }

        if (m_inlineMode == InlineEditMode::CreateFile && m_inlineEditTargetDir == path) {
            ImGui::Indent(40.0f);
            ImGui::PushItemWidth(-1);

            if (m_inlineEditFocusRequested) {
                ImGui::SetKeyboardFocusHere();
                m_inlineEditFocusRequested = false;
            }

            if (ImGui::InputTextWithHint("##newFile_name", "New file name...",
                m_inlineEditBuffer, IM_ARRAYSIZE(m_inlineEditBuffer),
                ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
                const std::string name{ m_inlineEditBuffer };
                if (!name.empty()) {
                    const auto newPath = path / name;
                    if (!std::filesystem::exists(newPath)) {
                        SPLArchive::saveDefault(newPath);
                        onFileAdded(path, name);
                        openEditor(newPath);
                    }
                }

                cancelInlineEdit();
            }

            const bool deactivated = ImGui::IsItemDeactivated();
            if (deactivated && (ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsMouseClicked(ImGuiMouseButton_Left))) {
                cancelInlineEdit();
            }

            ImGui::PopItemWidth();
            ImGui::Unindent(40.0f);
        }

        ImGui::TreePop();
    }
}

void ProjectManager::renderFile(const std::filesystem::path& path) {
    const auto text = fmt::format(ICON_FA_FILE " {}", path.filename().string());
    const bool isSplFile = s_spaExtensions.contains(path.extension().string());
    if (!isSplFile) {
        if (m_hideOtherFiles) {
            return;
        }

        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    }

    ImGui::Indent(40.0f);

    const bool isRenamingThis = (m_inlineMode == InlineEditMode::RenameFile && m_inlineEditPathOld == path);
    if (isRenamingThis) {
        ImGui::PushItemWidth(-1);

        if (m_inlineEditFocusRequested) {
            ImGui::SetKeyboardFocusHere();
            m_inlineEditFocusRequested = false;
        }

        if (ImGui::InputText("##rename", m_inlineEditBuffer, IM_ARRAYSIZE(m_inlineEditBuffer),
            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
            const std::string name{ m_inlineEditBuffer };
            if (!name.empty()) {
                const auto newPath = path.parent_path() / name;
                std::error_code ec;
                std::filesystem::rename(path, newPath, ec);
                if (ec) {
                    spdlog::error("Rename failed: {}", ec.message());
                } else {
                    m_mainEditor->onEditorRenamed(path, newPath);
                    onFileMoved(path.parent_path(), path.filename().string(), name);
                }
            }

            cancelInlineEdit();
        }

        const bool deactivated = ImGui::IsItemDeactivated();
        if (deactivated && (ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsMouseClicked(ImGuiMouseButton_Left))) {
            cancelInlineEdit();
        }

        ImGui::PopItemWidth();
    } else {
        if (ImGui::Selectable(text.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick) && isSplFile) {
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                openEditor(path);
            }
            else {
                openTempEditor(path);
            }
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

        if (ImGui::MenuItemIcon(ICON_FA_PEN_TO_SQUARE, "Rename")) {
            m_inlineMode = InlineEditMode::RenameFile;
            m_inlineEditPathOld = path;
            std::snprintf(m_inlineEditBuffer, IM_ARRAYSIZE(m_inlineEditBuffer), "%s", path.filename().string().c_str());
            m_inlineEditFocusRequested = true;
        }

        if (ImGui::MenuItemIcon(ICON_FA_TRASH, "Delete")) {
            spdlog::info("Deleting file: {}", path.string());
            std::filesystem::remove(path);
            onFileDeleted(path.parent_path(), path.filename().string());
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

void ProjectManager::cancelInlineEdit() {
    m_inlineMode = InlineEditMode::None;
    m_inlineEditPathOld.clear();
    m_inlineEditTargetDir.clear();
    m_inlineEditBuffer[0] = '\0';
    m_inlineEditFocusRequested = false;
}

void ProjectManager::buildDirectoryCache(const std::filesystem::path& directory) {
    std::vector<CachedEntry> entries;
    if (!std::filesystem::exists(directory)) {
        m_directoryCache.erase(directory);
        return;
    }

    std::error_code ec;
    for (const auto& dirEntry : std::filesystem::directory_iterator(directory, ec)) {
        if (ec) break;
        CachedEntry e{ dirEntry.path(), dirEntry.is_directory() };
        entries.push_back(std::move(e));
    }

    sortCached(entries);
    m_directoryCache[directory] = std::move(entries);
}

void ProjectManager::sortCached(std::vector<CachedEntry>& entries) {
    std::ranges::sort(entries, [](const CachedEntry& a, const CachedEntry& b) {
        if (a.isDirectory != b.isDirectory) return a.isDirectory > b.isDirectory; // dirs first
        return a.path.filename().string() < b.path.filename().string();
    });
}

void ProjectManager::onFileAdded(const std::filesystem::path& parentDir, const std::string& name) {
    const auto it = m_directoryCache.find(parentDir);
    if (it == m_directoryCache.end()) {
        return; // parent is not cached yet, ignore event
    }

    const auto fullPath = parentDir / name;
    if (!std::filesystem::exists(fullPath)) {
        return;
    }

    auto& vec = it->second;
    if (std::ranges::any_of(vec, [&](const CachedEntry& e) { return e.path == fullPath; })) {
        return; // already in cache
    }

    vec.emplace_back(fullPath, std::filesystem::is_directory(fullPath));
    sortCached(vec);
}

void ProjectManager::onFileDeleted(const std::filesystem::path& parentDir, const std::string& name) {
    const auto it = m_directoryCache.find(parentDir);
    if (it == m_directoryCache.end()) {
        return; // parent is not cached yet, ignore event
    }

    const auto fullPath = parentDir / name;
    auto& vec = it->second;
    vec.erase(std::remove_if(vec.begin(), vec.end(), [&](const CachedEntry& e) {
        return e.path == fullPath;
    }), vec.end());

    m_directoryCache.erase(fullPath);
}

void ProjectManager::onFileModified(const std::filesystem::path& file) {
    (void)file;
    // TODO: Maybe consider asking editors to reload?
}

void ProjectManager::onFileMoved(const std::filesystem::path& parentDir, const std::string& oldName, const std::string& newName) {
    const auto it = m_directoryCache.find(parentDir);
    if (it == m_directoryCache.end()) {
        return; // parent is not cached yet, ignore event
    }

    const auto oldPath = parentDir / oldName;
    const auto newPath = parentDir / newName;

    auto& vec = it->second;
    for (auto& e : vec) {
        if (e.path == oldPath) {
            const bool wasDir = e.isDirectory;
            e.path = newPath;
            e.isDirectory = std::filesystem::is_directory(newPath);
            if (wasDir) {
                // Move children cache (rename key)
                auto childCacheIt = m_directoryCache.find(oldPath);
                if (childCacheIt != m_directoryCache.end()) {
                    m_directoryCache[newPath] = std::move(childCacheIt->second);
                    m_directoryCache.erase(childCacheIt);
                }
            }

            break;
        }
    }

    sortCached(vec);
}
