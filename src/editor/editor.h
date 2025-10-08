#pragma once
#include "spl/spl_resource.h"
#include "editor_instance.h"
#include "editor_settings.h"
#include "debug_renderer.h"
#include "grid_renderer.h"
#include "types.h"

#include <imgui.h>
#include <nlohmann/json.hpp>

#include <array>
#include <chrono>
#include <unordered_map>
#include <vector>

enum class EmitterSpawnType {
    SingleShot,
    Looped,
    Interval
};

class Editor {
public:
    Editor();

    void render();
    void renderParticles();
    void renderMenu(std::string_view name);
    void renderToolbar(float itemHeight);
    void renderStats();
    void openPicker();
    void openEditor();
    void openTextureManager();
    void updateParticles(float deltaTime);
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

    const EditorSettings& getSettings() const {
        return m_settings;
    }

private:
    void renderResourcePicker();
    void renderTextureManager();
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

    void openTempTexture(const std::filesystem::path& path, size_t destIndex = -1);
    void discardTempTexture();
    void destroyTempTexture();
    void importTempTexture();

    void ensureValidSelection(const std::shared_ptr<EditorInstance>& editor);

    static bool palettizeTexture(
        const u8* data, 
        s32 width, 
        s32 height, 
        const TextureImportSpecification& spec, 
        std::vector<u8>& outData,
        std::vector<u8>& outPalette
    );
    static void quantizeTexture(const u8* data, s32 width, s32 height, const TextureImportSpecification& spec, u8* out);

private:
    struct TempTexture {
        std::string path;
        u8* data;
        u8* quantized;
        s32 width;
        s32 height;
        s32 channels;
        TextureFormat suggestedFormat;
        bool suggestedFormatUncompressed;
        TextureImportSpecification spec;
        TextureConversionPreference preference;
        std::unique_ptr<GLTexture> texture;
        bool isValidSize;
        size_t destIndex = -1;
    };

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

    static inline const u32 s_hoverAccentColor = ImGui::ColorConvertFloat4ToU32({ 0.7f, 0.3f, 0.7f, 1.0f });
    static constexpr glm::ivec2 s_gridDimensions = { 20, 20 };
    static constexpr glm::vec2 s_gridSpacing = { 1.0f, 1.0f };

    std::array<f32, 64> m_xAnimBuffer;
    std::array<f32, 64> m_yAnimBuffer;

    TempTexture* m_tempTexture = nullptr;
    float m_tempTextureScale = 1.0f;
    bool m_discardTempTexture = false; // Whether the temp texture should be discarded in the next frame

    size_t m_selectedTexture = -1;
    bool m_deleteSelectedTexture = false;

    std::unordered_map<u64, size_t> m_selectedResources;
    std::weak_ptr<EditorInstance> m_activeEditor;
    std::shared_ptr<GridRenderer> m_gridRenderer;
    std::unique_ptr<DebugRenderer> m_debugRenderer;
    std::shared_ptr<GridRenderer> m_collisionGridRenderer;

    std::queue<SPLResourceCopy> m_clipboardHistory;

    struct EmitterSpawnTask {
        u64 resourceIndex;
        std::chrono::time_point<std::chrono::steady_clock> time;
        std::chrono::duration<float> interval;
        u64 editorID;
    };

    std::vector<EmitterSpawnTask> m_emitterTasks;
};
