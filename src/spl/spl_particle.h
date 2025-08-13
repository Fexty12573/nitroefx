#pragma once

#include "types.h"

#include <glm/glm.hpp>

class ParticleRenderer;
class SPLEmitter;
struct CameraParams;

class SPLParticle {
public:
    void render(ParticleRenderer* renderer, const CameraParams& params, f32 s, f32 t) const;
    glm::vec3 getWorldPosition() const;

public:
    SPLEmitter* emitter; // The emitter that spawned this particle

    glm::vec3 position; // position of the particle, relative to the emitter
    glm::vec3 velocity;
    f32 rotation;
    f32 angularVelocity;
    f32 lifeTime; // time the particle will live for, in seconds
    f32 age; // time the particle has been alive for, in seconds
    f32 emissionTimer; // time since this particle has emitted child particles, in seconds

    // These two values are essentially 1.0f / lifeTime (or 1.0f / loopTime), represented as an integer
    // They are used to map between age/lifeTime and a [0, 255] range
    // Mainly just to lower the amount of divisions needed in the update functions
    u16 loopTimeFactor;
    u16 lifeTimeFactor;

    u8 texture; // Index of the current texture in the resource

    // A value between 0 and 1 that is added to the life rate of the particle.
    // This is used only for looping particles, so particles spawned at the same time
    // don't have aren't all in sync (animation-wise)
    f32 lifeRateOffset;

    struct {
        f32 baseAlpha;
        f32 animAlpha;
    } visibility;
    f32 baseScale;
    f32 animScale;
    glm::vec3 color;
    glm::vec3 emitterPos;
};
