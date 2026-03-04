#pragma once
#include <types.h>
#include "fx.h"

#include <chrono>
#include <functional>
#include <glm/glm.hpp>
#include <spdlog/spdlog.h>


class SPLParticle;
class SPLEmitter;

enum class SPLSpinAxis {
    X = 0,
    Y,
    Z
};

enum class SPLCollisionType {
    Kill = 0,
    Bounce,
};

enum class SPLBehaviorType {
    Gravity,
    Random,
    Magnet,
    Spin,
    CollisionPlane,
    Convergence,
};

struct SPLBehavior {
    SPLBehaviorType type;

    explicit SPLBehavior(SPLBehaviorType type) : type(type) {}
    virtual void apply(SPLParticle& particle, glm::vec3& acceleration, SPLEmitter& emitter, float dt) = 0;
};

// Applies a gravity behavior to particles
struct SPLGravityBehaviorNative {
    VecFx16 magnitude;
    u16 padding;
};

// Applies a random movement behavior to particles
struct SPLRandomBehaviorNative {
    VecFx16 magnitude;
    u16 applyInterval; // The interval, in frames, at which the random behavior is applied
};

// Applies a magnetic force to particles
struct SPLMagnetBehaviorNative {
    VecFx32 target;
    fx16 force;
    u16 padding;
};

// Applies a rotational force to particles
struct SPLSpinBehaviorNative {
    u16 angle; // The angle of rotation, in "index" units, where 0x10000 is a full rotation. Applied every frame
    u16 axis; // The axis of rotation
};

// Applies a collision behavior to particles
struct SPLCollisionPlaneBehaviorNative {
    fx32 y; // The Y position of the collision plane
    fx16 elasticity; // The elasticity of the collision, 1.0 being perfectly elastic and 0.0 being perfectly inelastic

    struct {
        u16 collisionType : 2;
        u16 : 14;
    } flags;
};

// Applies a convergence behavior to particles
// Similar to SPLMagnetBehavior, but it acts directly on the particle's position instead of its acceleration
struct SPLConvergenceBehaviorNative {
    VecFx32 target;
    fx16 force;
    u16 padding;
};


struct SPLGravityBehavior : SPLBehavior {
    glm::vec3 magnitude;

    explicit SPLGravityBehavior(const SPLGravityBehaviorNative& native) 
        : SPLBehavior(SPLBehaviorType::Gravity)
        , magnitude(native.magnitude.toVec3()) {}

    explicit SPLGravityBehavior(const glm::vec3& mag)
        : SPLBehavior(SPLBehaviorType::Gravity)
        , magnitude(mag) {}

    void apply(SPLParticle& particle, glm::vec3& acceleration, SPLEmitter& emitter, float dt) override;
};

struct SPLRandomBehavior : SPLBehavior {
    glm::vec3 magnitude;
    f32 applyInterval;
    std::chrono::time_point<std::chrono::steady_clock> lastApplication;

    explicit SPLRandomBehavior(const SPLRandomBehaviorNative& native);

    SPLRandomBehavior(const glm::vec3& mag, f32 interval)
        : SPLBehavior(SPLBehaviorType::Random)
        , magnitude(mag)
        , applyInterval(interval)
        , lastApplication(std::chrono::steady_clock::now()) {}

    void apply(SPLParticle& particle, glm::vec3& acceleration, SPLEmitter& emitter, float dt) override;
};

struct SPLMagnetBehavior : SPLBehavior {
    glm::vec3 target;
    f32 force;

    explicit SPLMagnetBehavior(const SPLMagnetBehaviorNative& native) 
        : SPLBehavior(SPLBehaviorType::Magnet)
        , target(native.target.toVec3())
        , force(FX_FX16_TO_F32(native.force)) {}

    SPLMagnetBehavior(const glm::vec3& target, f32 force)
        : SPLBehavior(SPLBehaviorType::Magnet)
        , target(target)
        , force(force) {}

    void apply(SPLParticle& particle, glm::vec3& acceleration, SPLEmitter& emitter, float dt) override;
};

struct SPLSpinBehavior : SPLBehavior {
    f32 angle;
    SPLSpinAxis axis;

    explicit SPLSpinBehavior(const SPLSpinBehaviorNative& native);

    SPLSpinBehavior(f32 angle, SPLSpinAxis axis)
        : SPLBehavior(SPLBehaviorType::Spin)
        , angle(angle)
        , axis(axis) {}

    void apply(SPLParticle& particle, glm::vec3& acceleration, SPLEmitter& emitter, float dt) override;
};

struct SPLCollisionPlaneBehavior : SPLBehavior {
    f32 y;
    f32 elasticity;
    SPLCollisionType collisionType;

    explicit SPLCollisionPlaneBehavior(const SPLCollisionPlaneBehaviorNative& native) 
        : SPLBehavior(SPLBehaviorType::CollisionPlane)
        , y(FX_FX32_TO_F32(native.y))
        , elasticity(FX_FX16_TO_F32(native.elasticity))
        , collisionType(static_cast<SPLCollisionType>(native.flags.collisionType)) {}

    SPLCollisionPlaneBehavior(f32 y, f32 elasticity, SPLCollisionType type)
        : SPLBehavior(SPLBehaviorType::CollisionPlane)
        , y(y)
        , elasticity(elasticity)
        , collisionType(type) {}

    void apply(SPLParticle& particle, glm::vec3& acceleration, SPLEmitter& emitter, float dt) override;
};

struct SPLConvergenceBehavior : SPLBehavior {
    glm::vec3 target;
    f32 force;

    explicit SPLConvergenceBehavior(const SPLConvergenceBehaviorNative& native) 
        : SPLBehavior(SPLBehaviorType::Convergence)
        , target(native.target.toVec3())
        , force(FX_FX16_TO_F32(native.force)) {}

    SPLConvergenceBehavior(const glm::vec3& target, f32 force)
        : SPLBehavior(SPLBehaviorType::Convergence)
        , target(target)
        , force(force) {}

    void apply(SPLParticle& particle, glm::vec3& acceleration, SPLEmitter& emitter, float dt) override;
};


