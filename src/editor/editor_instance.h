#pragma once

#include <chrono>
#include <filesystem>
#include <utility> // std::pair
#include <SDL3/SDL_events.h>

#include "camera.h"
#include "gfx/gl_viewport.h"
#include "gfx/gl_texture.h"
#include "particle_system.h"
#include "renderer.h"
#include "editor_history.h"
#include "spl/spl_archive.h"
#include "spl/spl_resource.h"
#include "base_editor.h"


enum class EmitterSpawnType {
    SingleShot,
    Looped,
    Interval
};


class EditorInstance : public BaseEditor {
public:
    explicit EditorInstance(const std::filesystem::path& path, bool isTemp = false, bool isRecovered = false);
    explicit EditorInstance(std::string name, size_t narcIndex, std::span<const u8> data, bool isTemp = false);
    explicit EditorInstance(bool isTemp = false);

    std::pair<bool, bool> render() override;
    void renderParticles(const std::vector<Renderer*>& renderers);
    void update(float deltaTime) override;
    [[nodiscard]] bool isAnimating() const override {
        return !m_particleSystem.getEmitters().empty();
    }
    void handleEvent(const SDL_Event& event) override;

    void renderStats() override;
    void renderTextureManager(bool* open);

    void playEmitter(EmitterSpawnType spawnType, float interval);
    void playAllEmitters(EmitterSpawnType spawnType, float interval);
    void killEmitters();
    void resetCamera();

    void useModernRenderer();
    void useLegacyRenderer();

    [[nodiscard]] bool notifyClosing() override;
    void notifyResourceChanged(size_t index);
    bool valueChanged(bool changed);

    [[nodiscard]] bool isModified() const override {
        return m_modified;
    }

    void setMaxParticles(u32 maxParticles) {
        m_particleSystem.setMaxParticles(maxParticles);
    }

    void makePermanent() override {
        m_isTemp = false;
    }

    [[nodiscard]] bool isTemp() const override {
        return m_isTemp;
    }

    [[nodiscard]] bool isRecovered() const override {
        return m_isRecovered;
    }

    [[nodiscard]] EditorType getType() const override {
        return EditorType::Particle;
    }

    [[nodiscard]] bool isNarc() const {
        return m_narcIndex != std::numeric_limits<size_t>::max();
    }

    void duplicateResource(size_t index);
    void deleteResource(size_t index);
    void addResource();

    void save() override;
    void saveAs(const std::filesystem::path& path) override;
    void saveTo(const std::filesystem::path& path);
    void saveBackup() override;

    void pushHistory();

    bool canUndo() const {
        return m_history.canUndo();
    }

    bool canRedo() const {
        return m_history.canRedo();
    }

    EditorActionType undo();
    EditorActionType redo();

    std::filesystem::path getRelativePath() const;
    std::filesystem::path getBackupPath() const;

    [[nodiscard]] std::string getName() const override;

    void rename(const std::filesystem::path& to) {
        m_path = to;
    }

    void rename(std::string_view to) override {
        if (isNarc()) {
            m_narcMemberName = to;
        }
    }

    [[nodiscard]] const std::filesystem::path& getPath() const override {
        return m_path;
    }

    [[nodiscard]] std::optional<size_t> getNarcIndex() const override {
        return isNarc() ? std::optional<size_t>{ m_narcIndex } : std::nullopt;
    }

    SPLArchive& getArchive() {
        return m_archive;
    }

    [[nodiscard]] u64 getUniqueID() const override {
        return m_uniqueID;
    }

    ParticleSystem& getParticleSystem() {
        return m_particleSystem;
    }

    Camera& getCamera() {
        return m_camera;
    }

    void updateViewportSize() {
        m_updateProj = true;
    }

    [[nodiscard]] Clock::time_point getLastBackupTime() const override {
        return m_lastBackupTime;
    }

private:
    void openTempTexture(const std::filesystem::path& path, size_t destIndex = -1);
    void discardTempTexture();
    void destroyTempTexture();
    void importTempTexture();

    static bool palettizeTexture(
        const u8* data,
        s32 width,
        s32 height,
        const TextureImportSpecification& spec,
        std::vector<u8>& outData,
        std::vector<u8>& outPalette
    );
    static void quantizeTexture(const u8* data, s32 width, s32 height, const TextureImportSpecification& spec, u8* out);

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

    static constexpr auto INVALID_RESOURCE = std::numeric_limits<size_t>::max();

    std::filesystem::path m_path;
    size_t m_narcIndex = -1;
    std::string m_narcMemberName;
    SPLArchive m_archive;
    GLViewport m_viewport = GLViewport({800, 600});
    ParticleSystem m_particleSystem;
    Camera m_camera;
    EditorHistory m_history;

    Clock::time_point m_lastBackupTime;

    size_t m_selectedResource = INVALID_RESOURCE;
    SPLResource m_resourceBefore;

    // Texture manager / import state
    TempTexture* m_tempTexture = nullptr;
    float m_tempTextureScale = 1.0f;
    bool m_discardTempTexture = false;
    size_t m_selectedTexture = -1;
    bool m_deleteSelectedTexture = false;

    // Stats panel feedback animation
    u32 m_emitterFlashColor = 0;
    size_t m_lastEmitterCount = 0;

    // Interval-spawn tasks owned by this editor
    struct EmitterSpawnTask {
        u64 resourceIndex;
        Clock::time_point time;
        std::chrono::duration<float> interval;
    };
    std::vector<EmitterSpawnTask> m_emitterTasks;

    glm::vec2 m_size = {800, 600};
    u64 m_uniqueID;
    bool m_updateProj;
    bool m_isTemp = false;
    bool m_modified = false; // Has the file been modified?
    bool m_isRecovered = false; // Was this instance recovered from a backup?
};
