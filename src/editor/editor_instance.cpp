#include "editor_instance.h"
#include "application.h"
#include "application_colors.h"
#include "project_manager.h"
#include "spl/spl_random.h"
#include "spl/enum_names.h"
#include "gfx/gl_util.h"
#include "imgui/extensions.h"
#include "help_messages.h"

#include <array>
#include <cinttypes>
#include <random>

#include <GL/glew.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <im_anim.h>
#include <implot.h>
#include <tinyfiledialogs.h>
#include <fonts/IconsFontAwesome6.h>
#include <fmt/format.h>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/integer.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <libimagequant.h>
#include <spng.h>
#include <stb_image.h>

#define NOTIFY(action) valueChanged(action)
#define HELP(name) helpPopup(help::name)


namespace {

constexpr std::array s_emitterSpawnTypes = {
    "Single Shot",
    "Looped",
    "Interval"
};

const u32 s_hoverAccentColor = ImGui::ColorConvertFloat4ToU32({ 0.7f, 0.3f, 0.7f, 1.0f });

}

EditorInstance::EditorInstance(const std::filesystem::path& path, bool isTemp, bool isRecovered)
    : m_path(path), m_archive(path)
    , m_particleSystem(g_application->getEditor()->getSettings().maxParticles, m_archive.getTextures())
    , m_camera(glm::radians(45.0f), { 800, 800 }, 1.0f, 500.0f), m_isTemp(isTemp), m_isRecovered(isRecovered) {

    if (m_isRecovered) {
        m_modified = true;
    }

    createRenderers();
    m_lastBackupTime = Clock::now();
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

EditorInstance::EditorInstance(std::string name, size_t narcIndex, std::span<const u8> data, bool isTemp)
    : m_narcIndex(narcIndex), m_narcMemberName(std::move(name)), m_archive(data)
    , m_particleSystem(g_application->getEditor()->getSettings().maxParticles, m_archive.getTextures())
    , m_camera(glm::radians(45.0f), {800, 800}, 1.0f, 500.0f), m_isTemp(isTemp) {

    createRenderers();
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

    createRenderers();
    notifyResourceChanged(INVALID_RESOURCE);

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
        ImGui::PushFont(g_application->getFont("Italic"), 0.0f);
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
        ImGui::TextUnformatted(getRelativePath().string().c_str());
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

void EditorInstance::update(float deltaTime) {
    const float timeScale = g_application->getEditor()->getTimeScale();

    const auto now = Clock::now();
    for (auto& task : m_emitterTasks) {
        if (timeScale * (now - task.time) >= task.interval) {
            m_particleSystem.addEmitter(m_archive.getResources()[task.resourceIndex], false);
            task.time = now;
        }
    }

    m_camera.update();
    m_particleSystem.update(deltaTime * timeScale);
}

void EditorInstance::playEmitter(EmitterSpawnType spawnType) {
    const auto resourceIndex = m_selectedResource;
    if (resourceIndex >= m_archive.getResources().size()) {
        spdlog::warn("Invalid resource index: {}", resourceIndex);
        return;
    }

    m_particleSystem.addEmitter(m_archive.getResource(resourceIndex), spawnType == EmitterSpawnType::Looped);

    if (spawnType == EmitterSpawnType::Interval) {
        m_emitterTasks.emplace_back(resourceIndex, Clock::now(), std::chrono::duration<float>(m_emitterInterval));
    }
}

void EditorInstance::playAllEmitters(EmitterSpawnType spawnType) {
    for (size_t i = 0; i < m_archive.getResources().size(); ++i) {
        m_particleSystem.addEmitter(m_archive.getResource(i), spawnType == EmitterSpawnType::Looped);

        if (spawnType == EmitterSpawnType::Interval) {
            m_emitterTasks.emplace_back(i, Clock::now(), std::chrono::duration<float>(m_emitterInterval));
        }
    }
}

void EditorInstance::killEmitters() {
    m_particleSystem.killAllEmitters();
    m_emitterTasks.clear();
}

void EditorInstance::resetCamera() {
    m_camera.reset();
}

void EditorInstance::renderStats() {
    const auto& system = m_particleSystem;

    const auto activeParticles = system.getParticleCount();
    const auto maxParticles = system.getMaxParticles();
    const auto fraction = static_cast<float>(activeParticles) / maxParticles;
    const auto particleText = fmt::format("Particles: {}/{}", activeParticles, maxParticles);

    constexpr u32 colorLow = IM_COL32(0, 200, 0, 255);
    constexpr u32 colorHigh = IM_COL32(220, 0, 0, 255);
    ImGui::AnimatedStatBar("##particleBar", fraction, particleText.c_str(), colorLow, colorHigh);

    const auto emitterCount = system.getEmitters().size();
    const float dt = ImGui::GetIO().DeltaTime;
    const ImGuiID flashId = ImGui::GetID("##emitterFlash");

    if (emitterCount != m_lastEmitterCount) {
        m_emitterFlashColor = emitterCount > m_lastEmitterCount ? AppColors::LightGreen : AppColors::LightRed;
        iam_tween_float(flashId, 0, 1.0f, 0.0f, iam_ease_preset(iam_ease_linear), iam_policy_cut, dt, 0.0f);
        m_lastEmitterCount = emitterCount;
    }

    const float flash = iam_tween_float(
        flashId, 0, 0.0f, 0.45f,
        iam_ease_preset(iam_ease_out_cubic),
        iam_policy_crossfade, dt, 0.0f
    );

    const auto emitterText = fmt::format("Active Emitters: {}", emitterCount);
    if (flash > 0.001f && m_emitterFlashColor != 0) {
        const auto& style = ImGui::GetStyle();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const ImVec2 ts = ImGui::CalcTextSize(emitterText.c_str());
        const ImVec2 pad = { style.FramePadding.x, style.FramePadding.y * 0.5f };

        ImVec4 hl = ImGui::ColorConvertU32ToFloat4(m_emitterFlashColor);
        hl.w = flash * 0.35f;
        ImGui::GetWindowDrawList()->AddRectFilled(
            p - pad,
            { p.x + ts.x + pad.x, p.y + ts.y + pad.y },
            ImGui::ColorConvertFloat4ToU32(hl),
            style.FrameRounding
        );
    }

    ImGui::TextUnformatted(emitterText.c_str());
}

void EditorInstance::renderPanels() {
    auto* const shell = g_application->getEditor();
    if (shell->pickerOpen()) {
        renderResourcePicker(&shell->pickerOpen());
    }
    if (shell->textureManagerOpen()) {
        renderTextureManager(&shell->textureManagerOpen());
    }
    if (shell->resourceEditorOpen()) {
        renderResourceEditor(&shell->resourceEditorOpen());
    }
}

void EditorInstance::renderResourcePicker(bool* open) {
    if (ImGui::Begin("Resource Picker##Editor", open)) {

        const auto& style = ImGui::GetStyle();
        constexpr float thumbnailSize = 48.0f;
        constexpr ImVec2 thumbnailSizeVec = { thumbnailSize, thumbnailSize };
        const float itemHeight = thumbnailSize + style.FramePadding.y * 2;

        auto* const editor = this;

        const SPLResourceCopy* toPaste = nullptr;
        size_t pasteIndex = -1;

        auto& archive = editor->getArchive();
        auto& resources = archive.getResources();
        auto& textures = archive.getTextures();

        bool anyHovered = false;

        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.40f, 0.40f, 0.40f, 0.84f));

        const auto contentRegion = ImGui::GetContentRegionAvail();
        if (ImGui::BeginListBox("##Resources", contentRegion)) {
            const float itemWidth = contentRegion.x - (style.ItemSpacing.x + style.WindowPadding.x) * 1.3f;
            const float dt = ImGui::GetIO().DeltaTime;

            // Detect selection changes
            const size_t selected = m_selectedResource;
            const ImGuiID accentId = ImGui::GetID("##pickerAccent");
            bool selectionChanged = false;
            if (m_lastPickerSelection != selected) {
                m_lastPickerSelection = selected;
                selectionChanged = true;
                iam_tween_float(accentId, 0, 0.0f, 0.0f, iam_ease_preset(iam_ease_linear), iam_policy_cut, dt, 0.0f);
            }

            const float accentGrow = iam_tween_float(
                accentId, 0,
                selected != (size_t)-1 ? 1.0f : 0.0f, 0.3f,
                iam_ease_preset(iam_ease_out_cubic),
                iam_policy_crossfade, dt, 0.0f
            );

            for (size_t i = 0; i < resources.size(); ++i) {
                const auto& resource = resources[i];
                const auto& texture = textures[resource.header.misc.textureIndex];

                ImGui::PushID(i);
                const auto itemId = ImGui::GetID("##Resource");

                const auto name = fmt::format("[{}] Tex {}x{}", i, texture.width, texture.height);

                auto bgColor = m_selectedResource == i
                    ? style.Colors[ImGuiCol_ButtonActive]
                    : style.Colors[ImGuiCol_Button];

                const auto cursor = ImGui::GetCursorScreenPos();
                if (ImGui::InvisibleButton("##Resource", { itemWidth, itemHeight })) {
                    m_selectedResource = i;
                    editor->notifyResourceChanged(i);
                }

                const bool hovered = ImGui::IsItemHovered();
                const bool pressed = ImGui::IsItemActive();

                if (hovered) {
                    anyHovered = true;

                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                        ImGui::OpenPopup("##ResourcePopup");
                    }
                }

                const auto targetColor = pressed
                    ? style.Colors[ImGuiCol_SliderGrabActive]
                    : hovered ? style.Colors[ImGuiCol_ButtonHovered] : bgColor;

                bgColor = iam_tween_color(
                    itemId,
                    1,
                    targetColor,
                    0.2f,
                    iam_ease_preset(iam_ease_linear),
                    iam_policy_crossfade,
                    iam_col_srgb,
                    dt,
                    bgColor
                );

                const auto bgColor2 = bgColor * ImVec4(0.8f, 0.8f, 0.8f, 1.0f);

                // Draw a filled rectangle behind the item
                const auto drawList = ImGui::GetWindowDrawList();
                const int firstVtx = drawList->VtxBuffer.Size;
                drawList->AddRectFilled(
                    cursor, 
                    { cursor.x + itemWidth, cursor.y + itemHeight },
                    ImGui::ColorConvertFloat4ToU32(bgColor),
                    style.FrameRounding
                );
                const int lastVtx = drawList->VtxBuffer.Size;

                ImGui::ShadeVertsLinearColorGradientKeepAlpha(drawList,
                    firstVtx, lastVtx,
                    cursor, { cursor.x, cursor.y + itemHeight },
                    ImGui::ColorConvertFloat4ToU32(bgColor),
                    ImGui::ColorConvertFloat4ToU32(bgColor2)
                );

                // Selection accent bar on the left edge, eased in on selection change
                if (m_selectedResource == i && accentGrow > 0.001f) {
                    const float barHeight = itemHeight * accentGrow;
                    const float barTop = cursor.y + (itemHeight - barHeight) * 0.5f;
                    ImVec4 accent = ImGui::ColorConvertU32ToFloat4(AppColors::LightBlue);
                    accent.w = accentGrow;
                    drawList->AddRectFilled(
                        { cursor.x, barTop },
                        { cursor.x + 3.0f, barTop + barHeight },
                        ImGui::ColorConvertFloat4ToU32(accent),
                        1.5f
                    );

                    if (selectionChanged) {
                        const float itemContentY = (cursor.y - ImGui::GetWindowPos().y) + ImGui::GetScrollY();
                        const float targetScroll = itemContentY - ImGui::GetWindowHeight() * 0.5f + itemHeight * 0.5f;
                        iam_scroll_to_y(targetScroll, 0.25f);
                    }
                }

                const ImVec2 offset = {
                    iam_tween_float(
                        itemId,
                        0,
                        hovered ? 8.0f : 0.0f,
                        0.25f,
                        iam_ease_preset(iam_ease_in_out_quad),
                        iam_policy_crossfade,
                        dt
                    ),
                    0.0f
                };

                const auto imagePos = cursor + style.FramePadding + offset;
                const auto frameMinPos = imagePos - ImVec2(2, 2);
                const auto frameMaxPos = imagePos + ImVec2(thumbnailSize + 2, thumbnailSize + 2);
                drawList->AddRectFilled(
                    frameMinPos,
                    frameMaxPos,
                    IM_COL32(60, 60, 60, 255),
                    2
                );

                drawList->AddRect(
                    frameMinPos,
                    frameMaxPos,
                    IM_COL32(230, 230, 230, 30),
                    2
                );

                ImGui::SetCursorScreenPos(imagePos);
                ImGui::Image(texture.glTexture->getHandle(), thumbnailSizeVec);

                ImGui::SameLine();

                ImGui::PushFont(nullptr, Application::getFontSizeLarge());
                const auto textHeight = ImGui::GetFontSize();
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (thumbnailSize - textHeight) / 2.0f);
                ImGui::TextUnformatted(name.c_str());
                ImGui::PopFont();

                if (ImGui::BeginPopup("##ResourcePopup")) {
                    if (ImGui::MenuItemIcon(ICON_FA_COPY, "Copy", "Ctrl+C", false, AppColors::LightGreen3)) {
                        g_application->getEditor()->pushClipboard(editor->getName(), resource);
                        ImGui::CloseCurrentPopup();
                    }

                    if (ImGui::MenuItemIcon(ICON_FA_COPY, "Copy with Texture", "Ctrl+Shift+C", false, AppColors::LightGreen3)) {
                        g_application->getEditor()->pushClipboard(editor->getName(), resource, texture);
                        ImGui::CloseCurrentPopup();
                    }

                    if (ImGui::MenuItemIcon(ICON_FA_PASTE, "Paste", "Ctrl+V", false, AppColors::LightOrange, !g_application->getEditor()->clipboardHistory().empty())) {
                        const auto& copy = g_application->getEditor()->clipboardHistory().back();
                        if (!copy.resource) {
                            spdlog::error("No resource in clipboard to paste");
                        } else {
                            toPaste = &copy;
                            pasteIndex = i;
                        }

                        ImGui::CloseCurrentPopup();
                    }

                    if (ImGui::MenuItemIcon(ICON_FA_CLONE, "Duplicate", nullptr, false, AppColors::LightBlue2)) {
                        editor->duplicateResource(i);
                        m_selectedResource = resources.size() - 1;
                        editor->notifyResourceChanged(resources.size() - 1);

                        ImGui::CloseCurrentPopup();
                    }

                    if (ImGui::MenuItemIcon(ICON_FA_TRASH, "Delete", nullptr, false, AppColors::Gray)) {
                        if (m_selectedResource == i) {
                            m_selectedResource = -1;
                            editor->notifyResourceChanged(-1);
                        }

                        editor->deleteResource(i);
                        ImGui::CloseCurrentPopup();
                    }

                    ImGui::EndPopup();
                }

                ImGui::SetCursorScreenPos(cursor);
                ImGui::Dummy({ itemWidth, itemHeight });

                ImGui::PopID();
            }

            ImGui::EndListBox();
        }

        ImGui::PopStyleColor();

        if (!anyHovered
            && ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows) 
            && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            ImGui::OpenPopup("##AddResourcePopup");
        }

        if (ImGui::BeginPopup("##AddResourcePopup")) {
            if (ImGui::MenuItemIcon(ICON_FA_CIRCLE_PLUS, "Add Resource")) {
                killEmitters(); // Stop all emitters before adding a new resource to avoid crashes
                editor->addResource();
                m_selectedResource = resources.size() - 1;
                editor->notifyResourceChanged(resources.size() - 1);
                ImGui::CloseCurrentPopup();
            }

            if (ImGui::MenuItemIcon(ICON_FA_PASTE, "Paste", "Ctrl+V", false, AppColors::LightOrange, !g_application->getEditor()->clipboardHistory().empty())) {
                const auto& copy = g_application->getEditor()->clipboardHistory().back();
                if (!copy.resource) {
                    spdlog::error("No resource in clipboard to paste");
                } else {
                    toPaste = &copy;
                    pasteIndex = -1; // Paste as new resource
                }
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }

        if (ImGui::IsWindowFocused() || ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)) {
            if (ImGui::IsKeyPressed(ImGuiKey_C) && ImGui::GetIO().KeyCtrl) {
                if (editor && m_selectedResource != -1) {
                    const auto& resource = resources[m_selectedResource];
                    
                    if (ImGui::GetIO().KeyShift) {
                        g_application->getEditor()->pushClipboard(editor->getName(), resource, textures[resource.header.misc.textureIndex]);
                    } else {
                        g_application->getEditor()->pushClipboard(editor->getName(), resource);
                    }
                }
            }

            if (ImGui::IsKeyPressed(ImGuiKey_V) && ImGui::GetIO().KeyCtrl) {
                if (!g_application->getEditor()->clipboardHistory().empty()) {
                    const auto& copy = g_application->getEditor()->clipboardHistory().back();
                    if (copy.resource) {
                        toPaste = &copy;
                        pasteIndex = -1; // Paste at end
                    } else {
                        spdlog::error("No resource in clipboard to paste");
                    }
                }
            }
        }

        if (toPaste) {
            size_t texIndex = 0;
            if (toPaste->texture) {
                // Texture always pasted at the end
                texIndex = textures.size();

                auto& tex = *toPaste->texture;

                auto& texData = archive.getTextureData();
                auto& palData = archive.getPaletteData();

                texData.push_back(std::move(tex.data));
                palData.push_back(std::move(tex.pltt));

                auto& existingTextures = archive.getTextures();
                existingTextures.emplace_back(std::move(tex.texture));

                editor->getParticleSystem().getRenderer().setTextures(existingTextures);
            }

            if (toPaste->resource) {
                if (pasteIndex >= resources.size() || pasteIndex == -1) {
                    auto& res = resources.emplace_back(std::move(*toPaste->resource));
                    res.header.misc.textureIndex = static_cast<u8>(texIndex);
                } else {
                    const auto res = resources.emplace(resources.begin() + pasteIndex, std::move(*toPaste->resource));
                    res->header.misc.textureIndex = static_cast<u8>(texIndex);
                }
            }

            editor->valueChanged(true);
        }
    }

    ImGui::End();
}

