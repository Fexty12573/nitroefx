#include "editor.h"
#include "project_manager.h"
#include "spl/enum_names.h"
#include "help_messages.h"

#include <array>
#include <fmt/format.h>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/integer.hpp>
#include <imgui.h>
#include <imgui_internal.h>

#define LOCK_EDITOR() auto activeEditor_locked = m_activeEditor.lock()
#define NOTIFY(action) activeEditor_locked->valueChanged(action)
#define HELP(name) helpPopup(help::name)


namespace {

constexpr std::array s_emitterSpawnTypes = {
    "Single Shot",
    "Looped",
    "Interval"
};

}


void Editor::render() {
    const auto& instances = g_projectManager->getOpenEditors();

    ImGuiWindowClass windowClass;
    windowClass.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_NoTabBar
        | ImGuiDockNodeFlags_NoDockingOverCentralNode
        | ImGuiDockNodeFlags_NoUndocking;
    ImGui::SetNextWindowClass(&windowClass);

    ImGui::Begin("Work Area##Editor", nullptr,
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoDecoration
    );

    std::vector<std::shared_ptr<EditorInstance>> toClose;
    if (ImGui::BeginTabBar("Editor Instances", ImGuiTabBarFlags_Reorderable 
        | ImGuiTabBarFlags_FittingPolicyResizeDown | ImGuiTabBarFlags_AutoSelectNewTabs)) {
        for (const auto& instance : instances) {
            const auto [open, active] = instance->render();
            if (!open) {
                toClose.push_back(instance);
            }
            if (active) {
                g_projectManager->setActiveEditor(instance);
            }
        }

        ImGui::EndTabBar();
    }

    for (const auto& instance : toClose) {
        g_projectManager->closeEditor(instance);
    }

    ImGui::End();

    if (m_picker_open) {
        renderResourcePicker();
    }

    if (m_editor_open) {
        renderResourceEditor();
    }
}

void Editor::renderParticles() {
    const auto& editor = g_projectManager->getActiveEditor();
    if (!editor) {
        return;
    }

    editor->renderParticles();
}

void Editor::openPicker() {
    m_picker_open = true;
}

void Editor::openEditor() {
    m_editor_open = true;
}

void Editor::updateParticles(float deltaTime) {
    const auto& editor = g_projectManager->getActiveEditor();
    if (!editor) {
        return;
    }

    for (auto& task : m_emitterTasks) {
        const auto now = std::chrono::steady_clock::now();
        if (task.editorID == editor->getUniqueID() && now - task.time >= task.interval) {
            editor->getParticleSystem().addEmitter(
                editor->getArchive().getResources()[task.resourceIndex], 
                false
            );
            task.time = now;
        }
    }

    editor->updateParticles(deltaTime * m_timeScale);
}

void Editor::playEmitterAction(EmitterSpawnType spawnType) {
    const auto& editor = g_projectManager->getActiveEditor();
    if (!editor) {
        return;
    }

    const auto resourceIndex = m_selectedResources[editor->getUniqueID()];
    editor->getParticleSystem().addEmitter(
        editor->getArchive().getResource(resourceIndex),
        spawnType == EmitterSpawnType::Looped
    );

    if (spawnType == EmitterSpawnType::Interval) {
        m_emitterTasks.emplace_back(
            resourceIndex,
            std::chrono::steady_clock::now(),
            std::chrono::duration<float>(m_emitterInterval),
            editor->getUniqueID()
        );
    }
}

void Editor::killEmitters() {
    const auto& editor = g_projectManager->getActiveEditor();
    if (!editor) {
        return;
    }

    editor->getParticleSystem().killAllEmitters();
    std::erase_if(m_emitterTasks, [id = editor->getUniqueID()](const auto& task) {
        return task.editorID == id;
    });
}

void Editor::handleEvent(const SDL_Event& event) {
    const auto& editor = g_projectManager->getActiveEditor();
    if (!editor) {
        return;
    }

    editor->handleEvent(event);
}

