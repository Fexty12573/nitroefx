#pragma once

#include "types.h"
#include "util/crc32.h"

#include <map>
#include <string>

#include <glm/glm.hpp>


struct EditorSettings {
    bool displayActiveEmitters = true; // Display active emitters
    bool displayEditedEmitter = true; // Display the emitter being edited
    bool useOrthographicCamera = false; // Use orthographic camera instead of perspective
    bool useFixedDsResolution = false; // Use fixed display resolution for the editor (Nintendo DS Resolution)
    int fixedDsResolutionScale = 1; // Scale for the fixed DS resolution
    glm::vec4 activeEmitterColor = { 1.0f, 1.0f, 0.0f, 0.4f }; // Color of the active emitter
    glm::vec4 editedEmitterColor = { 1.0f, 0.0f, 1.0f, 0.3f }; // Color of the edited emitter
    glm::vec4 collisionPlaneBounceColor = { 0.0f, 1.0f, 0.0f, 0.3f }; // Color of the collision plane (bounce mode)
    glm::vec4 collisionPlaneKillColor = { 1.0f, 0.0f, 0.0f, 0.3f }; // Color of the collision plane (kill mode)
    glm::vec4 backgroundColor = { 0.0f, 0.0f, 0.0f, 1.0f }; // Background color of the viewport
    u32 maxParticles = 1000; // Maximum number of particles to process
};