void EditorInstance::renderTextureManager(bool* open) {
    if (ImGui::Begin("Texture Manager##Editor", open)) {
        auto* const editor = this;

        // Process deferred actions from a previous frame before touching the archive.
        if (m_discardTempTexture) {
            destroyTempTexture();
        }
        if (m_deleteSelectedTexture) {
            m_archive.deleteTexture(m_selectedTexture);
            m_selectedTexture = -1;
            m_deleteSelectedTexture = false;
        }

        auto& archive = editor->getArchive();
        auto& textures = archive.getTextures();

        const auto importPopupId = ImGui::GetID("##ImportTexturePopup");
        const auto deleteTexturePopupId = ImGui::GetID("##DeleteTexturePopup");

        if (ImGui::IconButton(ICON_FA_FILE_IMPORT, "Import", AppColors::LightBlue)) {
            const auto path = tinyfd_openFileDialog(
                "Import Texture", 
                "", 
                0, 
                nullptr, 
                "Image Files", 
                0
            );
            if (path) {
                openTempTexture(path);
                ImGui::OpenPopup(importPopupId);
            }
        }

        ImGui::SameLine();

        if (ImGui::IconButton(ICON_FA_FILE_EXPORT, "Export All...", AppColors::Yellow)) {
            const auto path = Application::openDirectory("Select Destination");
            if (!path.empty()) {
                archive.exportTextures(path, Application::getTempPath());
            }
        }

        ImGui::BeginChild("##TextureList", {}, ImGuiChildFlags_Borders);
        bool anyHovered = false;

        const ImVec2 padding = { ImGui::GetStyle().FramePadding.x, 16.0f - ImGui::GetTextLineHeight() * 0.5f };

        for (int i = 0; i < textures.size(); ++i) {
            auto& texture = textures[i];
            const auto name = fmt::format("[{}] Tex {}x{}", i, texture.width, texture.height);
            
            ImGui::Image((ImTextureID)texture.glTexture->getHandle(), { 32, 32 });
            ImGui::SameLine();
            const bool open = ImGui::PaddedTreeNode(name.c_str(), padding, ImGuiTreeNodeFlags_SpanAvailWidth);

            if (ImGui::BeginPopupContextItem(fmt::format("##TexturePopup{}", i).c_str())) {
                if (ImGui::MenuItemIcon(ICON_FA_FILE_IMPORT, "Update...", nullptr, false, AppColors::LightBlue2)) {
                    const auto path = tinyfd_openFileDialog(
                        "Update Texture",
                        "",
                        0,
                        nullptr,
                        "Image Files",
                        0
                    );

                    if (path) {
                        openTempTexture(path, i);
                        ImGui::OpenPopup(importPopupId);
                    }
                }

                if (ImGui::MenuItemIcon(ICON_FA_FILE_EXPORT, "Export...", nullptr, false, AppColors::Yellow)) {
                    const char* filterPatterns[] = { "*.png", "*.bmp", "*.tga" };
                    const auto path = tinyfd_saveFileDialog(
                        "Export Texture",
                        fmt::format("texture_{}.png", i).c_str(),
                        std::size(filterPatterns),
                        filterPatterns,
                        nullptr
                    );

                    if (path) {
                        archive.exportTexture(i, path);
                    }
                }

                if (ImGui::MenuItemIcon(ICON_FA_COPY, "Copy", nullptr, false, AppColors::LightGreen3)) {
                    g_application->getEditor()->pushClipboard(editor->getName(), texture);
                }

                if (ImGui::MenuItemIcon(ICON_FA_TRASH, "Delete", nullptr, false, AppColors::Gray)) {
                    m_selectedTexture = i;
                    ImGui::OpenPopup(deleteTexturePopupId);
                }

                ImGui::EndPopup();
            }

            if (open) {
                ImGui::Text("Format: %s", getTextureFormat(texture.param.format));

                if (ImGui::BeginCombo("Repeat", getTextureRepeat(texture.param.repeat))) {
                    for (const auto [val, name] : detail::g_textureRepeatNames) {
                        if (editor->valueChanged(ImGui::Selectable(name, texture.param.repeat == val))) {
                            texture.param.repeat = val;
                            texture.glTexture->setWrapping(texture.param.repeat, texture.param.flip);
                        }
                    }

                    ImGui::EndCombo();
                }

                if (ImGui::BeginCombo("Flip", getTextureFlip(texture.param.flip))) {
                    for (const auto [val, name] : detail::g_textureFlipNames) {
                        if (editor->valueChanged(ImGui::Selectable(name, texture.param.flip == val))) {
                            texture.param.flip = val;
                            texture.glTexture->setWrapping(texture.param.repeat, texture.param.flip);
                        }
                    }

                    ImGui::EndCombo();
                }

                if (editor->valueChanged(ImGui::Checkbox("Palette Color 0 Transparent", &texture.param.palColor0Transparent))) {
                    texture.glTexture->update(texture);
                }

                editor->valueChanged(ImGui::Checkbox("Use Shared Texture", &texture.param.useSharedTexture));

                if (texture.param.useSharedTexture) {
                    ImGui::InputScalar("Shared Texture ID", ImGuiDataType_U8, &texture.param.sharedTexID);
                }

                ImGui::TreePop();
            }

            anyHovered |= ImGui::IsItemHovered();
        }

        ImGui::EndChild();

        const auto copyTexture = [&] {
            auto& clipboard = g_application->getEditor()->clipboardHistory();
            const auto& copy = clipboard.back();
            if (!copy.texture || copy.resource) {
                spdlog::error("No texture in clipboard to paste");
            } else {
                auto& tex = *clipboard.back().texture;

                auto& texData = archive.getTextureData();
                auto& palData = archive.getPaletteData();

                texData.push_back(std::move(tex.data));
                palData.push_back(std::move(tex.pltt));

                auto& existingTextures = archive.getTextures();
                existingTextures.emplace_back(std::move(tex.texture));

                editor->getParticleSystem().getRenderer().setTextures(existingTextures);
                editor->valueChanged(true);
            }
        };

        if (ImGui::IsWindowFocused() || ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)) {
           if (ImGui::IsKeyPressed(ImGuiKey_V) && ImGui::GetIO().KeyCtrl && !g_application->getEditor()->clipboardHistory().empty()) {
                copyTexture();
           }
        }

        if (!anyHovered && ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItemIcon(ICON_FA_PASTE, "Paste", "Ctrl+V", false, AppColors::LightOrange, !g_application->getEditor()->clipboardHistory().empty())) {
                copyTexture();
            }

            ImGui::EndPopup();
        }

        const auto popupPos = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(popupPos, ImGuiCond_Appearing, { 0.5f, 0.5f });
        if (ImGui::BeginPopupModal("##ImportTexturePopup")) {
            const auto textureSize = ImVec2(m_tempTexture->width, m_tempTexture->height) * m_tempTextureScale;
            const ImVec2 tableSize = { std::max(textureSize.x, 300.0f), 0.0f};
            const auto& style = ImGui::GetStyle();

            if (ImGui::BeginTable("##TempTextureTable", 2, 
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchSame, tableSize)) {

                ImGui::TableSetupColumn("##aaa", ImGuiTableColumnFlags_WidthStretch, 0.5f);
                ImGui::TableSetupColumn("##bbb", ImGuiTableColumnFlags_WidthStretch, 0.5f);

                ImGui::TableNextColumn(); ImGui::Text("Size");
                ImGui::TableNextColumn(); ImGui::Text("%dx%d", m_tempTexture->width, m_tempTexture->height);
                
                ImGui::TableNextColumn(); ImGui::Text("Channels");
                ImGui::TableNextColumn(); ImGui::Text("%d", m_tempTexture->channels);

                ImGui::TableNextColumn(); ImGui::Text("Unique Colors");
                ImGui::TableNextColumn(); ImGui::Text("%" PRIu64, m_tempTexture->spec.uniqueColors.size());

                ImGui::TableNextColumn(); ImGui::Text("Unique Alphas");
                ImGui::TableNextColumn(); ImGui::Text("%" PRIu64, m_tempTexture->spec.uniqueAlphas.size());

                const auto estimatedSize = m_tempTexture->spec.getSizeEstimate(m_tempTexture->width, m_tempTexture->height);
                ImGui::TableNextColumn(); ImGui::Text("Estimated Size");
                ImGui::TableNextColumn();
                if (estimatedSize >= 1024) {
                        ImGui::Text("%zu kB", estimatedSize / 1024);
                } else {
                        ImGui::Text("%zu B", estimatedSize);
                }

                ImGui::TableNextColumn(); ImGui::Text("Format");
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(tableSize.x * 0.5f - style.CellPadding.x * 2);
                if (ImGui::BeginCombo("##Format", getTextureFormat(m_tempTexture->spec.format))) {
                    for (auto i = (int)TextureFormat::A3I5; i < (int)TextureFormat::Count; i++) {
                        const auto flags = (TextureFormat)i == TextureFormat::Comp4x4 ? ImGuiSelectableFlags_Disabled : 0;
                        if (ImGui::Selectable(getTextureFormat((TextureFormat)i), (int)m_tempTexture->spec.format == i, flags)) {
                            m_tempTexture->spec.setFormat((TextureFormat)i);

                            if (m_tempTexture->spec.format == m_tempTexture->suggestedFormat && m_tempTexture->suggestedFormatUncompressed) {
                                std::memcpy(
                                    m_tempTexture->quantized,
                                    m_tempTexture->data,
                                    (size_t)m_tempTexture->width * m_tempTexture->height * 4
                                );
                            } else {
                                quantizeTexture(
                                    m_tempTexture->data,
                                    m_tempTexture->width,
                                    m_tempTexture->height,
                                    m_tempTexture->spec,
                                    m_tempTexture->quantized
                                );
                            }

                            m_tempTexture->texture->update(m_tempTexture->quantized);
                        }
                    }

                    ImGui::EndCombo();
                }

                ImGui::TableNextColumn(); ImGui::Text("Color Compression");
                ImGui::TableNextColumn(); ImGui::Text("%s", m_tempTexture->spec.requiresColorCompression ? "Yes" : "No");

                ImGui::TableNextColumn(); ImGui::Text("Alpha Compression");
                ImGui::TableNextColumn(); ImGui::Text("%s", m_tempTexture->spec.requiresAlphaCompression ? "Yes" : "No");

                ImGui::EndTable();
            }

            ImGui::SetNextItemWidth(150.0f - style.CellPadding.x * 2);
            ImGui::SliderFloat("Display Scale", &m_tempTextureScale, 0.1f, 8.0f, "%.2fx");
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::BeginItemTooltip())
            {
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
                ImGui::TextUnformatted("Only for checking how the texture looks. Does not affect the actual imported texture.");
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }

            ImGui::Image((ImTextureID)m_tempTexture->texture->getHandle(), textureSize);

            if (!m_tempTexture->isValidSize) {
                ImGui::TextColored({ 0.93f, 0, 0, 1 }, "Invalid Texture Size (?)");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Both the width and the height of the texture must be a power of 2\n"
                                      "and they must be in the range [8, 1024]");
                }
                ImGui::BeginDisabled();
            }
            
            if (ImGui::Button("Confirm") && m_tempTexture->isValidSize) {
                importTempTexture();
                ImGui::CloseCurrentPopup();
            }

            if (!m_tempTexture->isValidSize) {
                ImGui::EndDisabled();
            }

            ImGui::SameLine();

            if (ImGui::Button("Cancel")) {
                discardTempTexture();
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }

        if (ImGui::BeginPopupModal("##DeleteTexturePopup")) {
            ImGui::Text("Are you sure you want to delete this texture?");
            ImGui::TextDisabled("(This might break existing resources)");
            ImGui::Separator();

            const auto texCount = archive.getTextureCount();
            if (texCount <= 1) {
                ImGui::TextColored({ 0.93f, 0.2f, 0.2f, 1 }, "You cannot delete the last texture.");
                ImGui::BeginDisabled();
            }

            if (ImGui::Button("Yes")) {
                m_deleteSelectedTexture = true;
                ImGui::CloseCurrentPopup();
            }

            if (texCount <= 1) {
                ImGui::EndDisabled();
            }

            ImGui::SameLine();
            if (ImGui::Button("No")) {
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }

    ImGui::End();
}