void Editor::renderResourcePicker() {
    if (ImGui::Begin("Resource Picker##Editor", &m_picker_open)) {

        const auto& editor = g_projectManager->getActiveEditor();
        if (!editor) {
            ImGui::Text("No editor open");
            ImGui::End();
            return;
        }

        auto& archive = editor->getArchive();
        auto& resources = archive.getResources();
        auto& textures = archive.getTextures();

        const auto id = editor->getUniqueID();
        if (!m_selectedResources.contains(id)) {
            m_selectedResources[id] = -1;
        }

        const auto contentRegion = ImGui::GetContentRegionAvail();
        if (ImGui::BeginListBox("##Resources", contentRegion)) {
            const ImGuiStyle& style = ImGui::GetStyle();

            for (int i = 0; i < resources.size(); ++i) {
                const auto& resource = resources[i];
                const auto& texture = textures[resource.header.misc.textureIndex];

                ImGui::PushID(i);
                const auto name = fmt::format("[{}] Tex {}x{}", i, texture.width, texture.height);

                auto bgColor = m_selectedResources[id] == i
                    ? style.Colors[ImGuiCol_ButtonActive]
                    : style.Colors[ImGuiCol_Button];

                const auto cursor = ImGui::GetCursorScreenPos();
                if (ImGui::InvisibleButton("##Resource", { contentRegion.x, 32 })) {
                    m_selectedResources[id] = i;
                }

                if (ImGui::IsItemHovered()) {
                    bgColor = style.Colors[ImGuiCol_ButtonHovered];
                }

                // Draw a filled rectangle behind the item
                ImGui::GetWindowDrawList()->AddRectFilled(
                    cursor, 
                    { cursor.x + contentRegion.x, cursor.y + 32 }, 
                    ImGui::ColorConvertFloat4ToU32(bgColor),
                    2.5f
                );

                ImGui::SetCursorScreenPos(cursor);
                ImGui::Image((ImTextureID)(uintptr_t)texture.glTexture->getHandle(), { 32, 32 });

                ImGui::SameLine();

                const auto textHeight = ImGui::GetFontSize();
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (32 - textHeight) / 2);
                ImGui::TextUnformatted(name.c_str());

                ImGui::PopID();
            }

            ImGui::EndListBox();
        }
    }

    ImGui::End();
}

void Editor::renderResourceEditor() {
    if (ImGui::Begin("Resource Editor##Editor", &m_editor_open)) {
        ImGui::SliderFloat("Global Time Scale", &m_timeScale, 0.0f, 2.0f, "%.2f");

        const auto& editor = g_projectManager->getActiveEditor();
        if (!editor) {
            ImGui::Text("No editor open");
            ImGui::End();
            return;
        }

        m_activeEditor = editor;

        auto& archive = editor->getArchive();
        auto& resources = archive.getResources();
        auto& textures = archive.getTextures();

        const auto id = editor->getUniqueID();
        if (!m_selectedResources.contains(id)) {
            m_selectedResources[id] = -1;
        }

        if (m_selectedResources[id] != -1) {
            auto& resource = resources[m_selectedResources[id]];
            const auto& texture = textures[resource.header.misc.textureIndex];

            if (ImGui::Button("Play Emitter")) {
                playEmitterAction(m_emitterSpawnType);
            }

            ImGui::SameLine();
            ImGui::SetNextItemWidth(150);
            ImGui::Combo("##SpawnType", (int*)&m_emitterSpawnType, s_emitterSpawnTypes.data(), s_emitterSpawnTypes.size());

            if (m_emitterSpawnType == EmitterSpawnType::Interval) {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                ImGui::InputFloat("##Interval", &m_emitterInterval, 0.1f, 1.0f, "%.2fs");
            }

            if (ImGui::Button("Kill Emitters")) {
                killEmitters();
            }

            if (ImGui::BeginTabBar("##editorTabs")) {
                if (ImGui::BeginTabItem("General")) {
                    ImGui::BeginChild("##headerEditor", {}, ImGuiChildFlags_Border);
                    renderHeaderEditor(resource.header);
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Behaviors")) {
                    ImGui::BeginChild("##headerEditor", {}, ImGuiChildFlags_Border);
                    renderBehaviorEditor(resource);
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Children")) {
                    ImGui::BeginChild("##childEditor", {}, ImGuiChildFlags_Border);
                    renderChildrenEditor(resource);
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }

                ImGui::EndTabBar();
            }
        }
    }

    ImGui::End();

    m_activeEditor.reset();
}

void Editor::renderHeaderEditor(SPLResourceHeader& header) const {
    if (m_activeEditor.expired()) {
        return;
    }

    LOCK_EDITOR();

    constexpr auto helpPopup = [](std::string_view text) {
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::BeginItemTooltip())
        {
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
            ImGui::TextUnformatted(text.data());
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    };

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

        NOTIFY(ImGui::DragFloat3("Axis", glm::value_ptr(header.axis)));
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

        ImGui::TreePop();
    }
}

