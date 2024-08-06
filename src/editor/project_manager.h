#pragma once

#include "editor_instance.h"

#include <filesystem>
#include <memory>
#include <span>
#include <vector>


// Project is a bit of an overstatement here, because it's really just a directory
class ProjectManager {
public:
    void openProject(const std::filesystem::path& path);
    void closeProject(bool force = false);
    void openEditor(const std::filesystem::path& path);
    void closeEditor(const std::shared_ptr<EditorInstance>& editor);

    void open();
    void render();

    std::span<const std::shared_ptr<EditorInstance>> getOpenEditors() const {
        return m_openEditors;
    }

    const std::shared_ptr<EditorInstance>& getActiveEditor() const {
        return m_activeEditor;
    }

    const std::filesystem::path& getProjectPath() const {
        return m_projectPath;
    }

    bool hasProject() const {
        return !m_projectPath.empty();
    }

private:
    void renderDirectory(const std::filesystem::path& path);
    void renderFile(const std::filesystem::path& path);

private:
    std::filesystem::path m_projectPath;

    std::vector<std::shared_ptr<EditorInstance>> m_openEditors;
    std::shared_ptr<EditorInstance> m_activeEditor;

    bool m_open = true;
    std::filesystem::path m_contextMenuPath;
};


inline const auto g_projectManager = new ProjectManager();