void EditorInstance::renderResourceEditor(bool* open) {
    if (ImGui::Begin("Resource Editor##Editor", open)) {
        ImGui::SliderFloat("Global Time Scale", &g_application->getEditor()->timeScale(), 0.0f, 2.0f, "%.2f");

        auto* const editor = this;

        auto& archive = editor->getArchive();
        auto& resources = archive.getResources();
        auto& textures = archive.getTextures();

        if (m_selectedResource != -1) {
            auto& resource = resources[m_selectedResource];
            const auto& texture = textures[resource.header.misc.textureIndex];

            if (ImGui::IconButton(ICON_FA_PLAY, "Play Emitter", AppColors::LightGreen)) {
                playEmitter(m_emitterSpawnType);
            }

            ImGui::SameLine();
            ImGui::SetNextItemWidth(150);
            ImGui::Combo("##SpawnType", (int*)&m_emitterSpawnType, s_emitterSpawnTypes.data(), s_emitterSpawnTypes.size());

            if (m_emitterSpawnType == EmitterSpawnType::Interval) {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                ImGui::InputFloat("##Interval", &m_emitterInterval, 0.1f, 1.0f, "%.2fs");
            }

            if (ImGui::IconButton(ICON_FA_PLAY, "Play All Emitters", AppColors::LightGreen)) {
                playAllEmitters(m_emitterSpawnType);
            }

            if (ImGui::IconButton(ICON_FA_STOP, "Kill Emitters", AppColors::LightRed)) {
                killEmitters();
            }

            //if (ImGui::RedButton("Kill Emitters")) {
            //    killEmitters();
            //}

            if (ImGui::BeginTabBar("##editorTabs")) {
                if (ImGui::BeginTabItem("General")) {
                    ImGui::BeginChild("##headerEditor", {}, ImGuiChildFlags_Borders);
                    renderHeaderEditor(resource.header);
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Behaviors")) {
                    ImGui::BeginChild("##headerEditor", {}, ImGuiChildFlags_Borders);
                    renderBehaviorEditor(resource);
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Animations")) {
                    ImGui::BeginChild("##animationEditor", {}, ImGuiChildFlags_Borders);
                    renderAnimationEditor(resource);
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Children")) {
                    ImGui::BeginChild("##childEditor", {}, ImGuiChildFlags_Borders);
                    renderChildrenEditor(resource);
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }

                ImGui::EndTabBar();
            }
        }
    }

    ImGui::End();
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
    if (index == INVALID_RESOURCE) {
        m_selectedResource = INVALID_RESOURCE;
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
    if (m_narcIndex != std::numeric_limits<size_t>::max()) {
        std::vector<u8> serialized;
        m_archive.save(serialized);
        g_projectManager->updateNarcMember(m_narcIndex, serialized);
        m_modified = false;
        m_isRecovered = false;
        return;
    }

    if (m_path.empty() || m_isRecovered) {
        const auto file = Application::saveFile();
        if (!file.empty()) {
            m_path = file;
        } else {
            spdlog::warn("Aborted save, no path specified");
            return;
        }
    }

    m_archive.save(m_path);
    m_modified = false;
    m_isRecovered = false;
    
    const auto backupPath = getBackupPath();
    if (std::filesystem::exists(backupPath)) {
        std::filesystem::remove(backupPath);
    }
}

void EditorInstance::saveAs(const std::filesystem::path& path) {
    m_path = path;
    m_narcIndex = -1; // Reset NARC index since we're saving to a new file
    return save();
}

void EditorInstance::saveTo(const std::filesystem::path& path) {
    m_archive.save(path);
}

void EditorInstance::saveBackup() {
    const auto backupPath = getBackupPath();
    const auto backupDir = backupPath.parent_path();
    if (!std::filesystem::exists(backupDir)) {
        std::filesystem::create_directories(backupDir);
    }

    spdlog::info("Saving backup of {} to {}", getName(), backupPath.string());

    m_archive.save(backupPath);
    m_lastBackupTime = Clock::now();
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
    if (m_narcIndex != std::numeric_limits<size_t>::max()) {
        return m_narcMemberName;
    }

    if (m_path.empty()) {
        return "Untitled-" + std::to_string(m_uniqueID & 0xFF) + ".spa";
    }

    if (!g_projectManager->hasProject()) {
        return m_path;
    }

    return std::filesystem::relative(m_path, g_projectManager->getProjectPath());
}

std::filesystem::path EditorInstance::getBackupPath() const {
    const auto backupDir = g_application->getTempPath() / "backups";
    return backupDir / fmt::format("{:x}~{}", m_uniqueID & 0xFFFFFFFF, getName());
}

std::string EditorInstance::getName() const {
    if (m_narcIndex != std::numeric_limits<size_t>::max()) {
        return m_narcMemberName;
    }

    if (m_path.empty()) {
        return "Untitled-" + std::to_string(m_uniqueID & 0xFF);
    }

    return m_path.filename().string();
}

void EditorInstance::openTempTexture(const std::filesystem::path& path, size_t destIndex) {
    constexpr auto isPowerOf2 = [](s32 value) {
        return (value & (value - 1)) == 0;
    };

    if (destIndex != -1 && destIndex >= m_archive.getTextures().size()) {
        spdlog::error("Invalid destination index for temp texture: {}", destIndex);
        return;
    }

    std::ifstream file(path, std::ios::binary | std::ios::in);
    if (!file) {
        spdlog::error("Failed to open file: {}", path.string());
        return;
    }

    const std::vector<u8> fileData{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    if (fileData.empty()) {
        spdlog::error("File is empty: {}", path.string());
        return;
    }

    spng_ctx* ctx = spng_ctx_new(0);
    if (!ctx) {
        spdlog::error("Failed to create SPNG context");
        return;
    }

    spng_set_crc_action(ctx, SPNG_CRC_USE, SPNG_CRC_USE);

    constexpr auto limit = 1024 * 1024 * 64U; // 64 MB
    spng_set_chunk_limits(ctx, limit, limit);

    int ret;
    if ((ret = spng_set_png_buffer(ctx, fileData.data(), fileData.size())) != 0) {
        spdlog::error("Failed to set PNG buffer: {}", spng_strerror(ret));
        spng_ctx_free(ctx);
        return;
    }

    spng_ihdr ihdr;
    if ((ret = spng_get_ihdr(ctx, &ihdr)) != 0) {
        spdlog::error("Failed to get PNG header: {}", spng_strerror(ret));
        spng_ctx_free(ctx);
        return;
    }

    const auto tempTex = new TempTexture;
    m_tempTexture = tempTex;

    tempTex->path = path.string();
    tempTex->data = stbi_load_from_memory(fileData.data(), (int)fileData.size(), &tempTex->width, &tempTex->height, &tempTex->channels, 4);
    if (!tempTex->data) {
        delete m_tempTexture;
        m_tempTexture = nullptr;
        spng_ctx_free(ctx);
        return;
    }

    tempTex->texture = std::make_unique<GLTexture>(tempTex->width, tempTex->height);
    tempTex->quantized = new u8[(size_t)tempTex->width * tempTex->height * 4];

    // If we load an indexed PNG, we can go a simple path and don't need to quantize it.
    if (ihdr.color_type == SPNG_COLOR_TYPE_INDEXED && ihdr.bit_depth <= 8) {
        spng_plte palette;
        spng_get_plte(ctx, &palette);

        TextureFormat format;
        if (palette.n_entries <= 4) {
            format = TextureFormat::Palette4;
        } else if (palette.n_entries <= 16) {
            format = TextureFormat::Palette16;
        } else if (palette.n_entries <= 256) {
            format = TextureFormat::Palette256;
        } else {
            spdlog::error("Unsupported indexed PNG bit depth: {}", ihdr.bit_depth);
            format = TextureFormat::Palette256;
        }

        std::memcpy(tempTex->quantized, tempTex->data, (size_t)tempTex->width * tempTex->height * 4);
        tempTex->preference = TextureConversionPreference::ColorDepth;
        tempTex->spec = {
            .color0Transparent = true,
            .requiresColorCompression = false,
            .requiresAlphaCompression = false,
            .format = format,
            .uniqueColors = {},
            .uniqueAlphas = {},
            .flags = TextureAttributes::None,
        };
        tempTex->suggestedFormat = format;
        tempTex->suggestedFormatUncompressed = true;
    } else {
        tempTex->preference = TextureConversionPreference::ColorDepth;
        tempTex->spec = SPLTexture::suggestSpecification(tempTex->width, tempTex->height, tempTex->channels, tempTex->data, tempTex->preference);
        tempTex->suggestedFormat = tempTex->spec.format;

        if (tempTex->spec.requiresColorCompression || tempTex->spec.requiresAlphaCompression) {
            tempTex->suggestedFormatUncompressed = false;
            quantizeTexture(tempTex->data, tempTex->width, tempTex->height, tempTex->spec, tempTex->quantized);
        } else {
            tempTex->suggestedFormatUncompressed = true;
            std::memcpy(tempTex->quantized, tempTex->data, (size_t)tempTex->width * tempTex->height * 4);
        }
    }

    tempTex->texture->update(tempTex->quantized);

    tempTex->isValidSize = true;

    if (tempTex->width > 1024 || tempTex->height > 1024) {
        tempTex->isValidSize = false;
    }

    if (!isPowerOf2(tempTex->width) || !isPowerOf2(tempTex->height)) {
        tempTex->isValidSize = false;
    }

    tempTex->destIndex = destIndex;

    spng_ctx_free(ctx);
}

void EditorInstance::discardTempTexture() {
    m_discardTempTexture = true;
    spdlog::info("Discarding temp texture");
}

void EditorInstance::destroyTempTexture() {
    stbi_image_free(m_tempTexture->data);
    delete[] m_tempTexture->quantized;
    delete m_tempTexture;
    m_tempTexture = nullptr;
    m_discardTempTexture = false;
}

void EditorInstance::importTempTexture() {
    if (!m_tempTexture) {
        return;
    }

    auto& archive = m_archive;
    auto& textures = archive.getTextures();
    auto& texture = m_tempTexture->destIndex != -1 ? textures[m_tempTexture->destIndex] : textures.emplace_back();
    texture.glTexture = std::move(m_tempTexture->texture);
    texture.width = m_tempTexture->width;
    texture.height = m_tempTexture->height;
    texture.param = {
        .format = m_tempTexture->spec.format,
        .s = 1,
        .t = 1,
        .repeat = TextureRepeat::None,
        .flip = TextureFlip::None,
        .palColor0Transparent = false,
        .useSharedTexture = false,
        .sharedTexID = 0xFF
    };

    // -3 because s/t are enums starting at GX_TEXSIZE_8 = 0
    texture.param.s = (u8)glm::log2(texture.width) - 3;
    texture.param.t = (u8)glm::log2(texture.height) - 3;

    auto& textureData = archive.getTextureData().emplace_back();
    auto& paletteData = archive.getPaletteData().emplace_back();

    palettizeTexture(
        m_tempTexture->quantized,
        m_tempTexture->width,
        m_tempTexture->height,
        m_tempTexture->spec,
        textureData,
        paletteData
    );

    texture.textureData = textureData;
    texture.paletteData = paletteData;

    switch (texture.param.format) {
    case TextureFormat::Palette4: [[fallthrough]];
    case TextureFormat::Palette16: [[fallthrough]];
    case TextureFormat::Palette256:
        // Check if the first color in the palette is transparent
        texture.param.palColor0Transparent = ((GXRgba*)paletteData.data())->a == 0;
        break;
    default:
        break;
    }

    discardTempTexture();

    m_particleSystem.getRenderer().setTextures(textures);
}

void EditorInstance::ensureValidSelection() {
    if (m_selectedResource != INVALID_RESOURCE && m_selectedResource >= m_archive.getResourceCount()) {
        notifyResourceChanged(INVALID_RESOURCE);
    }
}

bool EditorInstance::palettizeTexture(const u8* data, s32 width, s32 height, const TextureImportSpecification& spec, std::vector<u8>& outData, std::vector<u8>& outPalette) {
    return SPLTexture::convertFromRGBA8888(data, width, height, spec.format, outData, outPalette);
}

void EditorInstance::quantizeTexture(const u8* data, s32 width, s32 height, const TextureImportSpecification& spec, u8* out) {
    liq_attr* attr = liq_attr_create();
    liq_set_max_colors(attr, spec.getMaxColors());
    
    liq_image* image = liq_image_create_rgba(attr, data, width, height, 0);
    
    liq_error err;
    liq_result* result = nullptr;
    if ((err = liq_image_quantize(image, attr, &result)) != LIQ_OK) {
        spdlog::error("Failed to quantize image: {}", (int)err);
        return;
    }

    std::vector<u8> quantized(width * height);
    if ((err = liq_write_remapped_image(result, image, quantized.data(), quantized.size())) != LIQ_OK) {
        spdlog::error("Failed to write quantized image: {}", (int)err);
        return;
    }

    const auto palette = liq_get_palette(result);
    if (palette->count > spec.getMaxColors()) {
        spdlog::error("Too many colors in resulting palette");
        return;
    }

    auto palcopy = *palette;
    std::span<liq_color> colorspan(palcopy.entries, palcopy.count);

    // Further quantize alpha values if necessary
    if (spec.requiresAlphaCompression) {
        const auto maxAlphas = spec.getMaxAlphas();
        const auto [min, max] = spec.getAlphaRange();

        const auto transparent = std::ranges::find_if(colorspan, [](liq_color color) { return color.a == 0; });

        switch (spec.format) {
        case TextureFormat::None: break;
        case TextureFormat::A3I5:
        case TextureFormat::A5I3:
            std::ranges::for_each(colorspan, [min, max](auto& color) {
                const auto mapped = (u8)(((float)color.a / 255.0f) * (max - min)) + min;
                color.a = (u8)(((float)(mapped - min) / (max - min)) * 255.0f);
            });
            break;

        case TextureFormat::Palette4:
        case TextureFormat::Palette16:
        case TextureFormat::Palette256:
        case TextureFormat::Direct:
            if (spec.needsAlpha() || spec.format == TextureFormat::Direct) {
                std::ranges::for_each(colorspan, [](auto& color) {
                    if (color.a < 128) {
                        color.a = 0;
                    } else {
                        color.a = 255;
                    }
                });
            }
            break;
        default: break;
        }
    }

    const auto colors = (u32*)palcopy.entries;
    std::span<u32> remapped((u32*)out, width * height);
    for (auto [index, pixel] : std::views::zip(quantized, remapped)) {
        pixel = colors[index];
    }

    liq_result_destroy(result);
    liq_image_destroy(image);
    liq_attr_destroy(attr);
}

void EditorInstance::createRenderers() {
    m_debugRenderer = std::make_unique<DebugRenderer>(1000);
    m_collisionGridRenderer = std::make_shared<GridRenderer>(glm::ivec2(10, 10), glm::vec2(1.0f, 1.0f));
}

void EditorInstance::renderHeaderEditor(SPLResourceHeader &header)
{
    auto& flags = header.flags;
    auto& misc = header.misc;
    constexpr f32 frameTime = 1.0f / (f32)SPLArchive::SPL_FRAMES_PER_SECOND;

    auto open = ImGui::TreeNodeEx("##emitterSettings", ImGuiTreeNodeFlags_SpanAvailWidth);
    ImGui::SameLine();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 5);
    ImGui::SeparatorText("Emitter Settings");
    if (open) {
        if (ImGui::BeginCombo("Emission Type", getEmissionType(flags.emissionType))) {
            for (const auto [val, name] : detail::g_emissionTypeNames) {
                if (NOTIFY(ImGui::Selectable(name, flags.emissionType == val))) {
                    flags.emissionType = val;
                }
            }

            ImGui::EndCombo();
        }
        HELP(emissionType);

        if (ImGui::BeginCombo("Emission Axis", getEmissionAxis(flags.emissionAxis))) {
            for (const auto [val, name] : detail::g_emissionAxisNames) {
                if (NOTIFY(ImGui::Selectable(name, flags.emissionAxis == val))) {
                    flags.emissionAxis = val;
                }
            }

            ImGui::EndCombo();
        }
        HELP(emissionAxis);

        NOTIFY(ImGui::Checkbox("Self Maintaining", &flags.selfMaintaining));
        HELP(selfMaintaining);

        NOTIFY(ImGui::Checkbox("Draw Children First", &flags.drawChildrenFirst));
        HELP(drawChildrenFirst);

        NOTIFY(ImGui::Checkbox("Hide Parent", &flags.hideParent));
        HELP(hideParent);

        NOTIFY(ImGui::Checkbox("Use View Space", &flags.useViewSpace));
        HELP(useViewSpace);

        NOTIFY(ImGui::Checkbox("Has Fixed Polygon ID", &flags.hasFixedPolygonID));
        HELP(hasFixedPolygonID);

        NOTIFY(ImGui::Checkbox("Child Fixed Polygon ID", &flags.childHasFixedPolygonID));
        HELP(childHasFixedPolygonID);

        NOTIFY(ImGui::DragFloat3("Emitter Base Pos", glm::value_ptr(header.emitterBasePos), 0.01f));
        HELP(emitterBasePos);

        NOTIFY(ImGui::SliderFloat("Lifetime", &header.emitterLifeTime, frameTime, 60, "%.4fs", ImGuiSliderFlags_Logarithmic));
        HELP(emitterLifeTime);

        NOTIFY(ImGui::DragInt("Emission Amount", (int*)&header.emissionCount, 1, 0, 20));
        HELP(emissionCount);

        // The in-file value is 8 bits, 255 = 255 frames of delay, 255f / 30fps = 8.5s
        NOTIFY(ImGui::SliderFloat("Emission Interval", &misc.emissionInterval, frameTime, 8.5f, "%.4fs"));
        HELP(emissionInterval);

        u32 emissions = (u32)glm::ceil(header.emitterLifeTime / misc.emissionInterval);
        const u32 maxEmissions = (u32)(header.emitterLifeTime / frameTime);
        if (NOTIFY(ImGui::SliderInt("Emissions", (int*)&emissions, 1, maxEmissions))) {
            misc.emissionInterval = header.emitterLifeTime / (f32)emissions;
        }
        HELP(emissions);

        NOTIFY(ImGui::SliderFloat("Start Delay", &header.startDelay, 0, header.emitterLifeTime, "%.2fs"));
        HELP(startDelay);

        NOTIFY(ImGui::SliderFloat("Radius", &header.radius, 0.01f, 20.0f, "%.3f", ImGuiSliderFlags_Logarithmic));
        HELP(radius);

        NOTIFY(ImGui::SliderFloat("Length", &header.length, 0.01f, 20.0f, "%.3f", ImGuiSliderFlags_Logarithmic));
        HELP(length);

        NOTIFY(ImGui::DragFloat3("Axis", glm::value_ptr(header.axis), 0.02f));
        HELP(axis);

        ImGui::TreePop();
    }

    open = ImGui::TreeNodeEx("##particleSettings", ImGuiTreeNodeFlags_SpanAvailWidth);
    ImGui::SameLine();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 5);
    ImGui::SeparatorText("Particle Settings");
    if (open) {
        if (ImGui::BeginCombo("Draw Type", getDrawType(flags.drawType))) {
            for (const auto [val, name] : detail::g_drawTypeNames) {
                if (NOTIFY(ImGui::Selectable(name, flags.drawType == val))) {
                    flags.drawType = val;
                }
            }

            ImGui::EndCombo();
        }
        HELP(drawType);

        const auto& textures = m_archive.getTextures();
        if (ImGui::ImageButton("##tex", (ImTextureID)textures[header.misc.textureIndex].glTexture->getHandle(), {32, 32})) {
            ImGui::OpenPopup(ImGui::GetID("##texturePicker"));
        }
        ImGui::SameLine();
        ImGui::Text("Texture");
        HELP(texture);

        NOTIFY(ImGui::Checkbox("Rotate", &flags.hasRotation));
        HELP(hasRotation);

        NOTIFY(ImGui::Checkbox("Random Init Angle", &flags.randomInitAngle));
        HELP(randomInitAngle);

        NOTIFY(ImGui::Checkbox("Follow Emitter", &flags.followEmitter));
        HELP(followEmitter);

        if (ImGui::BeginCombo("Polygon Rotation Axis", getPolygonRotAxis(flags.polygonRotAxis))) {
            for (const auto [val, name] : detail::g_polygonRotAxisNames) {
                if (NOTIFY(ImGui::Selectable(name, flags.polygonRotAxis == val))) {
                    flags.polygonRotAxis = val;
                }
            }

            ImGui::EndCombo();
        }
        HELP(polygonRotAxis);

        ImGui::TextUnformatted("Polygon Reference Plane");
        HELP(polygonReferencePlane);
        ImGui::Indent();
        NOTIFY(ImGui::RadioButton("XY", &flags.polygonReferencePlane, 0));
        NOTIFY(ImGui::RadioButton("XZ", &flags.polygonReferencePlane, 1));
        ImGui::Unindent();

        NOTIFY(ImGui::ColorEdit3("Color", glm::value_ptr(header.color)));
        HELP(color);

        NOTIFY(ImGui::SliderFloat("Base Scale", &header.baseScale, 0.01f, 10.0f, "%.3f", ImGuiSliderFlags_Logarithmic));
        HELP(baseScale);

        NOTIFY(ImGui::SliderAngle("Init Angle", &header.initAngle, 0));
        HELP(initAngle);

        NOTIFY(ImGui::SliderFloat("Base Alpha", &misc.baseAlpha, 0, 1));
        HELP(baseAlpha);

        NOTIFY(ImGui::SliderFloat("Lifetime", &header.particleLifeTime, frameTime, 60, "%.4fs", ImGuiSliderFlags_Logarithmic));
        HELP(particleLifeTime);

        NOTIFY(ImGui::DragFloat("Aspect Ratio", &header.aspectRatio, 0.05f));
        HELP(aspectRatio);

        NOTIFY(ImGui::DragFloat("Init Velocity Pos Amplifier", &header.initVelPosAmplifier, 0.1f, -10, 10, "%.3f", ImGuiSliderFlags_Logarithmic));
        HELP(initVelPosAmplifier);

        NOTIFY(ImGui::DragFloat("Init Velocity Axis Amplifier", &header.initVelAxisAmplifier, 0.1f, -10, 10, "%.3f", ImGuiSliderFlags_Logarithmic));
        HELP(initVelAxisAmplifier);

        ImGui::TextUnformatted("Rotation Speed");
        HELP(rotationSpeed);
        ImGui::Indent();
        NOTIFY(ImGui::SliderAngle("Min", &header.minRotation, 0, header.maxRotation * 180 / glm::pi<f32>()));
        NOTIFY(ImGui::SliderAngle("Max", &header.maxRotation, header.minRotation * 180 / glm::pi<f32>(), 360));
        ImGui::Unindent();

        ImGui::TextUnformatted("Variance");
        HELP(variance);
        ImGui::Indent();
        NOTIFY(ImGui::SliderFloat("Base Scale##variance", &header.variance.baseScale, 0, 1));
        NOTIFY(ImGui::SliderFloat("Particle Lifetime##variance", &header.variance.lifeTime, 0, 1));
        NOTIFY(ImGui::SliderFloat("Init Velocity##variance", &header.variance.initVel, 0, 1));
        ImGui::Unindent();

        NOTIFY(ImGui::SliderFloat("Air Resistance", &misc.airResistance, 0.75f, 1.25f));
        HELP(airResistance);

        NOTIFY(ImGui::SliderFloat("Loop Time", &misc.loopTime, frameTime, 8.5f, "%.4fs"));
        HELP(loopTime);

        u32 loops = (u32)glm::ceil(header.particleLifeTime / misc.loopTime);
        const u32 maxLoops = (u32)(header.particleLifeTime / frameTime);
        if (NOTIFY(ImGui::SliderInt("Loops", (int*)&loops, 1, maxLoops))) {
            misc.loopTime = header.particleLifeTime / (f32)loops;
        }
        HELP(loops);

        NOTIFY(ImGui::Checkbox("Randomize Looped Anim", &flags.randomizeLoopedAnim));
        HELP(randomizeLoopedAnim);

        NOTIFY(ImGui::SliderFloat("DBB Scale", &misc.dbbScale, -8.0f, 7.0f));
        HELP(dbbScale);

        if (ImGui::BeginCombo("Scale Anim Axis", getScaleAnimDir(misc.scaleAnimDir))) {
            for (const auto [val, name] : detail::g_scaleAnimDirNames) {
                if (NOTIFY(ImGui::Selectable(name, misc.scaleAnimDir == val))) {
                    misc.scaleAnimDir = val;
                }
            }

            ImGui::EndCombo();
        }
        HELP(scaleAnimDir);

        ImGui::TextUnformatted("Texture Tiling");
        HELP(textureTiling);
        ImGui::Indent();
        int tileCount = 1 << misc.textureTileCountS;
        NOTIFY(ImGui::SliderInt("S", &tileCount, 1, 8));
        misc.textureTileCountS = glm::log2(tileCount);

        tileCount = 1 << misc.textureTileCountT;
        NOTIFY(ImGui::SliderInt("T", &tileCount, 1, 8));
        misc.textureTileCountT = glm::log2(tileCount);
        ImGui::Unindent();

        NOTIFY(ImGui::Checkbox("DPol Face Emitter", &misc.dpolFaceEmitter));
        HELP(dpolFaceEmitter);

        NOTIFY(ImGui::Checkbox("Flip X", &misc.flipTextureS));
        HELP(flipTextureX);

        NOTIFY(ImGui::Checkbox("Flip Y", &misc.flipTextureT));
        HELP(flipTextureY);

        ImGui::TextUnformatted("Polygon Offset");
        HELP(polygonOffset);
        ImGui::Indent();
        NOTIFY(ImGui::SliderFloat("X", &header.polygonX, -2, 2));
        NOTIFY(ImGui::SliderFloat("Y", &header.polygonY, -2, 2));
        ImGui::Unindent();

        if (ImGui::BeginPopup("##texturePicker")) {
            for (int i = 0; i < textures.size(); ++i) {
                ImGui::PushID(i);

                const auto& texture = textures[i];
                if (NOTIFY(ImGui::ImageButton("##tex", (ImTextureID)texture.glTexture->getHandle(), {32, 32}))) {
                    header.misc.textureIndex = i;
                    ImGui::CloseCurrentPopup();
                }

                if (i % 4 != 3) {
                    ImGui::SameLine();
                }

                ImGui::PopID();
            }

            ImGui::EndPopup();
        }

        ImGui::TreePop();
    }
}

