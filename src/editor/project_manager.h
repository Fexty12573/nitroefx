#pragma once

#include "editor_instance.h"

#include <SDL3/SDL_events.h>
#include <nitroarc.h>
#include <efsw/efsw.hpp>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <span>
#include <ranges>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <atomic>
#include <mutex>
#include <thread>
#include <fstream>
#include <variant>

class Editor;

// Project is a bit of an overstatement here, because it's really just a directory
class ProjectManager {
public:
    void init(Editor* editor);
    void openProject(const std::filesystem::path& path);
    void closeProject(bool force = false);
    void openEditor(const std::filesystem::path& path, bool isRecovered = false);
    void openEditor(); // Create an editor without an associated file
    void openTempEditor(const std::filesystem::path& path);
    void openNarcProject(const std::filesystem::path& path);
    void closeEditor(const std::shared_ptr<EditorInstance>& editor, bool force = false);
    void closeTempEditor();
    void closeAllEditors();
    void saveAllEditors() const;
    void saveAllNarcEditors() const;

    bool hasEditor(const std::filesystem::path& path) const {
        return std::ranges::any_of(m_openEditors, [&path](const auto& editor) {
            return editor->getPath() == path;
        });
    }

    std::shared_ptr<EditorInstance> getEditor(const std::filesystem::path& path) const {
        const auto it = std::ranges::find_if(m_openEditors, [&path](const auto& editor) {
            return editor->getPath() == path;
        });

        return it != m_openEditors.end() ? *it : nullptr;
    }

    std::shared_ptr<EditorInstance> getEditor(u64 uniqueID) const {
        const auto it = std::ranges::find_if(m_openEditors, [uniqueID](const auto& editor) {
            return editor->getUniqueID() == uniqueID;
        });

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

    bool hasNarcProject() const {
        return m_isNarc;
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

    void openFileSearch() {
        if (m_projectPath.empty()) {
            return;
        }

        m_fuzzyOpen = true;
        m_fuzzyQueryDirty = true;
    }

    void saveNarcProject();

    void updateNarcMember(size_t index, const std::vector<u8>& data);

private:
    struct NarcEntry;

    void openEditor(size_t narcIndex);
    void openTempEditor(size_t narcIndex);

    void renderDirectory(const std::filesystem::path& path);
    void renderFile(const std::filesystem::path& path);
    void renderNarcFile(NarcEntry& entry, size_t index);

    void cancelInlineEdit();

    enum class InlineEditMode {
        None,
        RenameFile,
        CreateFile
    };

    struct CachedEntry {
        std::filesystem::path path;
        std::string name;
        std::string nameLower;
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

    // Fuzzy finding
    void rebuildFuzzyIndex();
    void updateFuzzyResults();
    void renderFuzzyFinder();
    void fuzzyAddPath(const std::filesystem::path& p);
    void fuzzyRemovePath(const std::filesystem::path& p);
    void fuzzyMovePath(const std::filesystem::path& oldPath, const std::filesystem::path& newPath);
    void startFuzzyIndexingAsync();
    bool loadFuzzyIndex();
    void saveFuzzyIndex() const;

    class FileWatchListener : public efsw::FileWatchListener {
    public:
        explicit FileWatchListener(ProjectManager* projManager) : m_projManager(projManager) {}

        void handleFileAction(efsw::WatchID watchId, const std::string& dir,
            const std::string& filename, efsw::Action action, std::string oldFilename) override {
            if (!m_projManager) {
                return;
            }
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

    struct FuzzyFileEntry {
        std::filesystem::path fullPath;
        std::string relative;
        std::string filename;
        std::string relativeLower;
        std::string filenameLower;
        uint64_t charMask = 0;
    };

    struct FuzzyResult {
        size_t index;
        double score;
    };

    struct NarcEntry {
        std::string name;
        std::variant<std::span<u8>, std::vector<u8>> data;

        std::span<const u8> getData() const {
            if (std::holds_alternative<std::span<u8>>(data)) {
                return std::get<std::span<u8>>(data);
            }

            return std::get<std::vector<u8>>(data);
        }

        std::span<u8> getData() {
            if (std::holds_alternative<std::span<u8>>(data)) {
                return std::get<std::span<u8>>(data);
            }

            return std::get<std::vector<u8>>(data);
        }

        bool isModified() const {
            return std::holds_alternative<std::vector<u8>>(data);
        }
    };

    friend class FileWatchListener;

private:
    Editor* m_mainEditor;
    std::filesystem::path m_projectPath;
    bool m_isNarc = false;
    bool m_narcModified = false;

    // NARC data
    nitroarc_t m_narc;
    std::vector<u8> m_narcData;
    std::vector<NarcEntry> m_narcEntries;

    // Recursive directory cache
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
    size_t m_inlineEditIndex = -1;
    char m_inlineEditBuffer[256] = {};
    bool m_inlineEditFocusRequested = false;

    // Fuzzy finding
    std::vector<FuzzyFileEntry> m_fuzzyFiles;
    std::unordered_map<std::string, size_t> m_fuzzyIndex;
    std::atomic<bool> m_fuzzyIndexBuilt = false;
    std::atomic<bool> m_fuzzyIndexBuilding = false;
    bool m_fuzzyOpen = false;
    bool m_fuzzyQueryDirty = false;
    char m_fuzzyQuery[256] = { 0 };
    std::vector<FuzzyResult> m_fuzzyResults;
    std::string m_prevFuzzyQuery;
    std::vector<size_t> m_prevCandidates;
    std::vector<size_t> m_fuzzyAlpha;
    std::jthread m_fuzzyIndexThread;
    mutable std::mutex m_fuzzyMutex;
    mutable std::atomic<bool> m_fuzzyIndexDirty = false;

    static constexpr uint32_t INDEX_MAGIC = 0x20584449; // "IDX "
    static constexpr uint32_t INDEX_VERSION = 1;

    struct IndexHeader {
        uint32_t magic;
        uint32_t version;
        uint64_t timestamp;
        uint64_t fileCount;
    };
    struct IndexEntry {
        uint16_t relLen;
        uint16_t filenameLen;
        // Followed by relative path and filename
    };

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
