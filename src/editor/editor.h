#pragma once
#include "spl/spl_resource.h"
#include "editor_instance.h"
#include "editor_settings.h"
#include "debug_renderer.h"
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

    void selectResource(u64 editorID, size_t resourceIndex);

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

private:
    void renderResourcePicker();
    void renderResourceEditor();
    void renderSettings();
    void renderTutorial();

    void updateRenderSettings(bool swapRenderer = false);

    void renderHeaderEditor(SPLResourceHeader& header) const;
    void renderBehaviorEditor(SPLResource& res);

    bool renderGravityBehaviorEditor(const std::shared_ptr<SPLGravityBehavior>& gravity);
    bool renderRandomBehaviorEditor(const std::shared_ptr<SPLRandomBehavior>& random);
    bool renderMagnetBehaviorEditor(const std::shared_ptr<SPLMagnetBehavior>& magnet);
    bool renderSpinBehaviorEditor(const std::shared_ptr<SPLSpinBehavior>& spin);
    bool renderCollisionPlaneBehaviorEditor(const std::shared_ptr<SPLCollisionPlaneBehavior>& collisionPlane);
    bool renderConvergenceBehaviorEditor(const std::shared_ptr<SPLConvergenceBehavior>& convergence);

    void renderAnimationEditor(SPLResource& res);
    bool renderScaleAnimEditor(SPLScaleAnim& res);
    bool renderColorAnimEditor(const SPLResource& mainRes, SPLColorAnim& res);
    bool renderAlphaAnimEditor(SPLAlphaAnim& res);
    bool renderTexAnimEditor(SPLTexAnim& res);
    void renderChildrenEditor(SPLResource& res);

    void helpPopup(std::string_view text) const;

    void renderDebugShapes(const std::shared_ptr<EditorInstance>& editor, std::vector<Renderer*>& renderers);

    void updateMaxParticles();

    void ensureValidSelection(const std::shared_ptr<EditorInstance>& editor);

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

    EmitterSpawnType m_emitterSpawnType = EmitterSpawnType::SingleShot;
    float m_emitterInterval = 1.0f; // seconds

    // Tracks last selection per editor to animate selection changes
    std::unordered_map<u64, size_t> m_lastPickerSelection;

    static inline const u32 s_hoverAccentColor = ImGui::ColorConvertFloat4ToU32({ 0.7f, 0.3f, 0.7f, 1.0f });
    static constexpr glm::ivec2 s_gridDimensions = { 20, 20 };
    static constexpr glm::vec2 s_gridSpacing = { 1.0f, 1.0f };

    std::array<f32, 64> m_xAnimBuffer;
    std::array<f32, 64> m_yAnimBuffer1;
    std::array<f32, 64> m_yAnimBuffer2;

    std::unordered_map<u64, size_t> m_selectedResources;
    std::weak_ptr<EditorInstance> m_activeEditor;
    std::shared_ptr<GridRenderer> m_gridRenderer;
    std::unique_ptr<DebugRenderer> m_debugRenderer;
    std::shared_ptr<GridRenderer> m_collisionGridRenderer;

    std::queue<SPLResourceCopy> m_clipboardHistory;
};
