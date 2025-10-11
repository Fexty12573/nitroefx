#pragma once

#include "editor_instance.h"

#include <SDL3/SDL_events.h>
#include <narc/narc.h>
#include <narc/defs/fatb.h>
#include <efsw/efsw.hpp>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <span>
#include <ranges>
#include <vector>
#include <unordered_map>

class Editor;

// Project is a bit of an overstatement here, because it's really just a directory
class ProjectManager {
public:
    void init(Editor* editor);
    void openProject(const std::filesystem::path& path);
    void closeProject(bool force = false);
    void openEditor(const std::filesystem::path& path);
    void openEditor(); // Create an editor without an associated file
    void openTempEditor(const std::filesystem::path& path);
    void openNarcProject(const std::filesystem::path& path);
    void closeEditor(const std::shared_ptr<EditorInstance>& editor, bool force = false);
    void closeTempEditor();
    void closeAllEditors();
    void saveAllEditors() const;

    bool hasEditor(const std::filesystem::path& path) const {
        return std::ranges::any_of(m_openEditors, [&path](const auto& editor) { return editor->getPath() == path; });
    }

    std::shared_ptr<EditorInstance> getEditor(const std::filesystem::path& path) const {
        const auto it = std::ranges::find_if(m_openEditors, [&path](const auto& editor) { return editor->getPath() == path; });
        return it != m_openEditors.end() ? *it : nullptr;
    }

    std::shared_ptr<EditorInstance> getEditor(u64 uniqueID) const {
        const auto it = std::ranges::find_if(m_openEditors, [uniqueID](const auto& editor) { return editor->getUniqueID() == uniqueID; });
        return it != m_openEditors.end() ? *it : nullptr;
    }

    std::shared_ptr<EditorInstance> getNarcEditor(size_t index) {
        const auto it = std::ranges::find_if(m_openEditors, [index](const auto& editor) {
            return editor->getNarcIndex() == index;
        });

        return it != m_openEditors.end() ? *it : nullptr;
    }

    void open();
    void render();

    std::span<const std::shared_ptr<EditorInstance>> getOpenEditors() const {
        return m_openEditors;
    }

    const std::shared_ptr<EditorInstance>& getActiveEditor() const {
        return m_activeEditor;
    }

    void setActiveEditor(const std::shared_ptr<EditorInstance>& editor) {
        m_activeEditor = editor;
    }

    const std::filesystem::path& getProjectPath() const {
        return m_projectPath;
    }

    bool hasProject() const {
        return !m_projectPath.empty();
    }

    bool hasOpenEditors() const {
        return !m_openEditors.empty();
    }

    bool hasActiveEditor() const {
        return m_activeEditor != nullptr;
    }

    bool shouldForceActivate() const {
        return m_forceActivate;
    }

    void clearForceActivate() {
        m_forceActivate = false;
    }

    void handleEvent(const SDL_Event& event);

    bool hasUnsavedEditors() const {
        return std::ranges::any_of(m_openEditors, [](const auto& editor) {
            return editor->isModified();
        });
    }

    std::span<const std::shared_ptr<EditorInstance>> getUnsavedEditors() {
        return m_unsavedEditors;
    }

    void clearUnsavedEditors() {
        m_unsavedEditors.clear();
    }

private:
    void openEditor(size_t narcIndex);
    void openTempEditor(size_t narcIndex);

    void renderDirectory(const std::filesystem::path& path);
    void renderFile(const std::filesystem::path& path);
    void renderNarcFile(const std::string& name, size_t index);

    void cancelInlineEdit();

    enum class InlineEditMode {
        None,
        RenameFile,
        CreateFile
    };

    // ---------------- Directory Cache -----------------
    struct CachedEntry {
        std::filesystem::path path;
        bool isDirectory;
    };

    struct PathHash {
        size_t operator()(const std::filesystem::path& p) const noexcept {
            return std::hash<std::string>()(p.generic_string());
        }
    };

    void buildDirectoryCache(const std::filesystem::path& directory);
    void ensureDirectoryCached(const std::filesystem::path& directory) {
        if (!m_directoryCache.contains(directory)) {
            buildDirectoryCache(directory);
        }
    }

    static void sortCached(std::vector<CachedEntry>& entries);

    void onFileAdded(const std::filesystem::path& parentDir, const std::string& name);
    void onFileDeleted(const std::filesystem::path& parentDir, const std::string& name);
    void onFileModified(const std::filesystem::path& file);
    void onFileMoved(const std::filesystem::path& parentDir, const std::string& oldName, const std::string& newName);

    class FileWatchListener : public efsw::FileWatchListener {
    public:
        FileWatchListener(ProjectManager* projManager) : m_projManager(projManager) {}

        void handleFileAction(efsw::WatchID watchId, const std::string& dir,
            const std::string& filename, efsw::Action action, std::string oldFilename) override {
            if (!m_projManager) return;
            const std::filesystem::path parentDir(dir);
            switch (action) {
            case efsw::Action::Add:
                m_projManager->onFileAdded(parentDir, filename);
                break;
            case efsw::Action::Delete:
                m_projManager->onFileDeleted(parentDir, filename);
                break;
            case efsw::Action::Modified:
                m_projManager->onFileModified(parentDir / filename);
                break;
            case efsw::Action::Moved:
                m_projManager->onFileMoved(parentDir, oldFilename, filename);
                break;
            }
        }

    private:
        ProjectManager* m_projManager;
    };

    friend class FileWatchListener;

private:
    Editor* m_mainEditor;
    std::filesystem::path m_projectPath;
    bool m_isNarc;
    narc* m_narc;
    vfs_ctx m_vfsCtx;
    fatb_meta* m_fatbMeta;
    fatb_entry* m_fatb;
    char* m_fimg;

    // Directory cache
    std::unique_ptr<efsw::FileWatcher> m_watcher;
    std::unique_ptr<FileWatchListener> m_listener;
    std::unordered_map<std::filesystem::path, std::vector<CachedEntry>, PathHash> m_directoryCache;

    std::vector<std::shared_ptr<EditorInstance>> m_openEditors;
    std::shared_ptr<EditorInstance> m_activeEditor;
    bool m_forceActivate = false;

    // Unsaved changes data
    std::vector<std::shared_ptr<EditorInstance>> m_unsavedEditors;

    bool m_open = true;
    bool m_hideOtherFiles = false;
    std::filesystem::path m_contextMenuPath;
    std::filesystem::path m_selectedFile;
    std::string m_searchString;

    // Inline editing
    InlineEditMode m_inlineMode = InlineEditMode::None;
    std::filesystem::path m_inlineEditPathOld;
    std::filesystem::path m_inlineEditTargetDir;
    char m_inlineEditBuffer[256] = {};
    bool m_inlineEditFocusRequested = false;

    static inline const std::unordered_set<std::string> s_spaExtensions = {
        ".spa",
        ".bin",
        "._APS",
        ". APS",
        ".APS",
        ""
    };
};


inline const auto g_projectManager = new ProjectManager();
