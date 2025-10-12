#include "editor_instance.h"
#include "application.h"
#include "project_manager.h"
#include "spl/spl_random.h"
#include "gfx/gl_util.h"

#include <GL/glew.h>
#include <imgui.h>
#include <random>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>


EditorInstance::EditorInstance(const std::filesystem::path& path, bool isTemp)
    : m_path(path), m_archive(path)
    , m_particleSystem(g_application->getEditor()->getSettings().maxParticles, m_archive.getTextures())
    , m_camera(glm::radians(45.0f), { 800, 800 }, 1.0f, 500.0f), m_isTemp(isTemp) {

    m_uniqueID = SPLRandom::nextU64();
    m_updateProj = true;
    notifyResourceChanged(0);

    // Choose particle renderer backend
    if (g_application->getEditor()->getSettings().useLegacyParticleRenderer) {
        m_particleSystem.useLegacyRenderer();
        m_particleSystem.getRenderer().setTextures(m_archive.getTextures());
    }

    m_camera.setProjection(
        g_application->getEditor()->getSettings().useOrthographicCamera
            ? CameraProjection::Orthographic
            : CameraProjection::Perspective
    );
}

EditorInstance::EditorInstance(size_t narcIndex, std::span<const char> data, bool isTemp)
    : m_narcIndex(narcIndex), m_archive(data)
    , m_particleSystem(g_application->getEditor()->getSettings().maxParticles, m_archive.getTextures())
    , m_camera(glm::radians(45.0f), {800, 800}, 1.0f, 500.0f), m_isTemp(isTemp) {

    m_uniqueID = SPLRandom::nextU64();
    m_updateProj = true;
    notifyResourceChanged(0);

    // Choose particle renderer backend
    if (g_application->getEditor()->getSettings().useLegacyParticleRenderer) {
        m_particleSystem.useLegacyRenderer();
        m_particleSystem.getRenderer().setTextures(m_archive.getTextures());
    }

    m_camera.setProjection(
        g_application->getEditor()->getSettings().useOrthographicCamera
        ? CameraProjection::Orthographic
        : CameraProjection::Perspective
    );
}

EditorInstance::EditorInstance(bool isTemp)
    : m_archive(), m_particleSystem(g_application->getEditor()->getSettings().maxParticles, m_archive.getTextures())
    , m_camera(glm::radians(45.0f), { 800, 800 }, 1.0f, 500.0f), m_isTemp(isTemp) {
    m_uniqueID = SPLRandom::nextU64();
    m_updateProj = true;

    g_application->getEditor()->selectResource(m_uniqueID, -1);
    notifyResourceChanged(-1);

    // Choose particle renderer backend
    if (g_application->getEditor()->getSettings().useLegacyParticleRenderer) {
        m_particleSystem.useLegacyRenderer();
        m_particleSystem.getRenderer().setTextures(m_archive.getTextures());
    }

    m_camera.setProjection(
        g_application->getEditor()->getSettings().useOrthographicCamera
            ? CameraProjection::Orthographic
            : CameraProjection::Perspective
    );
}

std::pair<bool, bool> EditorInstance::render() {
    bool open = true;
    bool active = false;

    ImGui::PushID(m_uniqueID & 0x7FFFFFFF);

    m_camera.setViewportHovered(false);

    if (m_isTemp) {
        ImGui::PushFont(g_application->getFont("Italic"));
    }

    const auto& activeEditor = g_projectManager->getActiveEditor();
    const auto flags = g_projectManager->shouldForceActivate() && this == activeEditor.get()
        ? ImGuiTabItemFlags_SetSelected
        : ImGuiTabItemFlags_None;

    std::string name = getName();
    if (m_modified) {
        name += "*";
    }

    const bool openTab = ImGui::BeginTabItem(name.c_str(), &open, flags);

    if (m_isTemp) {
        ImGui::PopFont();
    }

    if (ImGui::BeginItemTooltip()) {
        ImGui::Text("%s", getRelativePath().string().c_str());
        ImGui::EndTooltip();
    }

    // Double click to persist the editor
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        m_isTemp = false;
    }

    if (openTab) {
        active = true;
        m_camera.setActive(true);

        const ImVec2 size = ImGui::GetContentRegionAvail();
        m_size = { size.x, size.y };
        m_size = glm::abs(m_size); // For some reason size.y is sometimes negative idk

        ImGui::Image((ImTextureID)(uintptr_t)m_viewport.getTexture(), size, ImVec2(0, 1), ImVec2(1, 0));
        if (ImGui::IsItemHovered()) {
            m_camera.setViewportHovered(true);
        }

        ImGui::EndTabItem();
    } else {
        m_camera.setActive(false);
    }

    ImGui::PopID();

    return { open, active };
}

void EditorInstance::renderParticles(const std::vector<Renderer*>& renderers) {
    auto renderSize = m_size;

    const auto& settings = g_application->getEditor()->getSettings();
    if (settings.useFixedDsResolution) {
        // We don't *actually* use 256x192, but we scale the viewport to match the aspect ratio
        // so there isn't any stretching or squishing.
        const float aspect = m_size.x / m_size.y;
        const float baseHeight = 192.0f * settings.fixedDsResolutionScale;

        renderSize.x = baseHeight * aspect;
        renderSize.y = baseHeight;
    }

    if (m_updateProj || renderSize != m_viewport.getSize()) {
        m_viewport.resize(renderSize, settings.fixedDsResolutionScale);
        m_camera.setViewport(renderSize.x, renderSize.y);
        m_updateProj = false;

        // Intentionally not setting m_size here as it represents the actual size of the editor viewport.
    }

    m_viewport.bind();

    const auto clearColor = settings.backgroundColor;
    glClearColor(clearColor.r, clearColor.g, clearColor.b, clearColor.a);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    for (const auto renderer : renderers) {
        renderer->render(m_camera.getView(), m_camera.getProj());
    }

    glDepthMask(GL_FALSE);
    m_particleSystem.render(m_camera.getParams());
    glDepthMask(GL_TRUE);

    m_viewport.unbind();
}

