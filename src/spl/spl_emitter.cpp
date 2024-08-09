#include "spl_emitter.h"
#include "editor/particle_system.h"

#include <glm/gtc/random.hpp>
#include <glm/gtc/constants.hpp>
#include <ranges>

#include "random.h"

#define ALLOW_DEATH_EMISSIONS 1

SPLEmitter::SPLEmitter(const SPLResource* resource, ParticleSystem* system, bool looping, const glm::vec3& pos) {
    m_resource = resource;
    m_system = system;
    m_state = { .looping = looping };

    m_position = pos + resource->header.emitterBasePos;
    m_particleInitVelocity = {};
    m_velocity = {};

    m_age = 0;
    m_emissionTimer = 0;

    m_axis = resource->header.axis;
    m_initAngle = resource->header.initAngle;
    m_emissionCount = resource->header.emissionCount;
    m_radius = resource->header.radius;
    m_length = resource->header.length;
    m_initVelPositionAmplifier = resource->header.initVelPosAmplifier;
    m_initVelAxisAmplifier = resource->header.initVelAxisAmplifier;
    m_baseScale = resource->header.baseScale;
    m_particleLifeTime = resource->header.particleLifeTime;

    m_color = resource->header.color;
    m_emissionInterval = resource->header.misc.emissionInterval;
    m_baseAlpha = resource->header.misc.baseAlpha;
    m_updateCycle = 0;
    m_collisionPlaneHeight = std::numeric_limits<f32>::min();

    // Textures can only be tiled in powers of 2
    m_texCoords = {
        glm::pow(2.0f, resource->header.misc.textureTileCountS),
        glm::pow(2.0f, resource->header.misc.textureTileCountT)
    };

    if (resource->header.misc.flipTextureS) {
        m_texCoords.x = -m_texCoords.x;
    }

    if (resource->header.misc.flipTextureT) {
        m_texCoords.y = -m_texCoords.y;
    }

    if (resource->header.flags.hasChildResource && resource->childResource) {
        m_childTexCoords = {
            resource->childResource->misc.textureTileCountS,
            resource->childResource->misc.textureTileCountT
        };

        if (resource->childResource->misc.flipTextureS) {
            m_childTexCoords.x = -m_childTexCoords.x;
        }

        if (resource->childResource->misc.flipTextureT) {
            m_childTexCoords.y = -m_childTexCoords.y;
        }
    }

    m_crossAxis1 = {};
    m_crossAxis2 = {};
}

SPLEmitter::~SPLEmitter() = default;

