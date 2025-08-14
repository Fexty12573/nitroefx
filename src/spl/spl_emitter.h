#pragma once

#include "spl_resource.h"
#include "spl_particle.h"
#include "types.h"

#include <vector>

class ParticleSystem;
struct CameraParams;

struct SPLEmitterState {
    bool terminate; // Kills the emitter and all of its particles
    bool emissionPaused;
    bool paused;
    bool renderingDisabled;
    bool started;
    bool looping;
    bool frame1; // Indicates if this is the first frame of the emitter's life
    bool dead; // Stops the emitter from emitting new particles, but allows it to update existing ones
};

class SPLEmitter {
public:
    explicit SPLEmitter(const SPLResource *resource, ParticleSystem* system, bool looping = false, const glm::vec3& pos = {});
    ~SPLEmitter();

    void update(float deltaTime);
    void render(const CameraParams& params);
    void emit(u32 count);
    void emitChildren(const SPLParticle& parent, u32 count);

    bool shouldTerminate() const;

    const SPLResource* getResource() const { return m_resource; }

    glm::vec3 getPosition() const { return m_position; }
    glm::vec3 getVelocity() const { return m_velocity; }
    glm::vec3 getAxis() const { return m_axis; }
    f32 getRadius() const { return m_radius; }
    f32 getLength() const { return m_length; }

private:
    void computeOrthogonalAxes();
    glm::vec3 tiltCoordinates(const glm::vec3& vec) const;

private:
    const SPLResource *m_resource;
    ParticleSystem* m_system;

    std::vector<SPLParticle*> m_particles;
    std::vector<SPLParticle*> m_childParticles;

    SPLEmitterState m_state;

    glm::vec3 m_position;
    glm::vec3 m_velocity;
    glm::vec3 m_particleInitVelocity;

    f32 m_age; // age of the emitter, in seconds
    f32 m_emissionTimer; // time, in seconds, since the last emission

    glm::vec3 m_axis;
    f32 m_initAngle;
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
    f32 m_baseAlpha;
    u8 m_updateCycle; // 0 = every frame, 1 = cycle A, 2 = cycle B, cycles A and B alternate

    glm::vec3 m_crossAxis1;
    glm::vec3 m_crossAxis2;

    friend class ParticleSystem;
    friend struct SPLCollisionPlaneBehavior;
};
