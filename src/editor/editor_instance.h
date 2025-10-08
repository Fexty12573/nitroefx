#pragma once

#include <filesystem>
#include <utility> // std::pair
#include <SDL3/SDL_events.h>

#include "camera.h"
#include "gfx/gl_viewport.h"
#include "particle_system.h"
#include "renderer.h"
#include "editor_history.h"
#include "spl/spl_archive.h"


class EditorInstance {
public:
    explicit EditorInstance(const std::filesystem::path& path, bool isTemp = false);
    explicit EditorInstance(size_t narcIndex, std::span<const char> data, bool isTemp = false);
    EditorInstance(bool isTemp = false);

    std::pair<bool, bool> render();
    void renderParticles(const std::vector<Renderer*>& renderers);
    void updateParticles(float deltaTime);
    void handleEvent(const SDL_Event& event);

    void useModernRenderer();
    void useLegacyRenderer();

    bool notifyClosing();
    void notifyResourceChanged(size_t index);
    bool valueChanged(bool changed);
    bool isModified() const {
        return m_modified;
    }

    void setMaxParticles(u32 maxParticles) {
        m_particleSystem.setMaxParticles(maxParticles);
    }

    void makePermanent() {
        m_isTemp = false;
    }

    bool isTemp() const {
        return m_isTemp;
    }

    void duplicateResource(size_t index);
    void deleteResource(size_t index);
    void addResource();

    void save();
    void saveAs(const std::filesystem::path& path);

    void pushHistory();

    bool canUndo() const {
        return m_history.canUndo();
    }

    bool canRedo() const {
        return m_history.canRedo();
    }

    EditorActionType undo();
    EditorActionType redo();


    std::string getName() const;

    void rename(const std::filesystem::path& to) {
        m_path = to;
    }

    const std::filesystem::path& getPath() const {
        return m_path;
    }

    size_t getNarcIndex() const {
        return m_narcIndex;
    }

    SPLArchive& getArchive() {
        return m_archive;
    }

    u64 getUniqueID() const {
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

private:
    std::filesystem::path m_path;
    size_t m_narcIndex = -1;
    SPLArchive m_archive;
    GLViewport m_viewport = GLViewport({ 800, 600 });
    ParticleSystem m_particleSystem;
    Camera m_camera;
    EditorHistory m_history;

    size_t m_selectedResource = -1;
    SPLResource m_resourceBefore;

    glm::vec2 m_size = { 800, 600 };
    bool m_updateProj;
    bool m_isTemp = false;
    bool m_modified = false; // Has the file been modified?
    u64 m_uniqueID;
};