void SPLEmitter::update(float deltaTime) {
    const auto& header = m_resource->header;
    constexpr auto wrap_f32 = [](f32 x) { return x - std::floor(x); };

    if (!m_state.terminate && (m_age == 0.0f || m_age <= header.emitterLifeTime)) {
        while (m_emissionTimer >= header.misc.emissionInterval) {
            emit((u32)header.emissionCount);
            m_emissionTimer -= header.misc.emissionInterval;
        }
    }

    struct AnimFunc {
        const SPLAnim* anim;
        bool loop;

        void operator()(SPLParticle& ptcl, const SPLResource& resource, f32 lifeRate) const {
            anim->apply(ptcl, resource, lifeRate);
        }
    };

    AnimFunc animFuncs[4] = {};
    int animFuncCount = 0;

    if (header.flags.hasScaleAnim && m_resource->scaleAnim) {
        animFuncs[animFuncCount++] = AnimFunc{
            &m_resource->scaleAnim.value(),
            m_resource->scaleAnim->flags.loop
        };
    }

    if (header.flags.hasColorAnim && m_resource->colorAnim && !m_resource->colorAnim->flags.randomStartColor) {
        animFuncs[animFuncCount++] = {
            &m_resource->colorAnim.value(),
            m_resource->colorAnim->flags.loop
        };
    }

    if (header.flags.hasAlphaAnim && m_resource->alphaAnim) {
        animFuncs[animFuncCount++] = {
            &m_resource->alphaAnim.value(),
            m_resource->alphaAnim->flags.loop
        };
    }

    if (header.flags.hasTexAnim && m_resource->texAnim && !m_resource->texAnim->param.randomizeInit) {
        animFuncs[animFuncCount++] = {
            &m_resource->texAnim.value(),
            m_resource->texAnim->param.loop
        };
    }

    std::vector<SPLParticle*> particlesToRemove;
    std::vector<SPLParticle*> childParticlesToRemove;

    for (const auto ptcl : m_particles) {
        const f32 lifeRates[2] = {
            ptcl->age / ptcl->lifeTime, // non-looping
            wrap_f32(ptcl->lifeRateOffset + ptcl->age / m_resource->header.misc.loopTime) // looping
        };

        for (int i = 0; i < animFuncCount; ++i) {
            animFuncs[i](*ptcl, *m_resource, lifeRates[animFuncs[i].loop]);
        }

        if (header.flags.followEmitter) {
            ptcl->emitterPos = m_position;
        }

        glm::vec3 acc{};

        for (const auto& behavior : m_resource->behaviors) {
            behavior->apply(*ptcl, acc, *this);
        }

        ptcl->rotation += ptcl->angularVelocity * deltaTime;

        ptcl->velocity *= header.misc.airResistance;
        ptcl->velocity += acc * deltaTime;

        ptcl->position += (ptcl->velocity + m_velocity) * deltaTime;

        if (header.flags.hasChildResource && m_resource->childResource) {
            const auto& child = m_resource->childResource.value();
            const auto lifeRate = ptcl->age / ptcl->lifeTime;

            if (lifeRate >= child.misc.emissionDelay) {
                while (ptcl->emissionTimer >= child.misc.emissionInterval) {
                    emitChildren(*ptcl, child.misc.emissionCount);
                    ptcl->emissionTimer -= child.misc.emissionInterval;
                }
            }
        }

        ptcl->age += deltaTime;
        ptcl->emissionTimer += deltaTime;

        if (ptcl->age >= ptcl->lifeTime) {
            particlesToRemove.push_back(ptcl);
        }
    }

    if (header.flags.hasChildResource && m_resource->childResource) {
        auto& child = m_resource->childResource.value();

        for (const auto ptcl : m_childParticles) {
            const f32 lifeRate = ptcl->age / ptcl->lifeTime;
            if (child.flags.hasScaleAnim) {
                child.applyScaleAnim(*ptcl, lifeRate);
            }

            if (child.flags.hasAlphaAnim) {
                child.applyAlphaAnim(*ptcl, lifeRate);
            }

            if (child.flags.followEmitter) {
                ptcl->emitterPos = m_position;
            }

            glm::vec3 acc{};

            if (child.flags.usesBehaviors) {
                for (const auto& behavior : m_resource->behaviors) {
                    behavior->apply(*ptcl, acc, *this);
                }
            }

            ptcl->rotation += ptcl->angularVelocity;

            ptcl->velocity *= header.misc.airResistance;
            ptcl->velocity += acc;

            ptcl->position += ptcl->velocity + m_velocity;

            ptcl->age += deltaTime;
            ptcl->emissionTimer += deltaTime;

            if (ptcl->age >= ptcl->lifeTime) {
                childParticlesToRemove.push_back(ptcl);
            }
        }
    }

    if (m_state.looping && m_age > header.emitterLifeTime) {
        m_age = 0;
        m_emissionTimer = 0;
    }

    m_age += deltaTime;
    m_emissionTimer += deltaTime;

    for (const auto ptcl : particlesToRemove) {
        std::erase(m_particles, ptcl);
        m_system->freeParticle(ptcl);
    }

    for (const auto ptcl : childParticlesToRemove) {
        std::erase(m_childParticles, ptcl);
        m_system->freeParticle(ptcl);
    }
}

void SPLEmitter::render(const glm::vec3& cameraPos) {
    ParticleRenderer* renderer = m_system->getRenderer();
    for (const auto ptcl : std::views::reverse(m_particles)) {
        ptcl->render(renderer, cameraPos, m_texCoords.s, m_texCoords.t);
    }
}

