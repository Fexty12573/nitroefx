#pragma once

#include "spl_resource.h"
#include "spl_particle.h"
#include "types.h"

#include <vector>


struct SPLEmitterState {
    bool terminate;
    bool emissionPaused;
    bool paused;
    bool renderingDisabled;
    bool started;
};

class SPLEmitter {
public:
    explicit SPLEmitter(SPLResource *resource, const glm::vec3& pos = {});
    ~SPLEmitter();

    void update();
    void render();

private:
    SPLResource *m_resource;

    std::vector<SPLParticle> m_particles;
    std::vector<SPLParticle> m_childParticles;

    SPLEmitterState m_state;

    glm::vec3 m_position;
    glm::vec3 m_velocity;
    glm::vec3 m_particleInitVelocity;
    f32 m_age; // age of the emitter, in seconds
    glm::vec3 m_axis;
    u16 m_initAngle;
    f32 m_emissionCount;
    f32 m_radius;
    f32 m_length;
    f32 m_initVelPositionAmplifier; // amplifies the initial velocity of the particles based on their position
    f32 m_initVelAxisAmplifier; // amplifies the initial velocity of the particles based on the emitter's axis
    f32 m_baseScale; // base scale of the particles
    f32 m_particleLifeTime; // life time of the particles, in seconds
    glm::vec3 m_color;
    f32 m_collisionPlaneHeight;
    glm::vec2 m_texCoords;
    glm::vec2 m_childTexCoords;

    f32 m_emissionInterval; // time, in seconds, between particle emissions
    u8 m_baseAlpha;
    u8 m_updateCycle; // 0 = every frame, 1 = cycle A, 2 = cycle B, cycles A and B alternate

    glm::vec3 m_crossAxis1;
    glm::vec3 m_crossAxis2;
};