void EditorInstance::renderBehaviorEditor(SPLResource& res) {
    std::vector<std::shared_ptr<SPLBehavior>> toRemove;

    if (ImGui::IconButton(ICON_FA_CIRCLE_PLUS, "Add Behavior...", AppColors::Turquoise)) {
        ImGui::OpenPopup("##addBehavior");
    }

    if (ImGui::BeginPopup("##addBehavior")) {
        if (NOTIFY(ImGui::MenuItem("Gravity", nullptr, false, !res.header.flags.hasGravityBehavior))) {
            res.behaviors.push_back(std::make_shared<SPLGravityBehavior>(glm::vec3(0, 0, 0)));
            res.header.addBehavior(SPLBehaviorType::Gravity);
        }

        if (NOTIFY(ImGui::MenuItem("Random", nullptr, false, !res.header.flags.hasRandomBehavior))) {
            res.behaviors.push_back(std::make_shared<SPLRandomBehavior>(glm::vec3(0, 0, 0), 1.0f));
            res.header.addBehavior(SPLBehaviorType::Random);
        }

        if (NOTIFY(ImGui::MenuItem("Magnet", nullptr, false, !res.header.flags.hasMagnetBehavior))) {
            res.behaviors.push_back(std::make_shared<SPLMagnetBehavior>(glm::vec3(0, 0, 0), 0.0f));
            res.header.addBehavior(SPLBehaviorType::Magnet);
        }

        if (NOTIFY(ImGui::MenuItem("Spin", nullptr, false, !res.header.flags.hasSpinBehavior))) {
            res.behaviors.push_back(std::make_shared<SPLSpinBehavior>(0.0f, SPLSpinAxis::Y));
            res.header.addBehavior(SPLBehaviorType::Spin);
        }

        if (NOTIFY(ImGui::MenuItem("Collision Plane", nullptr, false, !res.header.flags.hasCollisionPlaneBehavior))) {
            res.behaviors.push_back(std::make_shared<SPLCollisionPlaneBehavior>(0.0f, 0.0f, SPLCollisionType::Bounce));
            res.header.addBehavior(SPLBehaviorType::CollisionPlane);
        }

        if (NOTIFY(ImGui::MenuItem("Convergence", nullptr, false, !res.header.flags.hasConvergenceBehavior))) {
            res.behaviors.push_back(std::make_shared<SPLConvergenceBehavior>(glm::vec3(0, 0, 0), 0.0f));
            res.header.addBehavior(SPLBehaviorType::Convergence);
        }

        ImGui::EndPopup();
    }

    for (const auto& bhv : res.behaviors) {
        ImGui::PushID(bhv.get());

        bool context = false;
        switch (bhv->type) {
        case SPLBehaviorType::Gravity:
            context = renderGravityBehaviorEditor(std::static_pointer_cast<SPLGravityBehavior>(bhv));
            break;
        case SPLBehaviorType::Random:
            context = renderRandomBehaviorEditor(std::static_pointer_cast<SPLRandomBehavior>(bhv));
            break;
        case SPLBehaviorType::Magnet:
            context = renderMagnetBehaviorEditor(std::static_pointer_cast<SPLMagnetBehavior>(bhv));
            break;
        case SPLBehaviorType::Spin:
            context = renderSpinBehaviorEditor(std::static_pointer_cast<SPLSpinBehavior>(bhv));
            break;
        case SPLBehaviorType::CollisionPlane:
            context = renderCollisionPlaneBehaviorEditor(std::static_pointer_cast<SPLCollisionPlaneBehavior>(bhv));
            break;
        case SPLBehaviorType::Convergence:
            context = renderConvergenceBehaviorEditor(std::static_pointer_cast<SPLConvergenceBehavior>(bhv));
            break;
        }

        if (context) {
            if (NOTIFY(ImGui::MenuItem("Delete"))) {
                toRemove.push_back(bhv);
            }

            ImGui::EndPopup();
        }

        ImGui::PopID();
    }

    for (const auto& r : toRemove) {
        std::erase(res.behaviors, r);
        res.header.removeBehavior(r->type);
    }
}

