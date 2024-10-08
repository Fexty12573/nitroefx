#pragma once
#include "spl/spl_resource.h"
#include "editor_instance.h"
#include "types.h"

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
    void render();
    void renderParticles();
    void openPicker();
    void openEditor();
    void updateParticles(float deltaTime);

    void playEmitterAction(EmitterSpawnType spawnType);
    void killEmitters();

    void handleEvent(const SDL_Event& event);

private:
    void renderResourcePicker();
    void renderResourceEditor();

    void renderHeaderEditor(SPLResourceHeader& header) const;
    void renderBehaviorEditor(SPLResource& res);

    bool renderGravityBehaviorEditor(const std::shared_ptr<SPLGravityBehavior>& gravity);
    bool renderRandomBehaviorEditor(const std::shared_ptr<SPLRandomBehavior>& random);
    bool renderMagnetBehaviorEditor(const std::shared_ptr<SPLMagnetBehavior>& magnet);
    bool renderSpinBehaviorEditor(const std::shared_ptr<SPLSpinBehavior>& spin);
    bool renderCollisionPlaneBehaviorEditor(const std::shared_ptr<SPLCollisionPlaneBehavior>& collisionPlane);
    bool renderConvergenceBehaviorEditor(const std::shared_ptr<SPLConvergenceBehavior>& convergence);

    void renderAnimationEditor(SPLResource& res);

    void renderChildrenEditor(SPLResource& res);


private:
    bool m_picker_open = true;
    bool m_editor_open = true;
    float m_timeScale = 1.0f;

    EmitterSpawnType m_emitterSpawnType = EmitterSpawnType::SingleShot;
    float m_emitterInterval = 1.0f; // seconds

    std::unordered_map<u64, int> m_selectedResources;
    std::weak_ptr<EditorInstance> m_activeEditor;

    struct EmitterSpawnTask {
        u64 resourceIndex;
        std::chrono::time_point<std::chrono::steady_clock> time;
        std::chrono::duration<float> interval;
        u64 editorID;
    };

    std::vector<EmitterSpawnTask> m_emitterTasks;
};