void SPLEmitter::emit(u32 count) {
    const auto& header = m_resource->header;

    switch (header.flags.emissionType) {
    case SPLEmissionType::Point: [[fallthrough]];
    case SPLEmissionType::Sphere: [[fallthrough]];
    case SPLEmissionType::SphereSurface:
        break;
    case SPLEmissionType::CircleBorder: [[fallthrough]];
    case SPLEmissionType::CircleBorderUniform: [[fallthrough]];
    case SPLEmissionType::Circle: [[fallthrough]];
    case SPLEmissionType::CylinderSurface: [[fallthrough]];
    case SPLEmissionType::Cylinder: [[fallthrough]];
    case SPLEmissionType::HemisphereSurface: [[fallthrough]];
    case SPLEmissionType::Hemisphere:
        computeOrthogonalAxes();
        break;
    }

    for (u32 i = 0; i < count; ++i) {
        const auto ptcl = m_system->allocateParticle();
        if (!ptcl) {
            return;
        }

        m_particles.push_back(ptcl);

        switch (header.flags.emissionType) {
        case SPLEmissionType::Point: {
            ptcl->position = {};
        } break;

        case SPLEmissionType::SphereSurface: {
            ptcl->position = glm::sphericalRand(header.radius);
        } break;

        case SPLEmissionType::CircleBorder: {
            ptcl->position = tiltCoordinates({ glm::circularRand(header.radius), 0 });
        } break;

        case SPLEmissionType::CircleBorderUniform: {
            const f32 angle = glm::mix(0.0f, glm::two_pi<f32>(), (f32)i / (f32)count);
            ptcl->position = tiltCoordinates({ 
                glm::sin(angle) * header.radius,
                glm::cos(angle) * header.radius,
                0
            });
        } break;

        case SPLEmissionType::Sphere: {
            ptcl->position = glm::ballRand(header.radius);
        } break;

        case SPLEmissionType::Circle: {
            ptcl->position = tiltCoordinates({ glm::diskRand(header.radius), 0 });
        } break;

        case SPLEmissionType::CylinderSurface: {
            ptcl->position = tiltCoordinates({
                glm::circularRand(header.radius),
                glm::linearRand(-header.length, header.length),
            });
        } break;

        case SPLEmissionType::Cylinder: {
            ptcl->position = tiltCoordinates({
                glm::diskRand(header.radius),
                glm::linearRand(-header.length, header.length),
            });
        } break;

        case SPLEmissionType::HemisphereSurface: {
            ptcl->position = glm::sphericalRand(header.radius);
            const auto emitterUp = glm::cross(m_crossAxis1, m_crossAxis2);
            if (glm::dot(ptcl->position, emitterUp) <= 0) {
                ptcl->position = -ptcl->position;
            }
        } break;

        case SPLEmissionType::Hemisphere: {
            ptcl->position = glm::ballRand(header.radius);
            const auto emitterUp = glm::cross(m_crossAxis1, m_crossAxis2);
            if (glm::dot(ptcl->position, emitterUp) <= 0) {
                ptcl->position = -ptcl->position;
            }
        } break;
        }

        const f32 magPos = random::scaledRange2(header.initVelPosAmplifier, header.variance.initVel);
        const f32 magAxis = random::scaledRange2(header.initVelAxisAmplifier, header.variance.initVel);

        glm::vec3 posNorm;
        if (header.flags.emissionType == SPLEmissionType::CylinderSurface) {
            posNorm = glm::normalize(ptcl->velocity.x * m_crossAxis1 + ptcl->velocity.y * m_crossAxis2);
        } else if (ptcl->position == glm::vec3(0)) {
            posNorm = random::unitVector();
        } else {
            posNorm = glm::normalize(ptcl->position);
        }

        ptcl->velocity = posNorm * magPos + m_axis * magAxis + m_particleInitVelocity;
        ptcl->emitterPos = m_position;

        ptcl->baseScale = random::scaledRange2(header.baseScale, header.variance.baseScale);
        ptcl->animScale = 0;

        if (header.flags.hasColorAnim && m_resource->colorAnim && m_resource->colorAnim->flags.randomStartColor) {
            const glm::vec3 startColors[3] = {
                m_resource->colorAnim->start,
                header.color,
                m_resource->colorAnim->end
            };

            ptcl->color = startColors[random::nextU32() % 3];
        } else {
            ptcl->color = header.color;
        }

        ptcl->visibility.baseAlpha = header.misc.baseAlpha;
        ptcl->visibility.animAlpha = 1.0f;

        if (header.flags.randomInitAngle) {
            ptcl->rotation = random::range(0.0f, glm::two_pi<f32>());
        } else {
            ptcl->rotation = header.initAngle;
        }

        if (header.flags.hasRotation) {
            ptcl->angularVelocity = random::range(header.minRotation, header.maxRotation);
        } else {
            ptcl->angularVelocity = 0;
        }

        ptcl->lifeTime = random::scaledRange(header.particleLifeTime, header.variance.lifeTime);
        ptcl->age = 0;

        if (header.flags.hasTexAnim && m_resource->texAnim) {
            const auto& texAnim = m_resource->texAnim.value();
            if (m_resource->texAnim->param.randomizeInit) {
                ptcl->texture = texAnim.textures[random::nextU32() % texAnim.param.textureCount];
            } else {
                ptcl->texture = texAnim.textures[0];
            }
        } else {
            ptcl->texture = header.misc.textureIndex;
        }
        
        ptcl->lifeRateOffset = header.flags.randomizeLoopedAnim ? random::nextF32() : 0;
    }
}