bool EditorInstance::renderGravityBehaviorEditor(const std::shared_ptr<SPLGravityBehavior>& gravity) {
    static bool hovered = false;
    ImGui::BeginHoverBorderChild("##gravityEditor", hovered, s_hoverAccentColor);
    ImGui::TextUnformatted("Gravity");

    NOTIFY(ImGui::DragFloat3("Magnitude", glm::value_ptr(gravity->magnitude), 0.001f));

    ImGui::EndChild();

    hovered = ImGui::IsItemHovered();
    return ImGui::BeginPopupContextItem("##behaviorContext");
}

bool EditorInstance::renderRandomBehaviorEditor(const std::shared_ptr<SPLRandomBehavior>& random) {
    static bool hovered = false;
    ImGui::BeginHoverBorderChild("##randomEditor", hovered, s_hoverAccentColor);
    ImGui::TextUnformatted("Random");

    NOTIFY(ImGui::DragFloat3("Magnitude", glm::value_ptr(random->magnitude), 0.001f));
    NOTIFY(ImGui::SliderFloat("Apply Interval", &random->applyInterval, 0, 5, "%.3fs", ImGuiSliderFlags_Logarithmic));

    ImGui::EndChild();

    hovered = ImGui::IsItemHovered();
    return ImGui::BeginPopupContextItem("##behaviorContext");
}

