#pragma once

#include "editor_instance.h"

#include <SDL3/SDL_events.h>
#include <narc/narc.h>
#include <narc/defs/fatb.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <span>
#include <ranges>
#include <vector>

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
    void saveAllEditors();

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

private:
    Editor* m_mainEditor;
    std::filesystem::path m_projectPath;
    bool m_isNarc;
    narc* m_narc;
    vfs_ctx m_vfsCtx;
    fatb_meta* m_fatbMeta;
    fatb_entry* m_fatb;
    char* m_fimg;

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
    char m_inlineEditBuffer[256] = { 0 };
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
