#include "spl_particle.h"
#include "editor/camera.h"
#include "editor/particle_renderer.h"
#include "spl_emitter.h"

void SPLParticle::render(ParticleRenderer* renderer, const CameraParams& params, f32 s, f32 t) const {
    renderer->renderParticle(*this, params, s, t);
}

glm::vec3 SPLParticle::getWorldPosition() const {
    return emitterPos + position;
}