bool EditorInstance::renderMagnetBehaviorEditor(const std::shared_ptr<SPLMagnetBehavior>& magnet) {
    static bool hovered = false;
    ImGui::BeginHoverBorderChild("##magnetEditor", hovered, s_hoverAccentColor);
    ImGui::TextUnformatted("Magnet");

    NOTIFY(ImGui::DragFloat3("Target", glm::value_ptr(magnet->target), 0.05f, -5.0f, 5.0f));
    NOTIFY(ImGui::SliderFloat("Force", &magnet->force, 0, 5, "%.3f", ImGuiSliderFlags_Logarithmic));

    ImGui::EndChild();

    hovered = ImGui::IsItemHovered();
    return ImGui::BeginPopupContextItem("##behaviorContext");
}

bool EditorInstance::renderSpinBehaviorEditor(const std::shared_ptr<SPLSpinBehavior>& spin) {
    static bool hovered = false;
    ImGui::BeginHoverBorderChild("##spinEditor", hovered, s_hoverAccentColor);
    ImGui::TextUnformatted("Spin");

    NOTIFY(ImGui::SliderAngle("Angle", &spin->angle, -180.0f, 180.0f));
    ImGui::TextUnformatted("Axis");
    ImGui::Indent();
    NOTIFY(ImGui::RadioButton("X", (int*)&spin->axis, 0));
    NOTIFY(ImGui::RadioButton("Y", (int*)&spin->axis, 1));
    NOTIFY(ImGui::RadioButton("Z", (int*)&spin->axis, 2));
    ImGui::Unindent();

    ImGui::EndChild();

    hovered = ImGui::IsItemHovered();
    return ImGui::BeginPopupContextItem("##behaviorContext");
}

bool EditorInstance::renderCollisionPlaneBehaviorEditor(const std::shared_ptr<SPLCollisionPlaneBehavior>& collisionPlane) {
    static bool hovered = false;
    ImGui::BeginHoverBorderChild("##collisionPlaneEditor", hovered, s_hoverAccentColor);
    ImGui::TextUnformatted("Collision Plane");

    NOTIFY(ImGui::DragFloat("Height", &collisionPlane->y, 0.05f));
    NOTIFY(ImGui::SliderFloat("Elasticity", &collisionPlane->elasticity, 0, 2, "%.3f", ImGuiSliderFlags_Logarithmic));
    ImGui::TextUnformatted("Collision Type");
    ImGui::Indent();
    NOTIFY(ImGui::RadioButton("Kill", (int*)&collisionPlane->collisionType, 0));
    NOTIFY(ImGui::RadioButton("Bounce", (int*)&collisionPlane->collisionType, 1));
    ImGui::Unindent();

    ImGui::EndChild();

    hovered = ImGui::IsItemHovered();
    return ImGui::BeginPopupContextItem("##behaviorContext");
}

bool EditorInstance::renderConvergenceBehaviorEditor(const std::shared_ptr<SPLConvergenceBehavior>& convergence) {
    static bool hovered = false;
    ImGui::BeginHoverBorderChild("##convergenceEditor", hovered, s_hoverAccentColor);
    ImGui::TextUnformatted("Convergence");

    NOTIFY(ImGui::DragFloat3("Target", glm::value_ptr(convergence->target), 0.05f, -5.0f, 5.0f));
    NOTIFY(ImGui::SliderFloat("Force", &convergence->force, -5, 5, "%.3f", ImGuiSliderFlags_Logarithmic));

    ImGui::EndChild();

    hovered = ImGui::IsItemHovered();
    return ImGui::BeginPopupContextItem("##behaviorContext");
}