void EditorInstance::updateParticles(float deltaTime) {
    m_camera.update();
    m_particleSystem.update(deltaTime);
}

void EditorInstance::handleEvent(const SDL_Event& event) {
    m_camera.handleEvent(event);

    if (event.type == SDL_EVENT_KEY_DOWN) {
        if (event.key.key == SDLK_R && event.key.mod & SDL_KMOD_CTRL) {
            m_camera.reset();
        }
    }
}

void EditorInstance::useModernRenderer() {
    m_particleSystem.useModernRenderer();
    m_particleSystem.getRenderer().setTextures(m_archive.getTextures());
}

void EditorInstance::useLegacyRenderer() {
    m_particleSystem.useLegacyRenderer();
    m_particleSystem.getRenderer().setTextures(m_archive.getTextures());
}

bool EditorInstance::notifyClosing() {
    return true;
}

void EditorInstance::notifyResourceChanged(size_t index) {
    if (index == -1) {
        m_selectedResource = -1;
        return;
    }

    if (index >= m_archive.getResources().size()) {
        return;
    }

    m_selectedResource = index;
    m_resourceBefore = m_archive.getResources().at(index).duplicate();
}

bool EditorInstance::valueChanged(bool changed) {
    if (m_selectedResource >= m_archive.getResources().size()) {
        return false;
    }

    if (changed) {
        m_isTemp = false; // Changing anything inside a "temporary" editor will make it persistent
    }

    m_modified |= changed;

    if (ImGui::IsItemDeactivatedAfterEdit()) {
        pushHistory();
    }

    return changed;
}

void EditorInstance::duplicateResource(size_t index) {
    if (index >= m_archive.getResources().size()) {
        return;
    }

    auto& resources = m_archive.getResources();
    const auto& resource = resources[index];
    const auto& newResource = resources.emplace_back(resource.duplicate());

    m_history.push({
        .type = EditorActionType::ResourceAdd,
        .resourceIndex = resources.size() - 1,
        .before = {}, // No before state for new resources
        .after = newResource.duplicate()
    });
}

void EditorInstance::deleteResource(size_t index) {
    if (index >= m_archive.getResources().size()) {
        return;
    }

    auto& resources = m_archive.getResources();
    const auto& resource = resources[index];

    m_history.push({
        .type = EditorActionType::ResourceRemove,
        .resourceIndex = index,
        .before = resource.duplicate(),
        .after = {} // No after state for deleted resources
    });

    resources.erase(resources.begin() + index);
}

void EditorInstance::addResource() {
    auto& resources = m_archive.getResources();
    const auto& resource = resources.emplace_back(SPLResource::create());

    m_history.push({
        .type = EditorActionType::ResourceAdd,
        .resourceIndex = resources.size() - 1,
        .before = {}, // No before state for new resources
        .after = resource.duplicate()
    });
}

void EditorInstance::save() {
    if (m_narcIndex != -1) {
        spdlog::error("Saving NARC editors not supported yet");
        return;
    }

    if (m_path.empty()) {
        const auto file = Application::saveFile();
        if (!file.empty()) {
            m_path = file;
        }
    }

    m_archive.save(m_path);
    m_modified = false;
}

void EditorInstance::saveAs(const std::filesystem::path& path) {
    m_path = path;
    m_narcIndex = -1; // Reset NARC index since we're saving to a new file
    return save();
}

void EditorInstance::pushHistory() {
    if (m_selectedResource >= m_archive.getResources().size()) {
        return;
    }

    const auto after = m_archive.getResources().at(m_selectedResource).duplicate();

    m_history.push({
        .type = EditorActionType::ResourceModify,
        .resourceIndex = m_selectedResource,
        .before = m_resourceBefore,
        .after = after
    });

    m_resourceBefore = after.duplicate();
}

EditorActionType EditorInstance::undo() {
    if (m_history.canUndo()) {
        m_modified = true;
        return m_history.undo(m_archive.getResources());
    }

    return EditorActionType::None;
}

EditorActionType EditorInstance::redo() {
    if (m_history.canRedo()) {
        m_modified = true;
        return m_history.redo(m_archive.getResources());
    }

    return EditorActionType::None;
}

std::filesystem::path EditorInstance::getRelativePath() const {
    if (m_narcIndex != -1) {
        return fmt::format("N{:05d}.spa", m_narcIndex);
    }

    if (m_path.empty()) {
        return "Untitled-" + std::to_string(m_uniqueID & 0xFF) + ".spa";
    }

    if (g_projectManager->getProjectPath().empty()) {
        return m_path;
    }

    return std::filesystem::relative(m_path, g_projectManager->getProjectPath());
}

std::string EditorInstance::getName() const {
    if (m_narcIndex != -1) {
        return fmt::format("N{:05d}.spa", m_narcIndex);
    }

    if (m_path.empty()) {
        return "Untitled-" + std::to_string(m_uniqueID & 0xFF);
    }

    return m_path.filename().string();
}