void SPLEmitter::emitChildren(const SPLParticle& parent, u32 count) {
    if (!m_resource->childResource) {
        return;
    }

    const auto& child = m_resource->childResource.value();

    for (u32 i = 0; i < count; ++i) {
        const auto ptcl = m_system->allocateParticle();
        if (!ptcl) {
            return;
        }

        m_childParticles.push_back(ptcl);

        ptcl->position = parent.position;
        ptcl->velocity = parent.velocity * child.velocityRatio + glm::vec3(
            random::aroundZero(child.randomInitVelMag),
            random::aroundZero(child.randomInitVelMag),
            random::aroundZero(child.randomInitVelMag)
        );

        ptcl->emitterPos = m_position;

        ptcl->baseScale = parent.baseScale * parent.animScale * child.scaleRatio;
        ptcl->animScale = 1.0f;

        if (child.flags.useChildColor) {
            ptcl->color = child.color;
        } else {
            ptcl->color = parent.color;
        }

        ptcl->visibility.baseAlpha = parent.visibility.baseAlpha * parent.visibility.animAlpha;
        ptcl->visibility.animAlpha = 1.0f;

        switch (child.flags.rotationType) {
        case SPLChildRotationType::None:
            ptcl->rotation = 0;
            ptcl->angularVelocity = 0;
            break;
        case SPLChildRotationType::InheritAngle:
            ptcl->rotation = parent.rotation;
            ptcl->angularVelocity = 0;
            break;
        case SPLChildRotationType::InheritAngleAndVelocity:
            ptcl->rotation = parent.rotation;
            ptcl->angularVelocity = parent.angularVelocity;
            break;
        }

        ptcl->lifeTime = child.lifeTime;
        ptcl->age = 0;

        ptcl->texture = child.misc.texture;
        ptcl->lifeRateOffset = 0;
    }
}

bool SPLEmitter::shouldTerminate() const {
    #define EITHER(a, b) ((a) || (b))

    if (m_state.looping && !m_state.terminate) {
        return false;
    }

    const auto& header = m_resource->header;
    return EITHER(
        header.flags.selfMaintaining && header.emitterLifeTime > 0 && m_state.started && m_age >= header.emitterLifeTime,
        m_state.terminate
    ) && m_particles.empty() && m_childParticles.empty();
}

void SPLEmitter::computeOrthogonalAxes() {
    constexpr glm::vec3 up{ 0, 1, 0 };
    glm::vec3 axis{};

    switch (m_resource->header.flags.emissionAxis) {
    case SPLEmissionAxis::Z:
        axis = { 0, 0, 1 };
        break;
    case SPLEmissionAxis::Y:
        axis = { 0, 1, 0 };
        break;
    case SPLEmissionAxis::X:
        axis = { 1, 0, 0 };
        break;
    case SPLEmissionAxis::Emitter:
        axis = glm::normalize(m_axis);
        break;
    }

    const f32 dot = glm::dot(up, axis);
    constexpr f32 epsilon = 0.0001f;

    glm::vec3 crossVector;
    if (glm::abs(dot) > 1.0f - epsilon) { // parallel
        // If the axis is parallel to the up vector, we can't use it for the cross product
        crossVector = { 1, 0, 0 };
    } else {
        crossVector = up;
    }

    m_crossAxis1 = glm::normalize(glm::cross(axis, crossVector));
    m_crossAxis2 = glm::normalize(glm::cross(axis, m_crossAxis1));
}

glm::vec3 SPLEmitter::tiltCoordinates(const glm::vec3& vec) const {
    const glm::vec3 axis3 = glm::normalize(glm::cross(m_crossAxis1, m_crossAxis2));
    return vec.x * m_crossAxis1 + vec.y * m_crossAxis2 + vec.z * axis3;
}
