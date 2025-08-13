#pragma once

#include "spl/spl_particle.h"
#include "spl/spl_emitter.h"
#include "particle_renderer.h"

#include <glm/glm.hpp>

#include <memory>
#include <queue>
#include <vector>

struct CameraParams;

class ParticleSystem {
public:
    ParticleSystem(u32 maxParticles, std::span<const SPLTexture> textures);
    ~ParticleSystem();

    void update(float deltaTime);
    void render(const CameraParams& params);

    std::weak_ptr<SPLEmitter> addEmitter(const SPLResource& resource, bool looping = false);
    void killEmitter(const std::weak_ptr<SPLEmitter>& emitter) const;
    void killAllEmitters() const;

    SPLParticle* allocateParticle();
    void freeParticle(SPLParticle* particle);

    void setMaxParticles(u32 maxParticles);
    u32 getMaxParticles() const { return m_maxParticles; }
    u32 getParticleCount() const { return m_maxParticles - (u32)m_availableParticles.size(); }

    ParticleRenderer& getRenderer() { return *m_renderer; }
    std::span<const std::shared_ptr<SPLEmitter>> getEmitters() const { return m_emitters; }

    // Future: allow switching backend at runtime
    void useLegacyRenderer();
    void useModernRenderer();

private:
    void forceKillAllEmitters();

private:
    std::unique_ptr<ParticleRenderer> m_renderer;
    std::queue<SPLParticle*> m_availableParticles;
    std::vector<std::shared_ptr<SPLEmitter>> m_emitters;
    bool m_cycle = false;

    u32 m_maxParticles;
    SPLParticle* m_particles;
};