void Editor::renderBehaviorEditor(SPLResource& res) {
    LOCK_EDITOR();
    std::vector<std::shared_ptr<SPLBehavior>> toRemove;

    if (ImGui::Button("Add Behavior...")) {
        ImGui::OpenPopup("##addBehavior");
    }

    if (ImGui::BeginPopup("##addBehavior")) {
        if (NOTIFY(ImGui::MenuItem("Gravity", nullptr, false, !res.header.flags.hasGravityBehavior))) {
            res.behaviors.push_back(std::make_shared<SPLGravityBehavior>(glm::vec3(0, 0, 0)));
            res.header.addBehavior(SPLBehaviorType::Gravity);
        }

        if (NOTIFY(ImGui::MenuItem("Random", nullptr, false, !res.header.flags.hasRandomBehavior))) {
            res.behaviors.push_back(std::make_shared<SPLRandomBehavior>(glm::vec3(0, 0, 0), 1));
            res.header.addBehavior(SPLBehaviorType::Random);
        }

        if (NOTIFY(ImGui::MenuItem("Magnet", nullptr, false, !res.header.flags.hasMagnetBehavior))) {
            res.behaviors.push_back(std::make_shared<SPLMagnetBehavior>(glm::vec3(0, 0, 0), 0));
            res.header.addBehavior(SPLBehaviorType::Magnet);
        }

        if (NOTIFY(ImGui::MenuItem("Spin", nullptr, false, !res.header.flags.hasSpinBehavior))) {
            res.behaviors.push_back(std::make_shared<SPLSpinBehavior>(0, SPLSpinAxis::Y));
            res.header.addBehavior(SPLBehaviorType::Spin);
        }

        if (NOTIFY(ImGui::MenuItem("Collision Plane", nullptr, false, !res.header.flags.hasCollisionPlaneBehavior))) {
            res.behaviors.push_back(std::make_shared<SPLCollisionPlaneBehavior>(0, 0, SPLCollisionType::Bounce));
            res.header.addBehavior(SPLBehaviorType::CollisionPlane);
        }

        if (NOTIFY(ImGui::MenuItem("Convergence", nullptr, false, !res.header.flags.hasConvergenceBehavior))) {
            res.behaviors.push_back(std::make_shared<SPLConvergenceBehavior>(glm::vec3(0, 0, 0), 0));
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

bool Editor::renderGravityBehaviorEditor(const std::shared_ptr<SPLGravityBehavior>& gravity) {
    LOCK_EDITOR();
    static bool hovered = false;
    if (hovered) {
        ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertFloat4ToU32({ 0.7f, 0.3f, 0.7f, 1.0f }));
    }
    ImGui::BeginChild("##gravityEditor", {}, ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY);
    ImGui::TextUnformatted("Gravity");

    NOTIFY(ImGui::DragFloat3("Magnitude", glm::value_ptr(gravity->magnitude)));

    ImGui::EndChild();

    if (hovered) {
        ImGui::PopStyleColor();
    }

    hovered = ImGui::IsItemHovered();
    return ImGui::BeginPopupContextItem("##behaviorContext");
}

bool Editor::renderRandomBehaviorEditor(const std::shared_ptr<SPLRandomBehavior>& random) {
    LOCK_EDITOR();
    static bool hovered = false;
    if (hovered) {
        ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertFloat4ToU32({ 0.7f, 0.3f, 0.7f, 1.0f }));
    }
    ImGui::BeginChild("##randomEditor", {}, ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY);
    ImGui::TextUnformatted("Random");

    NOTIFY(ImGui::DragFloat3("Magnitude", glm::value_ptr(random->magnitude)));
    NOTIFY(ImGui::SliderFloat("Apply Interval", &random->applyInterval, 0, 5, "%.3fs", ImGuiSliderFlags_Logarithmic));

    ImGui::EndChild();

    if (hovered) {
        ImGui::PopStyleColor();
    }

    hovered = ImGui::IsItemHovered();
    return ImGui::BeginPopupContextItem("##behaviorContext");
}

bool Editor::renderMagnetBehaviorEditor(const std::shared_ptr<SPLMagnetBehavior>& magnet) {
    LOCK_EDITOR();
    static bool hovered = false;
    if (hovered) {
        ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertFloat4ToU32({ 0.7f, 0.3f, 0.7f, 1.0f }));
    }
    ImGui::BeginChild("##magnetEditor", {}, ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY);
    ImGui::TextUnformatted("Magnet");

    NOTIFY(ImGui::DragFloat3("Target", glm::value_ptr(magnet->target), 0.05f, -5.0f, 5.0f));
    NOTIFY(ImGui::SliderFloat("Force", &magnet->force, 0, 5, "%.3f", ImGuiSliderFlags_Logarithmic));

    ImGui::EndChild();

    if (hovered) {
        ImGui::PopStyleColor();
    }

    hovered = ImGui::IsItemHovered();
    return ImGui::BeginPopupContextItem("##behaviorContext");
}

