#include "spl_emitter.h"
#include "editor/particle_system.h"
#include "editor/camera.h"
#include "spl_archive.h"
#include "spl_random.h"

#include <glm/gtc/random.hpp>
#include <glm/gtc/constants.hpp>
#include <ranges>


SPLEmitter::SPLEmitter(const SPLResource* resource, ParticleSystem* system, bool looping, const glm::vec3& pos) {
    m_resource = resource;
    m_system = system;
    m_state = {
        .terminate = false,
        .emissionPaused = false,
        .paused = false,
        .renderingDisabled = false,
        .started = false,
        .looping = looping,
        .frame1 = true,
        .dead = false
    };

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
            glm::pow(2.0f, resource->childResource->misc.textureTileCountS),
            glm::pow(2.0f, resource->childResource->misc.textureTileCountT)
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

SPLEmitter::~SPLEmitter() {
    for (const auto ptcl : m_particles) {
        m_system->freeParticle(ptcl);
    }

    for (const auto ptcl : m_childParticles) {
        m_system->freeParticle(ptcl);
    }

    m_particles.clear();
    m_childParticles.clear();
}

void SPLEmitter::update(float deltaTime) {
    const auto& header = m_resource->header;
    constexpr auto wrap_f32 = [](f32 x) { return x - std::floor(x); };

    if (!m_state.terminate) {
        if (m_emissionInterval == 0.0f || m_state.frame1) { // Special handling for the first frame, where lifeTime == emissionInterval
            emit((u32)header.emissionCount);
        } else {
            if (m_age <= header.emitterLifeTime) {
                while (m_emissionTimer >= m_emissionInterval) {
                    emit((u32)header.emissionCount);
                    m_emissionTimer -= m_emissionInterval;
                }
            }
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
            behavior->apply(*ptcl, acc, *this, deltaTime);
        }

        constexpr f32 fps = SPLArchive::SPL_FRAMES_PER_SECOND;

        ptcl->rotation += ptcl->angularVelocity * deltaTime * fps;

        ptcl->velocity *= glm::pow(header.misc.airResistance, deltaTime * fps);
        ptcl->velocity += acc * deltaTime * fps;

        ptcl->position += (ptcl->velocity + m_velocity) * deltaTime * fps;

        if (header.flags.hasChildResource && m_resource->childResource) {
            const auto& child = m_resource->childResource.value();
            const auto lifeRate = ptcl->age / ptcl->lifeTime;

            if (lifeRate >= child.misc.emissionDelay) {
                if (child.misc.emissionInterval == 0.0f || ptcl->age == 0.0f) {
                    emitChildren(*ptcl, child.misc.emissionCount);
                } else {
                    while (ptcl->emissionTimer >= child.misc.emissionInterval) {
                        emitChildren(*ptcl, child.misc.emissionCount);
                        ptcl->emissionTimer -= child.misc.emissionInterval;
                    }
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
                    behavior->apply(*ptcl, acc, *this, deltaTime);
                }
            }

            constexpr f32 fps = SPLArchive::SPL_FRAMES_PER_SECOND;

            ptcl->rotation += ptcl->angularVelocity * deltaTime * fps;

            ptcl->velocity *= glm::pow(header.misc.airResistance, deltaTime * fps);
            ptcl->velocity += acc * deltaTime * fps;

            ptcl->position += (ptcl->velocity + m_velocity) * deltaTime * fps;

            ptcl->age += deltaTime;
            ptcl->emissionTimer += deltaTime;

            if (ptcl->age >= ptcl->lifeTime) {
                childParticlesToRemove.push_back(ptcl);
            }
        }
    }

    m_age += deltaTime;
    m_emissionTimer += deltaTime;
    m_state.frame1 = false;

    if (m_age > header.emitterLifeTime) {
        if (m_state.looping) {
            m_age = 0;
            m_emissionTimer = 0;
            m_state.frame1 = true;
        } else {
            m_state.dead = true;
        }
    }

    for (const auto ptcl : particlesToRemove) {
        std::erase(m_particles, ptcl);
        m_system->freeParticle(ptcl);
    }

    for (const auto ptcl : childParticlesToRemove) {
        std::erase(m_childParticles, ptcl);
        m_system->freeParticle(ptcl);
    }
}

void SPLEmitter::render(const CameraParams& params) {
    auto& renderer = m_system->getRenderer();
    for (const auto ptcl : std::views::reverse(m_particles)) {
        ptcl->render(&renderer, params, m_texCoords.s, m_texCoords.t);
    }

    for (const auto ptcl : std::views::reverse(m_childParticles)) {
        ptcl->render(&renderer, params, m_childTexCoords.s, m_childTexCoords.t);
    }
}

void SPLEmitter::emit(u32 count) {
    if (m_state.dead) {
        return;
    }

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

    constexpr auto nonzero = [](f32 x) { return x == 0.0f ? FX32_F32_EPSILON : x; };

    for (u32 i = 0; i < count; ++i) {
        const auto ptcl = m_system->allocateParticle();
        if (!ptcl) {
            return;
        }

        m_particles.push_back(ptcl);
        ptcl->emitter = this;

        switch (header.flags.emissionType) {
        case SPLEmissionType::Point: {
            ptcl->position = {};
        } break;

        case SPLEmissionType::SphereSurface: {
            ptcl->position = glm::sphericalRand(nonzero(header.radius));
        } break;

        case SPLEmissionType::CircleBorder: {
            ptcl->position = tiltCoordinates({ glm::circularRand(nonzero(header.radius)), 0 });
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
            ptcl->position = glm::ballRand(nonzero(header.radius));
        } break;

        case SPLEmissionType::Circle: {
            ptcl->position = tiltCoordinates({ glm::diskRand(nonzero(header.radius)), 0 });
        } break;

        case SPLEmissionType::CylinderSurface: {
            ptcl->velocity = glm::vec3(glm::circularRand(nonzero(header.radius)), 0.0f);
            ptcl->position = tiltCoordinates({
                ptcl->velocity.x,
                ptcl->velocity.y,
                glm::linearRand(-header.length, header.length),
            });
        } break;

        case SPLEmissionType::Cylinder: {
            ptcl->position = tiltCoordinates({
                glm::diskRand(nonzero(header.radius)),
                glm::linearRand(-header.length, header.length),
            });
        } break;

        case SPLEmissionType::HemisphereSurface: {
            ptcl->position = glm::sphericalRand(nonzero(header.radius));
            const auto emitterUp = glm::cross(m_crossAxis1, m_crossAxis2);
            if (glm::dot(ptcl->position, emitterUp) <= 0) {
                ptcl->position = -ptcl->position;
            }
        } break;

        case SPLEmissionType::Hemisphere: {
            ptcl->position = glm::ballRand(nonzero(header.radius));
            const auto emitterUp = glm::cross(m_crossAxis1, m_crossAxis2);
            if (glm::dot(ptcl->position, emitterUp) <= 0) {
                ptcl->position = -ptcl->position;
            }
        } break;
        }

        const f32 magPos = SPLRandom::scaledRange2(header.initVelPosAmplifier, header.variance.initVel);
        const f32 magAxis = SPLRandom::scaledRange2(header.initVelAxisAmplifier, header.variance.initVel);

        glm::vec3 posNorm;
        if (header.flags.emissionType == SPLEmissionType::CylinderSurface) {
            posNorm = glm::normalize(ptcl->velocity.x * m_crossAxis1 + ptcl->velocity.y * m_crossAxis2);
        } else if (ptcl->position == glm::vec3(0)) {
            posNorm = SPLRandom::unitVector();
        } else {
            posNorm = glm::normalize(ptcl->position);
        }

        ptcl->velocity = posNorm * magPos + m_axis * magAxis + m_particleInitVelocity;
        ptcl->emitterPos = m_position;

        ptcl->baseScale = SPLRandom::scaledRange2(header.baseScale, header.variance.baseScale);
        ptcl->animScale = 1.0f;

        if (header.flags.hasColorAnim && m_resource->colorAnim && m_resource->colorAnim->flags.randomStartColor) {
            const glm::vec3 startColors[3] = {
                m_resource->colorAnim->start,
                header.color,
                m_resource->colorAnim->end
            };

            ptcl->color = startColors[SPLRandom::nextU32() % 3];
        } else {
            ptcl->color = header.color;
        }

        ptcl->visibility.baseAlpha = header.misc.baseAlpha;
        ptcl->visibility.animAlpha = 1.0f;

        if (header.flags.randomInitAngle) {
            ptcl->rotation = SPLRandom::range(0.0f, glm::two_pi<f32>());
        } else {
            ptcl->rotation = header.initAngle;
        }

        if (header.flags.hasRotation) {
            ptcl->angularVelocity = SPLRandom::range(header.minRotation, header.maxRotation);
        } else {
            ptcl->angularVelocity = 0;
        }

        ptcl->lifeTime = SPLRandom::scaledRange(header.particleLifeTime, header.variance.lifeTime);
        ptcl->age = 0;
        ptcl->emissionTimer = 0;

        if (header.flags.hasTexAnim && m_resource->texAnim) {
            const auto& texAnim = m_resource->texAnim.value();
            if (m_resource->texAnim->param.randomizeInit) {
                ptcl->texture = texAnim.textures[SPLRandom::nextU32() % texAnim.param.textureCount];
            } else {
                ptcl->texture = texAnim.textures[0];
            }
        } else {
            ptcl->texture = header.misc.textureIndex;
        }
        
        ptcl->lifeRateOffset = header.flags.randomizeLoopedAnim ? SPLRandom::nextF32() : 0;
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
        ptcl->emitter = this;

        ptcl->position = parent.position;
        ptcl->velocity = parent.velocity * child.velocityRatio + glm::vec3(
            SPLRandom::aroundZero(child.randomInitVelMag),
            SPLRandom::aroundZero(child.randomInitVelMag),
            SPLRandom::aroundZero(child.randomInitVelMag)
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
        ptcl->emissionTimer = 0;

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
