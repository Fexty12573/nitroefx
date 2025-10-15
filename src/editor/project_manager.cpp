#include "project_manager.h"
#include "application.h"
#include "imgui/extensions.h"
#include "fonts/IconsFontAwesome6.h"
#include "util/fzy.h"
#include "util/stream.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <SDL3/SDL_messagebox.h>
#include <spdlog/spdlog.h>
#include <narc/narc.h>
#include <narc/defs/fimg.h>

#include <numeric>
#include <queue>
#include <numeric>
#include <cctype>
#include <limits>
#include <string_view>
#include <charconv>
#include <utility>
#include <optional>
#include <cstdio>
#include "util/wsl.h"


namespace fs = std::filesystem;

namespace helpers {

static char toLowerAscii(char c) {
    return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c;
}
static std::string toLowerAscii(std::string_view sv) {
    std::string s(sv.size(), '\0');
    std::ranges::transform(sv, s.begin(), [](char c) { return toLowerAscii(c); });
    return s;
}

static uint64_t maskChar(char c) {
    // All inputs are already lowercase
    if (c >= 'a' && c <= 'z') return 1ull << (c - 'a');        // 26 bits
    if (c >= '0' && c <= '9') return 1ull << (26 + (c - '0')); // +10 = 36
    switch (c) {                                               // + a few path-ish chars
    case '/': return 1ull << 36;
    case '\\':return 1ull << 37;
    case '.': return 1ull << 38;
    case '_': return 1ull << 39;
    case '-': return 1ull << 40;
    case ' ': return 1ull << 41;
    default: return 0;
    }
}

static uint64_t buildMask(std::string_view sv) {
    uint64_t mask = 0;
    for (char c : sv) {
        mask |= maskChar(c);
    }

    return mask;
}

}

void ProjectManager::init(Editor* editor) {
    m_mainEditor = editor;
    m_watcher = std::make_unique<efsw::FileWatcher>();
    m_listener = std::make_unique<FileWatchListener>(this);
}

void ProjectManager::openProject(const fs::path& path) {
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
        if (!SDL_ShowMessageBox(&data, &button) || button == 0) {
            return;
        }

        closeProject(true);
    }

    m_projectPath = path;

    // Reset cache and watcher
    m_directoryCache.clear();
    m_fuzzyFiles.clear();
    m_fuzzyIndex.clear();
    m_fuzzyIndexBuilt = false;
    m_fuzzyIndexDirty = true;
    m_prevFuzzyQuery.clear();
    m_prevCandidates.clear();

    startFuzzyIndexingAsync();

    if (fs::exists(m_projectPath)) {
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

        // Save index with most recent changes
        saveFuzzyIndex();

        {
            std::scoped_lock lock(m_fuzzyMutex);
            m_fuzzyFiles.clear();
            m_fuzzyIndex.clear();
        }

        if (m_fuzzyIndexThread.joinable()) {
            m_fuzzyIndexThread.request_stop();
            m_fuzzyIndexThread.join();
        }
        m_fuzzyIndexBuilt = false;
        m_fuzzyIndexBuilding = false;
        m_fuzzyOpen = false;
    }
}