bool Editor::renderSpinBehaviorEditor(const std::shared_ptr<SPLSpinBehavior>& spin) {
    LOCK_EDITOR();
    static bool hovered = false;
    if (hovered) {
        ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertFloat4ToU32({ 0.7f, 0.3f, 0.7f, 1.0f }));
    }
    ImGui::BeginChild("##spinEditor", {}, ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY);
    ImGui::TextUnformatted("Spin");

    NOTIFY(ImGui::SliderAngle("Angle", &spin->angle));
    ImGui::TextUnformatted("Axis");
    ImGui::Indent();
    NOTIFY(ImGui::RadioButton("X", (int*)&spin->axis, 0));
    NOTIFY(ImGui::RadioButton("Y", (int*)&spin->axis, 1));
    NOTIFY(ImGui::RadioButton("Z", (int*)&spin->axis, 2));
    ImGui::Unindent();

    ImGui::EndChild();

    if (hovered) {
        ImGui::PopStyleColor();
    }

    hovered = ImGui::IsItemHovered();
    return ImGui::BeginPopupContextItem("##behaviorContext");
}

bool Editor::renderCollisionPlaneBehaviorEditor(const std::shared_ptr<SPLCollisionPlaneBehavior>& collisionPlane) {
    LOCK_EDITOR();
    static bool hovered = false;
    if (hovered) {
        ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertFloat4ToU32({ 0.7f, 0.3f, 0.7f, 1.0f }));
    }
    ImGui::BeginChild("##collisionPlaneEditor", {}, ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY);
    ImGui::TextUnformatted("Collision Plane");

    NOTIFY(ImGui::DragFloat("Height", &collisionPlane->y, 0.05f));
    NOTIFY(ImGui::SliderFloat("Elasticity", &collisionPlane->elasticity, 0, 2, "%.3f", ImGuiSliderFlags_Logarithmic));
    ImGui::TextUnformatted("Collision Type");
    ImGui::Indent();
    NOTIFY(ImGui::RadioButton("Kill", (int*)&collisionPlane->collisionType, 0));
    NOTIFY(ImGui::RadioButton("Bounce", (int*)&collisionPlane->collisionType, 1));
    ImGui::Unindent();

    ImGui::EndChild();

    if (hovered) {
        ImGui::PopStyleColor();
    }

    hovered = ImGui::IsItemHovered();
    return ImGui::BeginPopupContextItem("##behaviorContext");
}

bool Editor::renderConvergenceBehaviorEditor(const std::shared_ptr<SPLConvergenceBehavior>& convergence) {
    LOCK_EDITOR();
    static bool hovered = false;
    if (hovered) {
        ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertFloat4ToU32({ 0.7f, 0.3f, 0.7f, 1.0f }));
    }
    ImGui::BeginChild("##convergenceEditor", {}, ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY);
    ImGui::TextUnformatted("Convergence");

    NOTIFY(ImGui::DragFloat3("Target", glm::value_ptr(convergence->target), 0.05f, -5.0f, 5.0f));
    NOTIFY(ImGui::SliderFloat("Force", &convergence->force, -5, 5, "%.3f", ImGuiSliderFlags_Logarithmic));

    ImGui::EndChild();

    if (hovered) {
        ImGui::PopStyleColor();
    }

    hovered = ImGui::IsItemHovered();
    return ImGui::BeginPopupContextItem("##behaviorContext");
}

void Editor::renderAnimationEditor(SPLResource& res) {
}

void Editor::renderChildrenEditor(SPLResource& res) {
    if (m_activeEditor.expired()) {
        return;
    }

    LOCK_EDITOR();

    if (!res.childResource) {
        ImGui::TextUnformatted("This resource does not have an associated child resource.");
        if (ImGui::Button("Add Child Resource")) {
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
        }

        return;
    }

    auto& child = res.childResource.value();
    constexpr f32 frameTime = 1.0f / (f32)SPLArchive::SPL_FRAMES_PER_SECOND;
    constexpr auto helpPopup = [](std::string_view text) {
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::BeginItemTooltip())
        {
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
            ImGui::TextUnformatted(text.data());
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    };

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

        if (ImGui::BeginCombo("Child Rotation", getChildRotType(flags.rotationType))) {
            for (const auto [val, name] : detail::g_childRotTypeNames) {
                if (NOTIFY(ImGui::Selectable(name, flags.rotationType == val))) {
                    flags.rotationType = val;
                }
            }

            ImGui::EndCombo();
        }
        HELP(childRotation);

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

        NOTIFY(ImGui::SliderFloat("Scale Ratio", &child.scaleRatio, 0, 1));
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

        ImGui::TreePop();
    }
}
