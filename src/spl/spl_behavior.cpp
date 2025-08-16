#include "spl_behavior.h"
#include "spl_random.h"
#include "spl_archive.h"
#include "spl_emitter.h"
#include "spl_particle.h"

#include <glm/gtc/matrix_transform.hpp>


void SPLGravityBehavior::apply(SPLParticle& particle, glm::vec3& acceleration, SPLEmitter& emitter, float dt) {
    acceleration += magnitude;
}

SPLRandomBehavior::SPLRandomBehavior(const SPLRandomBehaviorNative& native) : SPLBehavior(SPLBehaviorType::Random) {
    magnitude = native.magnitude.toVec3();
    applyInterval = (f32)native.applyInterval / SPLArchive::SPL_FRAMES_PER_SECOND;
    lastApplication = std::chrono::steady_clock::now();
}

void SPLRandomBehavior::apply(SPLParticle& particle, glm::vec3& acceleration, SPLEmitter& emitter, float dt) {
    const auto now = std::chrono::steady_clock::now();
    const auto delta = std::chrono::duration_cast<std::chrono::duration<float>>(now - lastApplication);
    if (delta.count() >= applyInterval) {
        acceleration.x += SPLRandom::aroundZero(magnitude.x);
        acceleration.y += SPLRandom::aroundZero(magnitude.y);
        acceleration.z += SPLRandom::aroundZero(magnitude.z);
        lastApplication = now;
    }
}

void SPLMagnetBehavior::apply(SPLParticle& particle, glm::vec3& acceleration, SPLEmitter& emitter, float dt) {
    acceleration += force * (target - (particle.position + particle.velocity));
}

SPLSpinBehavior::SPLSpinBehavior(const SPLSpinBehaviorNative& native) : SPLBehavior(SPLBehaviorType::Spin) {
    axis = (SPLSpinAxis)native.axis;
    angle = static_cast<f32>(native.angle) / 65535.0f * glm::two_pi<f32>();

    // Normalize the angle to be within the range [-pi, pi]
    if (angle > glm::pi<f32>()) {
        angle -= glm::two_pi<f32>();
    }
}

void SPLSpinBehavior::apply(SPLParticle& particle, glm::vec3& acceleration, SPLEmitter& emitter, float dt) {
    const auto adjustedAngle = angle * dt * SPLArchive::SPL_FRAMES_PER_SECOND;
    switch (axis) {
    case SPLSpinAxis::X:
        particle.position = glm::rotate(glm::mat4(1), adjustedAngle, { 1, 0, 0 }) * glm::vec4(particle.position, 1);
        break;
    case SPLSpinAxis::Y:
        particle.position = glm::rotate(glm::mat4(1), adjustedAngle, { 0, 1, 0 }) * glm::vec4(particle.position, 1);
        break;
    case SPLSpinAxis::Z:
        particle.position = glm::rotate(glm::mat4(1), adjustedAngle, { 0, 0, 1 }) * glm::vec4(particle.position, 1);
        break;
    }
}

void SPLCollisionPlaneBehavior::apply(SPLParticle& particle, glm::vec3& acceleration, SPLEmitter& emitter, float dt) {
    const f32 cy = emitter.m_collisionPlaneHeight > std::numeric_limits<f32>::min()
        ? emitter.m_collisionPlaneHeight
        : this->y;

    constexpr auto moved_above = [](f32 py_, f32 ey_, f32 cy_) { return ey_ < cy_ && ey_ + py_ > cy_; };
    constexpr auto moved_below = [](f32 py_, f32 ey_, f32 cy_) { return ey_ >= cy_ && ey_ + py_ < cy_; };

    const f32 py = particle.position.y;
    const f32 ey = particle.emitterPos.y;

    switch (collisionType) {
    case SPLCollisionType::Kill:
        if (moved_above(py, ey, cy) || moved_below(py, ey, cy)) {
            particle.position.y = cy - ey;
            particle.age = particle.lifeTime;
        }
        break;
    case SPLCollisionType::Bounce:
        if (moved_above(py, ey, cy) || moved_below(py, ey, cy)) {
            particle.position.y = cy - ey;
            particle.velocity.y *= -elasticity;
        }
        break;
    }
}

void SPLConvergenceBehavior::apply(SPLParticle& particle, glm::vec3& acceleration, SPLEmitter& emitter, float dt) {
    particle.position += force * (target - particle.position) * dt * (f32)SPLArchive::SPL_FRAMES_PER_SECOND;
}