void ProjectManager::openEditor(const fs::path& path, bool isRecovered) {
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

    const auto editor = std::make_shared<EditorInstance>(path, false, isRecovered);
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

void ProjectManager::openTempEditor(const fs::path& path) {
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

void ProjectManager::openNarcProject(const fs::path& path) {
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

void ProjectManager::saveAllEditors() const {
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
                const auto quickOpenKeybind = g_application->getKeybind(ApplicationAction::QuickOpen)->toString();
                if (ImGui::Button(fmt::format("Quick Open ({})", quickOpenKeybind).c_str())) {
                    openFileSearch();
                }
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
                        if (!entry.name.contains(m_searchString)) {
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

    if (m_fuzzyOpen) {
        renderFuzzyFinder();
    }

    ImGui::End();
}

void ProjectManager::handleEvent(const SDL_Event& event) {
    switch (event.type) {
    case SDL_EVENT_DROP_FILE: {
        const fs::path path = event.drop.data;
        if (fs::is_directory(path)) {
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

void ProjectManager::renderDirectory(const fs::path& path) {
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
                if (!child.name.contains(m_searchString)) {
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
                    if (!fs::exists(newPath)) {
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

void ProjectManager::renderFile(const fs::path& path) {
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
                fs::rename(path, newPath, ec);
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
            (void)std::snprintf(m_inlineEditBuffer, IM_ARRAYSIZE(m_inlineEditBuffer), "%s", path.filename().string().c_str());
            m_inlineEditFocusRequested = true;
        }

        if (ImGui::MenuItemIcon(ICON_FA_TRASH, "Delete")) {
            spdlog::info("Deleting file: {}", path.string());
            fs::remove(path);
            onFileDeleted(path.parent_path(), path.filename().string());
            fuzzyRemovePath(path);
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

void ProjectManager::buildDirectoryCache(const fs::path& directory) {
    std::vector<CachedEntry> entries;
    if (!fs::exists(directory)) {
        m_directoryCache.erase(directory);
        return;
    }

    std::error_code ec;
    for (const auto& dirEntry : fs::directory_iterator(directory, ec)) {
        if (ec) {
            break;
        }

        CachedEntry e;
        e.path = dirEntry.path();
        e.isDirectory = dirEntry.is_directory();
        e.name = dirEntry.path().filename().string();

        e.nameLower.resize(e.name.size());
        std::ranges::transform(e.name, e.nameLower.begin(), [](unsigned char c) {
            return (char)std::tolower(c);
        });

        entries.emplace_back(std::move(e));
    }

    sortCached(entries);
    m_directoryCache[directory] = std::move(entries);
}

void ProjectManager::sortCached(std::vector<CachedEntry>& entries) {
    std::ranges::sort(entries, [](const CachedEntry& a, const CachedEntry& b) {
        if (a.isDirectory != b.isDirectory) { // dirs first
            return a.isDirectory > b.isDirectory;
        }
        return a.nameLower < b.nameLower;
    });
}

void ProjectManager::onFileAdded(const fs::path& parentDir, const std::string& name) {
    const auto it = m_directoryCache.find(parentDir);
    const auto fullPath = parentDir / name;
    if (fs::is_regular_file(fullPath)) {
        fuzzyAddPath(fullPath);
    }

    if (it == m_directoryCache.end()) {
        return; // parent is not cached yet, ignore event
    }

    if (!fs::exists(fullPath)) {
        return;
    }

    auto& vec = it->second;
    if (std::ranges::any_of(vec, [&](const CachedEntry& e) { return e.path == fullPath; })) {
        return; // already in cache
    }

    const auto nameLower = helpers::toLowerAscii(name);
    vec.emplace_back(fullPath, name, nameLower, fs::is_directory(fullPath));
    sortCached(vec);
}

void ProjectManager::onFileDeleted(const fs::path& parentDir, const std::string& name) {
    const auto fullPath = parentDir / name;
    fuzzyRemovePath(fullPath);

    const auto it = m_directoryCache.find(parentDir);
    if (it == m_directoryCache.end()) {
        return; // parent is not cached yet, ignore event
    }

    auto& vec = it->second;
    std::erase_if(vec, [&](const CachedEntry& e) {
        return e.path == fullPath;
    });

    m_directoryCache.erase(fullPath);
}

void ProjectManager::onFileModified(const fs::path& file) {
    (void)file;
    // TODO: Maybe consider asking editors to reload?
}

void ProjectManager::onFileMoved(const fs::path& parentDir, const std::string& oldName, const std::string& newName) {
    const auto oldPath = parentDir / oldName;
    const auto newPath = parentDir / newName;
    fuzzyMovePath(oldPath, newPath);

    const auto it = m_directoryCache.find(parentDir);
    if (it == m_directoryCache.end()) {
        return; // parent is not cached yet, ignore event
    }

    auto& vec = it->second;
    for (auto& e : vec) {
        if (e.path == oldPath) {
            const bool wasDir = e.isDirectory;
            e.path = newPath;
            e.isDirectory = fs::is_directory(newPath);
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

void ProjectManager::rebuildFuzzyIndex() {
    if (m_projectPath.empty()) {
        return;
    }

    using namespace std::string_view_literals;

    const auto& ignores = g_application->getSettings().indexIgnores;

    // Definitely don't include anything in .git or .cache folders
    // and anything matching the user-provided ignore prefixes
    const auto shouldIgnore = [&ignores](std::string_view path) {
        if (path.starts_with(".git"sv) || path.starts_with(".cache"sv)) {
            return true;
        }

        return std::ranges::any_of(ignores, [&path](const auto& ignore) {
            return path.starts_with(ignore);
        });
    };

    std::vector<FuzzyFileEntry> newFiles;
    std::unordered_map<std::string, size_t> newIndex;
    newFiles.reserve(4096);

#ifdef _WIN32
    const auto mapping = WSLUtil::detectMapping(m_projectPath);
    if (mapping) {
        // Use WSL enumeration using 'find' command because directory iterators
        // are really slow on WSL paths when accessed from Windows.
        std::vector<std::pair<fs::path, std::string>> files;
        if (!WSLUtil::enumerateFiles(*mapping, files)) {
            spdlog::warn("WSL enumeration failed, could not build file index");
        } else {
            for (const auto& [full, rel] : files) {
                if (newIndex.contains(rel)) {
                    continue;
                }

                if (shouldIgnore(rel)) {
                    continue;
                }

                FuzzyFileEntry e;
                e.fullPath = full;
                e.relative = rel;
                e.filename = fs::path(rel).filename().generic_string();
                e.relativeLower = helpers::toLowerAscii(e.relative);
                e.filenameLower = helpers::toLowerAscii(e.filename);
                e.charMask = helpers::buildMask(e.relativeLower);

                newIndex[e.relative] = newFiles.size();
                newFiles.push_back(std::move(e));
            }
        }
    } else {
#endif
        // Normal filesystem traversal
        std::error_code ec;
        for (const auto& it : fs::recursive_directory_iterator(m_projectPath)) {
            if (!it.is_regular_file()) {
                continue;
            }

            const auto& p = it.path();
            const auto rel = fs::relative(p, m_projectPath, ec).generic_string();
            if (ec || newIndex.contains(rel)) {
                continue;
            }

            if (shouldIgnore(rel)) {
                continue;
            }

            FuzzyFileEntry e;
            e.fullPath = p;
            e.relative = rel;
            e.filename = p.filename().generic_string();
            e.relativeLower = helpers::toLowerAscii(e.relative);
            e.filenameLower = helpers::toLowerAscii(e.filename);
            e.charMask = helpers::buildMask(e.relativeLower);

            newIndex[rel] = newFiles.size();
            newFiles.push_back(std::move(e));
        }
#ifdef _WIN32
    }
#endif

    {
        std::scoped_lock lock(m_fuzzyMutex);
        m_fuzzyFiles.swap(newFiles);
        m_fuzzyIndex.swap(newIndex);
        m_prevFuzzyQuery.clear();
        m_prevCandidates.clear();
    }

    m_fuzzyIndexBuilt = true;
    m_fuzzyIndexDirty = true;
    m_fuzzyQueryDirty = true;
    saveFuzzyIndex();
}

void ProjectManager::fuzzyAddPath(const fs::path& p) {
    if (m_projectPath.empty()) {
        return;
    }

    if (!fs::is_regular_file(p)) {
        return;
    }

    std::error_code ec;
    const auto rel = fs::relative(p, m_projectPath, ec).generic_string();
    if (ec) {
        spdlog::trace("Failed to index file: {}, reason: {}", p.string(), ec.message());
        return;
    }

    std::scoped_lock lock(m_fuzzyMutex);
    if (m_fuzzyIndex.contains(rel)) {
        return;
    }

    FuzzyFileEntry e;
    e.fullPath = p;
    e.relative = rel;
    e.filename = p.filename().generic_string();

    e.relativeLower = helpers::toLowerAscii(e.relative);
    e.filenameLower = helpers::toLowerAscii(e.filename);

    // Build compact bitmask over relativeLower
    e.charMask = helpers::buildMask(e.relativeLower);

    m_fuzzyIndex[rel] = m_fuzzyFiles.size();
    m_fuzzyFiles.push_back(std::move(e));
    m_prevFuzzyQuery.clear();
    m_prevCandidates.clear();
    m_fuzzyIndexDirty = true;
    m_fuzzyQueryDirty = true;
}

void ProjectManager::fuzzyRemovePath(const fs::path& p) {
    if (m_projectPath.empty()) {
        return;
    }

    std::error_code ec;
    const auto rel = fs::relative(p, m_projectPath, ec).generic_string();
    if (ec) {
        return;
    }

    std::scoped_lock lock(m_fuzzyMutex);
    const auto it = m_fuzzyIndex.find(rel);
    if (it == m_fuzzyIndex.end()) {
        return;
    }

    const size_t idx = it->second;

    if (idx + 1 != m_fuzzyFiles.size()) {
        std::swap(m_fuzzyFiles[idx], m_fuzzyFiles.back());
        m_fuzzyIndex[m_fuzzyFiles[idx].relative] = idx;
    }

    m_fuzzyFiles.pop_back();
    m_fuzzyIndex.erase(it);
    m_prevFuzzyQuery.clear();
    m_prevCandidates.clear();
    m_fuzzyIndexDirty = true;
    m_fuzzyQueryDirty = true;
}

void ProjectManager::fuzzyMovePath(const fs::path& oldPath, const fs::path& newPath) {
    if (m_projectPath.empty()) {
        return;
    }

    std::error_code ec;
    const auto oldRel = fs::relative(oldPath, m_projectPath, ec).generic_string();
    if (ec) {
        return;
    }

    std::scoped_lock lock(m_fuzzyMutex);
    const auto it = m_fuzzyIndex.find(oldRel);
    if (it == m_fuzzyIndex.end()) { // maybe just an add
        fuzzyAddPath(newPath);
        return;
    }

    const auto newRel = fs::relative(newPath, m_projectPath, ec).generic_string();
    if (ec) {
        return;
    }

    if (m_fuzzyIndex.contains(newRel)) { // conflict; rebuild
        m_fuzzyIndexBuilt = false;
        return;
    }

    auto& entry = m_fuzzyFiles[it->second];
    entry.fullPath = newPath;
    entry.relative = newRel;
    entry.filename = newPath.filename().generic_string();
    entry.relativeLower = helpers::toLowerAscii(entry.relative);
    entry.filenameLower = helpers::toLowerAscii(entry.filename);
    entry.charMask = helpers::buildMask(entry.relativeLower);

    m_fuzzyIndex.erase(it);
    m_fuzzyIndex[newRel] = &entry - m_fuzzyFiles.data();
    m_prevFuzzyQuery.clear();
    m_prevCandidates.clear();
    m_fuzzyIndexDirty = true;
    m_fuzzyQueryDirty = true;
}

void ProjectManager::updateFuzzyResults() {
    m_fuzzyResults.clear();
    std::scoped_lock lock(m_fuzzyMutex);
    const size_t n = m_fuzzyFiles.size();
    if (n == 0) {
        return;
    }

    if (m_fuzzyIndexDirty.exchange(false)) {
        m_prevFuzzyQuery.clear();
        m_prevCandidates.clear();
    }

    std::string queryLower = helpers::toLowerAscii(m_fuzzyQuery);
    if (queryLower.empty()) {
        // Show some top files (alphabetical) when empty
        std::vector<size_t> idx(n);
        std::ranges::iota(idx, 0);

        std::ranges::sort(idx, [&](size_t a, size_t b) {
            return m_fuzzyFiles[a].relative < m_fuzzyFiles[b].relative;
        });

        const size_t limit = std::min<size_t>(idx.size(), 50);
        m_fuzzyResults.reserve(limit);

        for (size_t i = 0; i < limit; ++i) {
            m_fuzzyResults.emplace_back(idx[i], 0.0);
        }

        m_prevFuzzyQuery.clear();
        m_prevCandidates.clear();
        return;
    }

    const auto qmask = helpers::buildMask(queryLower);

    // Candidate pool: if the new query is a strict extension of the previous,
    // reuse the previous candidate set (it can only shrink).
    std::span<const size_t> pool;
    std::vector<size_t> fullPool;
    if (!m_prevFuzzyQuery.empty() && queryLower.starts_with(m_prevFuzzyQuery) && !m_prevCandidates.empty()) {
        pool = m_prevCandidates;
    } else {
        fullPool.resize(n);
        std::ranges::iota(fullPool, 0);
        pool = fullPool;
    }

    typedef FuzzyResult Cand;
    struct MinCmp {
        bool operator()(const Cand& a, const Cand& b) const {
            return a.score > b.score;
        }
    };

    constexpr size_t K = 200;
    std::priority_queue<Cand, std::vector<Cand>, MinCmp> heap;
    std::vector<size_t> nextCandidates;
    nextCandidates.reserve(pool.size());

    // Weigh filename higher than path
    constexpr double FILENAME_BOOST = 1.15;

    using namespace std::string_view_literals;

    for (size_t idx : pool) {
        const auto& f = m_fuzzyFiles[idx];

        // pre-filter: if the file doesn't even contain all query chars, skip
        if ((f.charMask & qmask) != qmask) {
            continue;
        }

        double best = 0.0;

        if (fzy::hasMatch(queryLower, f.filenameLower)) {
            best = std::max(best, fzy::score(queryLower, f.filenameLower) * FILENAME_BOOST);
        }

        // If not great in filename, try relative path
        if (best < 1.0 && fzy::hasMatch(queryLower, f.relativeLower)) {
            best = std::max(best, fzy::score(queryLower, f.relativeLower));
        }

        if (best <= 0.0) {
            continue;
        }

        // Survives => part of the next candidate set (for incremental narrowing)
        nextCandidates.push_back(idx);

        // Maintain top-K heap
        if (heap.size() < K) {
            heap.push({ idx, best });
        } else if (best > heap.top().score) {
            heap.pop();
            heap.push({ idx, best });
        }
    }

    // Unwind heap to sorted results (desc)
    m_fuzzyResults.resize(heap.size());
    for (int i = (int)heap.size() - 1; i >= 0; --i) {
        m_fuzzyResults[i] = { heap.top().index, heap.top().score };
        heap.pop();
    }

    // Stash for next incremental query
    m_prevFuzzyQuery = std::move(queryLower);
    m_prevCandidates.swap(nextCandidates);
}

void ProjectManager::renderFuzzyFinder() {
    if (!m_fuzzyIndexBuilt && !m_fuzzyIndexBuilding) {
        startFuzzyIndexingAsync();
    }

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, {0.5f, 0.3f});
    ImGui::SetNextWindowSize({ std::min(900.0f, vp->Size.x * 0.9f), 500.0f }, ImGuiCond_Appearing);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, ImGui::GetStyleColorVec4(ImGuiCol_TableRowBg));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImGui::GetStyleColorVec4(ImGuiCol_SliderGrab));

    if (ImGui::Begin("Quick Open##FuzzyFinder", &m_fuzzyOpen,
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoNav)) {
        // Close when clicking anywhere outside the window
        if ((ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)) &&
            !ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) {
            m_fuzzyOpen = false;
        }

        // Reset hovered selection when the window appears
        static size_t s_selectedResult = 0;
        if (ImGui::IsWindowAppearing()) {
            s_selectedResult = 0;
            ImGui::SetKeyboardFocusHere();
        }

        if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && !ImGui::IsAnyItemActive()) {
            ImGui::SetKeyboardFocusHere();
        }

        if (ImGui::InputTextWithHint("##fuzzyQuery", "Type to search...", m_fuzzyQuery, IM_ARRAYSIZE(m_fuzzyQuery), ImGuiInputTextFlags_AutoSelectAll)) {
            m_fuzzyQueryDirty = true;
        }

        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            m_fuzzyOpen = false;
        }

        if (m_fuzzyQueryDirty) {
            updateFuzzyResults();
            m_fuzzyQueryDirty = false;
        }

        if (m_fuzzyIndexBuilding) {
            ImGui::Text("Indexing files... This runs in the background.");
        }

        ImGui::Separator();


        ImGui::BeginChild("##fuzzyResults", {-1, -1});

        // Take a snapshot of results to render safely
        struct Row {
            std::string name;
            std::string path;
            fs::path full;
            std::string ext;
        };

        std::vector<Row> rows;
        {
            std::scoped_lock lock(m_fuzzyMutex);
            rows.reserve(m_fuzzyResults.size());
            for (const auto& r : m_fuzzyResults) {
                if (r.index >= m_fuzzyFiles.size()) {
                    continue;
                }

                const auto& f = m_fuzzyFiles[r.index];
                Row row;
                row.name = f.filename;
                // parent path portion of relative
                const std::string& rel = f.relative;
                const size_t pos = rel.rfind('/');
                row.path = (pos == std::string::npos) ? std::string() : rel.substr(0, pos);
                row.full = f.fullPath;
                row.ext = f.fullPath.extension().string();

                rows.emplace_back(std::move(row));
            }
        }

        // Clamp hovered in case result count changed
        if (rows.empty()) {
            s_selectedResult = 0;
        } else if (s_selectedResult >= rows.size()) {
            s_selectedResult = rows.size() - 1;
        }

        constexpr auto tableFlags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY;
        if (ImGui::BeginTable("##FuzzyTable", 2, tableFlags)) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.6f);
            ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthStretch, 0.4f);

            ImGuiListClipper clipper;
            clipper.Begin((int)rows.size());
            while (clipper.Step()) {
                for (size_t i = (size_t)clipper.DisplayStart; std::cmp_less(i, clipper.DisplayEnd); ++i) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();

                    ImGui::PushID((int)i);

                    const bool isSel = i == s_selectedResult;
                    const ImGuiSelectableFlags selFlags = ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick
                        | (isSel ? ImGuiSelectableFlags_Highlight : ImGuiSelectableFlags_None);

                    const bool isNonSpa = !s_spaExtensions.contains(rows[i].ext);
                    if (isNonSpa) {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                    }

                    if (ImGui::Selectable(rows[i].name.c_str(), isSel, selFlags)) {
                        s_selectedResult = i;

                        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                            openEditor(rows[i].full);
                            m_fuzzyOpen = false;
                        }
                    }

                    if (isNonSpa) {
                        ImGui::PopStyleColor();
                    }

                    // Ensure selected stays visible when navigating
                    if (isSel) {
                        ImGui::SetScrollHereY(0.25f);
                    }

                    ImGui::TableNextColumn();
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                    ImGui::TextUnformatted(rows[i].path.c_str());
                    ImGui::PopStyleColor();

                    ImGui::PopID();
                }
            }
            ImGui::EndTable();
        }

        const size_t count = rows.size();
        if (count > 0) {
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
                s_selectedResult = (s_selectedResult + 1) % (int)count;
            } else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
                s_selectedResult = (s_selectedResult - 1 + (int)count) % (int)count;
            }

            if (ImGui::IsKeyPressed(ImGuiKey_Enter) && s_selectedResult < count) {
                openEditor(rows[s_selectedResult].full);
                m_fuzzyOpen = false;
            }
        }

        ImGui::EndChild();
    }
    ImGui::End();

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
}

void ProjectManager::startFuzzyIndexingAsync() {
    if (m_projectPath.empty() || m_fuzzyIndexBuilt || m_fuzzyIndexBuilding) return;
    m_fuzzyIndexBuilding = true;

    // Try to load cache synchronously first (fast path)
    if (loadFuzzyIndex()) {
        m_fuzzyIndexBuilt = true;
        m_fuzzyIndexBuilding = false;
        m_fuzzyIndexDirty = true;
        m_fuzzyQueryDirty = true;
        return;
    }

    if (m_fuzzyIndexThread.joinable()) {
        m_fuzzyIndexThread.request_stop();
        m_fuzzyIndexThread.join();
    }

    m_fuzzyIndexThread = std::jthread([this](std::stop_token st) {
        spdlog::info("Building fuzzy index in background for {}", m_projectPath.string());
        rebuildFuzzyIndex();
        if (st.stop_requested()) {
            return;
        }

        m_fuzzyIndexBuilding = false;
        spdlog::info("Finished building fuzzy index: {} files", m_fuzzyFiles.size());
    });
}

static fs::path fuzzyCachePath(const fs::path& projectRoot) {
    const auto pathHash = std::hash<std::string>{}(canonical(projectRoot).lexically_normal().string());
    const auto cacheDir = Application::getCachePath();
    create_directories(cacheDir);

    return cacheDir / fmt::format("{:016x}.idx", pathHash);
}

bool ProjectManager::loadFuzzyIndex() {
    if (m_projectPath.empty()) {
        return false;
    }

    const auto cacheFile = fuzzyCachePath(m_projectPath);
    std::error_code ec;
    if (!fs::exists(cacheFile, ec)) {
        return false;
    }

    std::ifstream in(cacheFile, std::ios::binary | std::ios::in);
    if (!in) {
        return false;
    }

    IndexHeader header;
    in >> header;

    if (!in) {
        return false;
    }

    uint64_t currentStamp = 0;
    const auto ftime = fs::last_write_time(m_projectPath, ec);
    if (!ec) {
        currentStamp = (uint64_t)ftime.time_since_epoch().count();
    }

    if (header.timestamp != currentStamp) {
        return false;
    }

    if (header.fileCount > std::numeric_limits<uint32_t>::max()) {
        return false;
    }

    std::vector<FuzzyFileEntry> files;
    std::unordered_map<std::string, size_t> index;
    files.reserve((size_t)header.fileCount);

    for (size_t i = 0; i < header.fileCount; ++i) {
        IndexEntry entry;
        in >> entry;

        if (!in) {
            return false;
        }

        std::string rel(entry.relLen, '\0');
        std::string fname(entry.filenameLen, '\0');
        in.read(rel.data(), entry.relLen);
        in.read(fname.data(), entry.filenameLen);

        if (!in) {
            return false;
        }

        FuzzyFileEntry e;
        e.relative = std::move(rel);
        e.filename = std::move(fname);
        e.fullPath = m_projectPath / e.relative;
        e.relativeLower = helpers::toLowerAscii(e.relative);
        e.filenameLower = helpers::toLowerAscii(e.filename);
        e.charMask = helpers::buildMask(e.relativeLower);

        index[e.relative] = files.size();
        files.push_back(std::move(e));
    }

    {
        std::scoped_lock lk(m_fuzzyMutex);
        m_fuzzyFiles.swap(files);
        m_fuzzyIndex.swap(index);
        m_prevFuzzyQuery.clear();
        m_prevCandidates.clear();
    }

    spdlog::info("Loaded fuzzy index cache: {} entries", m_fuzzyFiles.size());
    return true;
}

void ProjectManager::saveFuzzyIndex() const {
    if (m_projectPath.empty()) {
        return;
    }

    const auto cacheFile = fuzzyCachePath(m_projectPath);
    std::ofstream out(cacheFile, std::ios::binary | std::ios::out);
    if (!out) {
        return;
    }

    uint64_t currentStamp = 0;
    std::error_code ec;
    const auto ftime = fs::last_write_time(m_projectPath, ec);
    if (!ec) {
        currentStamp = (uint64_t)ftime.time_since_epoch().count();
    }

    spdlog::info("Writing timestamp: {} ({:X})", currentStamp, currentStamp);

    std::scoped_lock lk(m_fuzzyMutex);
    out << IndexHeader{
        .magic = INDEX_MAGIC,
        .version = INDEX_VERSION,
        .timestamp = currentStamp,
        .fileCount = m_fuzzyFiles.size()
    };

    for (const auto& e : m_fuzzyFiles) {
        const std::string& rel = e.relative;
        const std::string& fname = e.filename;
        const auto relLen = static_cast<uint16_t>(rel.size());
        const auto fileLen = static_cast<uint16_t>(fname.size());

        out << IndexEntry{
            .relLen = relLen,
            .filenameLen = fileLen
        };

        out.write(rel.data(), relLen);
        out.write(fname.data(), fileLen);
    }
}