void EditorInstance::renderAnimationEditor(SPLResource& res) {
    if (ImGui::IconButton(ICON_FA_CIRCLE_PLUS , "Add Animation...", AppColors::Turquoise)) {
        ImGui::OpenPopup("##addAnimation");
    }

    if (ImGui::BeginPopup("##addAnimation")) {
        if (NOTIFY(ImGui::MenuItem("Scale", nullptr, false, !res.header.flags.hasScaleAnim))) {
            res.addScaleAnim(SPLScaleAnim::createDefault());
            ImGui::CloseCurrentPopup();
        }

        if (NOTIFY(ImGui::MenuItem("Color", nullptr, false, !res.header.flags.hasColorAnim))) {
            res.addColorAnim(SPLColorAnim::createDefault());
            ImGui::CloseCurrentPopup();
        }

        if (NOTIFY(ImGui::MenuItem("Alpha", nullptr, false, !res.header.flags.hasAlphaAnim))) {
            res.addAlphaAnim(SPLAlphaAnim::createDefault());
            ImGui::CloseCurrentPopup();
        }

        if (NOTIFY(ImGui::MenuItem("Texture", nullptr, false, !res.header.flags.hasTexAnim))) {
            res.addTexAnim(SPLTexAnim::createDefault());
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    if (res.scaleAnim) {
        if (renderScaleAnimEditor(*res.scaleAnim)) {
            res.removeScaleAnim();
        }
    }

    if (res.colorAnim) {
        if (renderColorAnimEditor(res, *res.colorAnim)) {
            res.removeColorAnim();
        }
    }

    if (res.alphaAnim) {
        if (renderAlphaAnimEditor(*res.alphaAnim)) {
            res.removeAlphaAnim();
        }
    }

    if (res.texAnim) {
        if (renderTexAnimEditor(*res.texAnim)) {
            res.removeTexAnim();
        }
    }
}

bool EditorInstance::renderScaleAnimEditor(SPLScaleAnim& res) {
    static bool hovered = false;

    if (!ImGui::CollapsingHeader("Scale Animation")) {
        return false;
    }

    ImGui::BeginHoverBorderChild("##scaleAnimEditor", hovered, s_hoverAccentColor);

    NOTIFY(ImGui::SliderFloat("Start Scale", &res.start, 0.01f, 10.0f, "%.3f", ImGuiSliderFlags_Logarithmic));
    NOTIFY(ImGui::SliderFloat("Mid Scale", &res.mid, 0.01f, 10.0f, "%.3f", ImGuiSliderFlags_Logarithmic));
    NOTIFY(ImGui::SliderFloat("End Scale", &res.end, 0.01f, 10.0f, "%.3f", ImGuiSliderFlags_Logarithmic));
    constexpr u8 min = 0;
    constexpr u8 max = 255;
    NOTIFY(ImGui::SliderScalar("In", ImGuiDataType_U8, &res.curve.in, &min, &max, "%u"));
    NOTIFY(ImGui::SliderScalar("Out", ImGuiDataType_U8, &res.curve.out, &min, &max, "%u"));
    NOTIFY(ImGui::Checkbox("Loop", &res.flags.loop));

    res.plot(m_xAnimBuffer, m_yAnimBuffer1);
    if (ImPlot::BeginPlot("##scaleAnimPlot", {-1, 0}, ImPlotFlags_CanvasOnly)) {
        ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
        ImPlot::PlotLine("Scale", m_xAnimBuffer.data(), m_yAnimBuffer1.data(), m_xAnimBuffer.size());
        ImPlot::EndPlot();
    }

    ImGui::EndChild();

    hovered = ImGui::IsItemHovered();

    bool result = false;
    if (ImGui::BeginPopupContextItem("##scaleAnimContext")) {
        if (ImGui::MenuItem("Delete")) {
            ImGui::CloseCurrentPopup();
            result = true;
        }

        ImGui::EndPopup();
    }

    return result;
}

bool EditorInstance::renderColorAnimEditor(const SPLResource& mainRes, SPLColorAnim& res) {
    static bool hovered = false;

    if (!ImGui::CollapsingHeader("Color Animation")) {
        return false;
    }

    ImGui::BeginHoverBorderChild("##colorAnimEditor", hovered, s_hoverAccentColor);

    NOTIFY(ImGui::ColorEdit3("Start Color", glm::value_ptr(res.start)));
    NOTIFY(ImGui::ColorEdit3("End Color", glm::value_ptr(res.end)));
    constexpr u8 min = 0;
    constexpr u8 max = 255;
    NOTIFY(ImGui::SliderScalar("In", ImGuiDataType_U8, &res.curve.in, &min, &max, "%u"));
    NOTIFY(ImGui::SliderScalar("Peak", ImGuiDataType_U8, &res.curve.peak, &min, &max, "%u"));
    NOTIFY(ImGui::SliderScalar("Out", ImGuiDataType_U8, &res.curve.out, &min, &max, "%u"));

    NOTIFY(ImGui::Checkbox("Loop", &res.flags.loop));
    NOTIFY(ImGui::Checkbox("Interpolate", &res.flags.interpolate));
    NOTIFY(ImGui::Checkbox("Random Start Color", &res.flags.randomStartColor));
    
    const auto drawList = ImGui::GetWindowDrawList();
    const auto startPos = ImGui::GetCursorScreenPos();
    const auto maxWidth = ImGui::GetContentRegionAvail().x;
    auto pos = startPos;
    
    constexpr auto toImColor = [](const glm::vec3& color) {
        return IM_COL32(
            (u8)(color.r * 255),
            (u8)(color.g * 255),
            (u8)(color.b * 255),
            255
        );
    };

    const auto in = res.curve.getIn();
    const auto peak = res.curve.getPeak();
    const auto out = res.curve.getOut();

    const auto startCol = toImColor(res.start);
    const auto peakCol = toImColor(mainRes.header.color);
    const auto endCol = toImColor(res.end);

    if (in > 0.0f) {
        const auto endPos = ImVec2(pos.x + in * maxWidth, pos.y + 20);
        drawList->AddRectFilled(pos, endPos, startCol);
        pos.x = endPos.x;
    }

    if (res.flags.interpolate) {
        const auto endPos = ImVec2(pos.x + (peak - in) * maxWidth, pos.y + 20);
        drawList->AddRectFilledMultiColor(
            pos,
            endPos,
            startCol,
            peakCol,
            peakCol,
            startCol
        );
        pos.x = endPos.x;
    } else {
        const auto endPos = ImVec2(pos.x + (peak - in) * maxWidth, pos.y + 20);
        drawList->AddRectFilled(pos, endPos, toImColor(mainRes.header.color));
        pos.x = endPos.x;
    }

    if (res.flags.interpolate) {
        const auto endPos = ImVec2(pos.x + (out - peak) * maxWidth, pos.y + 20);
        drawList->AddRectFilledMultiColor(
            pos,
            endPos,
            peakCol,
            endCol,
            endCol,
            peakCol
        );
        pos.x = endPos.x;
    } else {
        const auto endPos = ImVec2(pos.x + (out - peak) * maxWidth, pos.y + 20);
        drawList->AddRectFilled(pos, endPos, endCol);
        pos.x = endPos.x;
    }

    if (out < 1.0f) {
        const auto endPos = ImVec2(pos.x + (1.0f - out) * maxWidth, pos.y + 20);
        drawList->AddRectFilled(pos, endPos, endCol);
    }

    ImGui::Dummy({ maxWidth, 20 });

    ImGui::EndChild();

    hovered = ImGui::IsItemHovered();

    bool result = false;
    if (ImGui::BeginPopupContextItem("##colorAnimContext")) {
        if (ImGui::MenuItem("Delete")) {
            ImGui::CloseCurrentPopup();
            result = true;
        }

        ImGui::EndPopup();
    }

    return result;
}

bool EditorInstance::renderAlphaAnimEditor(SPLAlphaAnim& res) {
    static bool hovered = false;

    if (!ImGui::CollapsingHeader("Alpha Animation")) {
        return false;
    }

    ImGui::BeginHoverBorderChild("##alphaAnimEditor", hovered, s_hoverAccentColor);

    NOTIFY(ImGui::SliderFloat("Start Alpha", &res.alpha.start, 0, 1));
    NOTIFY(ImGui::SliderFloat("Mid Alpha", &res.alpha.mid, 0, 1));
    NOTIFY(ImGui::SliderFloat("End Alpha", &res.alpha.end, 0, 1));

    res.alpha.start = (f32)((u8)(res.alpha.start * 31.0f)) / 31.0f;
    res.alpha.mid = (f32)((u8)(res.alpha.mid * 31.0f)) / 31.0f;
    res.alpha.end = (f32)((u8)(res.alpha.end * 31.0f)) / 31.0f;

    const auto minAlpha = std::min({ res.alpha.start, res.alpha.mid, res.alpha.end });
    const auto maxAlpha = std::max({ res.alpha.start, res.alpha.mid, res.alpha.end });

    constexpr u8 min = 0;
    constexpr u8 max = 255;
    NOTIFY(ImGui::SliderScalar("In", ImGuiDataType_U8, &res.curve.in, &min, &max, "%u"));
    NOTIFY(ImGui::SliderScalar("Out", ImGuiDataType_U8, &res.curve.out, &min, &max, "%u"));
    NOTIFY(ImGui::SliderFloat("Random Range", &res.flags.randomRange, 0, 1));
    NOTIFY(ImGui::Checkbox("Loop", &res.flags.loop));

    res.plotWith(m_xAnimBuffer, m_yAnimBuffer1, 0.0f);

    if (ImPlot::BeginPlot("##alphaAnimPlot", {-1, 0}, ImPlotFlags_CanvasOnly | ImPlotFlags_NoInputs)) {
        ImPlot::SetupAxisLimits(ImAxis_X1, 0, 1);
        ImPlot::SetupAxisLimits(ImAxis_Y1, minAlpha, maxAlpha);
        
        if (res.flags.randomRange > 0) {
            res.plotWith(m_xAnimBuffer, m_yAnimBuffer2, res.flags.randomRange, 255);
            ImPlot::PlotLine("Alpha Base", m_xAnimBuffer.data(), m_yAnimBuffer1.data(), m_xAnimBuffer.size());
            ImPlot::PlotShaded(
                "Alpha Variance",
                m_xAnimBuffer.data(),
                m_yAnimBuffer2.data(),
                m_yAnimBuffer1.data(),
                m_xAnimBuffer.size(),
                ImPlotSpec{ ImPlotProp_FillAlpha, 0.25f }
            );
        } else {
            ImPlot::PlotLine("Alpha", m_xAnimBuffer.data(), m_yAnimBuffer1.data(), m_xAnimBuffer.size());
        }

        ImPlot::EndPlot();
    }

    ImGui::EndChild();

    hovered = ImGui::IsItemHovered();

    bool result = false;
    if (ImGui::BeginPopupContextItem("##alphaAnimContext")) {
        if (ImGui::MenuItem("Delete")) {
            ImGui::CloseCurrentPopup();
            result = true;
        }

        ImGui::EndPopup();
    }

    return result;
}

bool EditorInstance::renderTexAnimEditor(SPLTexAnim& res) {
    static bool hovered = false;

    if (!ImGui::CollapsingHeader("Texture Animation")) {
        return false;
    }

    ImGui::BeginHoverBorderChild("##texAnimEditor", hovered, s_hoverAccentColor);

    NOTIFY(ImGui::SliderFloat("Step", &res.param.step, 0.01f, 1.0f));
    HELP(texAnimStep);

    NOTIFY(ImGui::Checkbox("Loop", &res.param.loop));
    HELP(texAnimLoop);

    NOTIFY(ImGui::Checkbox("Randomize Start", &res.param.randomizeInit));
    HELP(texAnimRandomizeInit);

    ImGui::SeparatorText("Textures");

    const auto& textures = m_archive.getTextures();
    const auto popupId = ImGui::GetID("##texAnimTexturePicker");
    bool buttonContextOpened = false;
    static size_t selectedTexture = 0;

    for (int i = 0; i < res.param.textureCount; i++) {
        ImGui::PushID(i);
        const auto& texture = textures[res.textures[i]];
        if (ImGui::ImageButton("##tex", texture.glTexture->getHandle(), { 32, 32 })) {
            ImGui::OpenPopup(popupId);
            selectedTexture = i;
        }
        if (ImGui::BeginPopupContextItem("##texContext")) {
            buttonContextOpened = true;
            ImGui::BeginDisabled(res.param.textureCount <= 1);

            if (NOTIFY(ImGui::MenuItem("Delete"))) {
                res.removeTexture(i);
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndDisabled();
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }

    if (res.param.textureCount < SPLTexAnim::MAX_TEXTURES) {
        if (ImGui::Button(ICON_FA_PLUS, { 32, 32 })) {
            res.addTexture();
        }
    }

    if (ImGui::BeginPopup("##texAnimTexturePicker")) {
        for (int i = 0; i < textures.size(); ++i) {
            ImGui::PushID(i);
            const auto& texture = textures[i];
            if (NOTIFY(ImGui::ImageButton("##tex", (ImTextureID)texture.glTexture->getHandle(), { 32, 32 }))) {
                res.textures[selectedTexture] = i;
                ImGui::CloseCurrentPopup();
            }

            if (i % 4 != 3) {
                ImGui::SameLine();
            }
            ImGui::PopID();
        }
        ImGui::EndPopup();
    }

    ImGui::EndChild();

    hovered = ImGui::IsItemHovered();
    bool result = false;

    if (!buttonContextOpened && ImGui::BeginPopupContextItem("##texAnimContext")) {
        if (ImGui::MenuItem("Delete")) {
            ImGui::CloseCurrentPopup();
            result = true;
        }

        ImGui::EndPopup();
    }

    return result;
}

void EditorInstance::renderChildrenEditor(SPLResource& res) {
    if (!res.childResource) {
        ImGui::TextUnformatted("This resource does not have an associated child resource.");
        if (ImGui::IconButton(ICON_FA_PLUS, "Add Child Resource", AppColors::Turquoise)) {
            res.header.flags.hasChildResource = true;
            res.childResource = SPLChildResource{
                .flags = {
                    .usesBehaviors = false,
                    .hasScaleAnim = false,
                    .hasAlphaAnim = false,
                    .rotationType = SPLChildRotationType::None,
                    .followEmitter = false,
                    .useChildColor = false,
                    .drawType = SPLDrawType::Billboard,
                    .polygonRotAxis = SPLPolygonRotAxis::Y,
                    .polygonReferencePlane = 0
                },
                .randomInitVelMag = 0.0f,
                .endScale = 1.0f,
                .lifeTime = 1.0f / SPLArchive::SPL_FRAMES_PER_SECOND,
                .velocityRatio = 1.0f,
                .scaleRatio = 1.0f,
                .color = {},
                .misc = {
                    .emissionCount = 0,
                    .emissionDelay = 0,
                    .emissionInterval = 1.0f / SPLArchive::SPL_FRAMES_PER_SECOND,
                    .texture = 0,
                    .textureTileCountS = 1,
                    .textureTileCountT = 1,
                    .flipTextureS = false,
                    .flipTextureT = false,
                    .dpolFaceEmitter = false
                }
            };

            pushHistory();
        }

        return;
    }

    auto& child = res.childResource.value();
    constexpr f32 frameTime = 1.0f / (f32)SPLArchive::SPL_FRAMES_PER_SECOND;

    if (NOTIFY(ImGui::IconButton(ICON_FA_XMARK, "Remove Child Resource", AppColors::LightRed))) {
        res.header.flags.hasChildResource = false;
        res.childResource.reset();
        pushHistory();
        return;
    }

    bool open = ImGui::TreeNodeEx("##parentSettings", ImGuiTreeNodeFlags_SpanAvailWidth);
    ImGui::SameLine();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 5);
    ImGui::SeparatorText("Parent Settings");
    if (open) {
        NOTIFY(ImGui::DragInt("Emission Amount", (int*)&child.misc.emissionCount, 1, 0, 20));
        HELP(emissionCount);

        NOTIFY(ImGui::SliderFloat("Emission Delay", &child.misc.emissionDelay, 0, 1));
        HELP(childEmissionDelay);

        NOTIFY(ImGui::SliderFloat("Emission Interval", &child.misc.emissionInterval, frameTime, 8.5f, "%.4fs"));
        HELP(childEmissionInterval);

        u32 emissions = (u32)glm::ceil(res.header.particleLifeTime / child.misc.emissionInterval);
        const u32 maxEmissions = (u32)(res.header.particleLifeTime / frameTime);
        if (NOTIFY(ImGui::SliderInt("Emissions", (int*)&emissions, 1, maxEmissions))) {
            child.misc.emissionInterval = res.header.particleLifeTime / (f32)emissions;
        }
        HELP(childEmissions);

        ImGui::TreePop();
    }

    open = ImGui::TreeNodeEx("##childSettings", ImGuiTreeNodeFlags_SpanAvailWidth);
    ImGui::SameLine();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 5);
    ImGui::SeparatorText("Child Settings");
    if (open) {
        auto& flags = child.flags;
        auto& misc = child.misc;

        if (ImGui::BeginCombo("Draw Type", getDrawType(flags.drawType))) {
            for (const auto [val, name] : detail::g_drawTypeNames) {
                if (NOTIFY(ImGui::Selectable(name, flags.drawType == val))) {
                    flags.drawType = val;
                }
            }

            ImGui::EndCombo();
        }
        HELP(drawType);

        const auto& textures = m_archive.getTextures();
        if (ImGui::ImageButton("##tex", (ImTextureID)textures[misc.texture].glTexture->getHandle(), {32, 32})) {
            ImGui::OpenPopup("##childTexturePicker");
        }
        ImGui::SameLine();
        ImGui::Text("Texture");
        HELP(childTexture);

        if (ImGui::BeginCombo("Child Rotation", getChildRotType(flags.rotationType))) {
            for (const auto [val, name] : detail::g_childRotTypeNames) {
                if (NOTIFY(ImGui::Selectable(name, flags.rotationType == val))) {
                    flags.rotationType = val;
                }
            }

            ImGui::EndCombo();
        }
        HELP(childRotation);

        ImGui::BeginDisabled((u8)flags.drawType < (u8)SPLDrawType::Polygon);

        if (ImGui::BeginCombo("Polygon Rotation Axis", getPolygonRotAxis(flags.polygonRotAxis))) {
            for (const auto [val, name] : detail::g_polygonRotAxisNames) {
                if (NOTIFY(ImGui::Selectable(name, flags.polygonRotAxis == val))) {
                    flags.polygonRotAxis = val;
                }
            }

            ImGui::EndCombo();
        }
        HELP(polygonRotAxis);

        ImGui::TextUnformatted("Polygon Reference Plane");
        HELP(polygonReferencePlane);
        ImGui::Indent();
        NOTIFY(ImGui::RadioButton("XY", &flags.polygonReferencePlane, 0));
        NOTIFY(ImGui::RadioButton("XZ", &flags.polygonReferencePlane, 1));
        ImGui::Unindent();

        ImGui::EndDisabled();

        NOTIFY(ImGui::Checkbox("Uses Behaviors", &flags.usesBehaviors));
        HELP(usesBehaviors);

        NOTIFY(ImGui::Checkbox("Follow Emitter", &flags.followEmitter));
        HELP(followEmitter);

        NOTIFY(ImGui::SliderFloat("Lifetime", &child.lifeTime, frameTime, 60, "%.4fs", ImGuiSliderFlags_Logarithmic));
        HELP(particleLifeTime);

        NOTIFY(ImGui::SliderFloat("Initial Velocity Random", &child.randomInitVelMag, -3, 3, "%.3f", ImGuiSliderFlags_Logarithmic));
        HELP(randomInitVelMag);

        NOTIFY(ImGui::SliderFloat("Velocity Ratio", &child.velocityRatio, 0, 1));
        HELP(velocityRatio);

        NOTIFY(ImGui::SliderFloat("Scale Ratio", &child.scaleRatio, 1 / 64.0f,  255 / 64.0f));
        HELP(scaleRatio);

        NOTIFY(ImGui::ColorEdit3("Color", glm::value_ptr(child.color)));
        HELP(color);

        NOTIFY(ImGui::Checkbox("Use Color", &flags.useChildColor));
        HELP(useChildColor);

        ImGui::TextUnformatted("Texture Tiling");
        HELP(textureTiling);
        ImGui::Indent();
        int tileCount = 1 << misc.textureTileCountS;
        NOTIFY(ImGui::SliderInt("S", &tileCount, 1, 8));
        misc.textureTileCountS = glm::log2(tileCount);

        tileCount = 1 << misc.textureTileCountT;
        NOTIFY(ImGui::SliderInt("T", &tileCount, 1, 8));
        misc.textureTileCountT = glm::log2(tileCount);
        ImGui::Unindent();

        NOTIFY(ImGui::Checkbox("DPol Face Emitter", &misc.dpolFaceEmitter));
        HELP(dpolFaceEmitter);

        NOTIFY(ImGui::Checkbox("Flip X", &misc.flipTextureS));
        HELP(flipTextureX);

        NOTIFY(ImGui::Checkbox("Flip Y", &misc.flipTextureT));
        HELP(flipTextureY);

        NOTIFY(ImGui::Checkbox("Scale Animation", &flags.hasScaleAnim));
        HELP(hasScaleAnim);
        if (flags.hasScaleAnim) {
            NOTIFY(ImGui::SliderFloat("End Scale", &child.endScale, 0, 5, "%.3f", ImGuiSliderFlags_Logarithmic));
            HELP(endScale);
        }

        NOTIFY(ImGui::Checkbox("Fade Out", &flags.hasAlphaAnim));
        HELP(hasAlphaAnim);

        if (ImGui::BeginPopup("##childTexturePicker")) {
            for (int i = 0; i < textures.size(); ++i) {
                ImGui::PushID(i);
                const auto& texture = textures[i];
                if (NOTIFY(ImGui::ImageButton("##tex", (ImTextureID)texture.glTexture->getHandle(), {32, 32}))) {
                    child.misc.texture = i;
                    ImGui::CloseCurrentPopup();
                }

                if ((i + 1) % 4 != 0) {
                    ImGui::SameLine();
                }

                ImGui::PopID();
            }

            ImGui::EndPopup();
        }

        ImGui::TreePop();
    }
}

void EditorInstance::renderDebugShapes(std::vector<Renderer*>& renderers) {
    const auto& resources = m_archive.getResources();
    const auto& settings = g_application->getEditor()->getSettings();

    renderers.push_back(m_debugRenderer.get());

    // Render edited emitter and collision plane if any
    if (m_selectedResource != INVALID_RESOURCE) {
        const auto& resource = resources[m_selectedResource];

        if (settings.displayEditedEmitter) {
            glm::vec3 axis;
            switch (resource.header.flags.emissionAxis) {
            case SPLEmissionAxis::X:
                axis = { 1, 0, 0 };
                break;
            case SPLEmissionAxis::Y:
                axis = { 0, 1, 0 };
                break;
            case SPLEmissionAxis::Z:
                axis = { 0, 0, 1 };
                break;
            case SPLEmissionAxis::Emitter:
                axis = glm::normalize(resource.header.axis);
                break;
            }

            const glm::vec4& color = settings.editedEmitterColor;
            switch (resource.header.flags.emissionType) {
            case SPLEmissionType::Point:
                m_debugRenderer->addBox(resource.header.emitterBasePos, { 0.2f, 0.2f, 0.2f }, color);
                break;
            case SPLEmissionType::SphereSurface: [[fallthrough]];
            case SPLEmissionType::Sphere:
                m_debugRenderer->addSphere(resource.header.emitterBasePos, resource.header.radius, color);
                break;
            case SPLEmissionType::CircleBorder: [[fallthrough]];
            case SPLEmissionType::CircleBorderUniform: [[fallthrough]];
            case SPLEmissionType::Circle:
                m_debugRenderer->addCircle(resource.header.emitterBasePos, axis, resource.header.radius, color);
                break;
            case SPLEmissionType::CylinderSurface: [[fallthrough]];
            case SPLEmissionType::Cylinder:
                m_debugRenderer->addCylinder(
                    resource.header.emitterBasePos,
                    axis,
                    resource.header.length,
                    resource.header.radius, 
                    color
                );
                break;
            case SPLEmissionType::HemisphereSurface:
            case SPLEmissionType::Hemisphere:
                m_debugRenderer->addHemisphere(resource.header.emitterBasePos, axis, resource.header.radius, color);
                break;
            }
        }

        for (const auto& bhv : resource.behaviors) {
            if (bhv->type == SPLBehaviorType::CollisionPlane) {
                const auto colPlane = std::static_pointer_cast<SPLCollisionPlaneBehavior>(bhv);
                const glm::vec4 color = colPlane->collisionType == SPLCollisionType::Kill 
                    ? settings.collisionPlaneKillColor 
                    : settings.collisionPlaneBounceColor;
                m_collisionGridRenderer->setColor(color);
                m_collisionGridRenderer->setHeight(colPlane->y);
                renderers.push_back(m_collisionGridRenderer.get());
            }
        }
    }

    const auto emitters = m_particleSystem.getEmitters();
    if (emitters.empty() || !settings.displayActiveEmitters) {
        return;
    }

    // Render emitters
    for (const auto& emitter : m_particleSystem.getEmitters()) {
        const auto resource = emitter->getResource();
        glm::vec3 axis;

        switch (resource->header.flags.emissionAxis) {
        case SPLEmissionAxis::X:
            axis = { 1, 0, 0 };
            break;
        case SPLEmissionAxis::Y:
            axis = { 0, 1, 0 };
            break;
        case SPLEmissionAxis::Z:
            axis = { 0, 0, 1 };
            break;
        case SPLEmissionAxis::Emitter:
            axis = emitter->getAxis();
            break;
        }

        const glm::vec4& color = settings.activeEmitterColor;
        switch (resource->header.flags.emissionType) {
        case SPLEmissionType::Point:
            m_debugRenderer->addBox(emitter->getPosition(), { 0.2f, 0.2f, 0.2f }, color);
            break;
        case SPLEmissionType::SphereSurface: [[fallthrough]];
        case SPLEmissionType::Sphere:
            m_debugRenderer->addSphere(emitter->getPosition(), resource->header.radius, color);
            break;
        case SPLEmissionType::CircleBorder: [[fallthrough]];
        case SPLEmissionType::CircleBorderUniform: [[fallthrough]];
        case SPLEmissionType::Circle:
            m_debugRenderer->addCircle(emitter->getPosition(), axis, resource->header.radius, color);
            break;
        case SPLEmissionType::CylinderSurface: [[fallthrough]];
        case SPLEmissionType::Cylinder:
            m_debugRenderer->addCylinder(
                emitter->getPosition(),
                axis,
                resource->header.length,
                resource->header.radius,
                color
            );
            break;
        case SPLEmissionType::HemisphereSurface: [[fallthrough]];
        case SPLEmissionType::Hemisphere:
            m_debugRenderer->addHemisphere(emitter->getPosition(), axis, resource->header.radius, color);
            break;
        }
    }
}

void EditorInstance::helpPopup(std::string_view text) const {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");

    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(text.data(), text.data() + text.size());
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}
