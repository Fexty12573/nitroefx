#pragma once
#include "spl/spl_resource.h"
#include "editor_instance.h"
#include "editor_settings.h"
#include "grid_renderer.h"
#include <types.h>

#include <imgui.h>
#include <nlohmann/json.hpp>

#include <array>
#include <chrono>
#include <unordered_map>
#include <vector>

class Editor {
public:
    Editor();

    void update(float deltaTime);
    void render();
    void renderParticles();
    void renderMenu(std::string_view name);
    void renderToolbar(float itemHeight);
    void openPicker();
    void openEditor();
    void openTextureManager();
    void openSettings();
    void openTutorial();

    void onEditorOpened(const std::shared_ptr<EditorInstance>& editor);
    void onEditorRenamed(const std::filesystem::path& oldPath, const std::filesystem::path& newPath);

    void playEmitter(EmitterSpawnType spawnType);
    void playAllEmitters(EmitterSpawnType spawnType);
    void killEmitters();
    void resetCamera();

    void handleEvent(const SDL_Event& event);

    void save();
    void saveAs(const std::filesystem::path& path);

    void loadConfig(const nlohmann::json& config);
    void saveConfig(nlohmann::json& config) const;

    bool canUndo() const;
    bool canRedo() const;
    void undo();
    void redo();

    void pushClipboard(const std::string& source, const SPLResource& res);
    void pushClipboard(const std::string& source, const SPLTexture& tex);
    void pushClipboard(const std::string& source, const SPLResource& res, const SPLTexture& tex);

    std::queue<SPLResourceCopy>& clipboardHistory() {
        return m_clipboardHistory;
    }

    const EditorSettings& getSettings() const {
        return m_settings;
    }

    float getTimeScale() const {
        return m_timeScale;
    }

    float& timeScale() {
        return m_timeScale;
    }

    bool& pickerOpen() { return m_pickerOpen; }
    bool& textureManagerOpen() { return m_textureManagerOpen; }
    bool& resourceEditorOpen() { return m_editorOpen; }

private:
    void renderSettings();
    void renderTutorial();

    void updateRenderSettings(bool swapRenderer = false);

    void updateMaxParticles();

private:
    bool m_pickerOpen = true;
    bool m_textureManagerOpen = true;
    bool m_editorOpen = true;
    bool m_settingsOpen = false;
    bool m_showTutorial = false;
    int m_tutorialWindowId = 0;
    float m_timeScale = 1.0f;

    u32 m_settingsWindowId = 0;

    EditorSettings m_settings;
    EditorSettings m_settingsBackup;
    EditorSettings m_settingsDefault;

    static constexpr glm::ivec2 s_gridDimensions = { 20, 20 };
    static constexpr glm::vec2 s_gridSpacing = { 1.0f, 1.0f };

    std::shared_ptr<GridRenderer> m_gridRenderer;

    std::queue<SPLResourceCopy> m_clipboardHistory;
};
