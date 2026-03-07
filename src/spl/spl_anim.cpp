#include "spl_resource.h"
#include "spl_particle.h"
#include "spl_random.h"

#include <glm/common.hpp>



void SPLScaleAnim::apply(SPLParticle& ptcl, const SPLResource& resource, f32 lifeRate) const {
    const f32 in = curve.getIn();
    const f32 out = curve.getOut();

    if (lifeRate < in) {
        ptcl.animScale = glm::mix(start, mid, lifeRate / in);
    } else if (lifeRate < out) {
        ptcl.animScale = mid;
    } else {
        ptcl.animScale = glm::mix(mid, end, (lifeRate - out) / (1.0f - out));
    }
}

void SPLScaleAnim::plot(std::span<f32> xs, std::span<f32> ys) const {
    const size_t samples = std::min(xs.size(), ys.size());
    for (size_t i = 0; i < samples; i++) {
        const f32 lifeRate = (f32)i / (f32)samples;
        xs[i] = lifeRate;

        if (lifeRate < curve.getIn()) {
            ys[i] = glm::mix(start, mid, lifeRate / curve.getIn());
        } else if (lifeRate < curve.getOut()) {
            ys[i] = mid;
        } else {
            ys[i] = glm::mix(mid, end, (lifeRate - curve.getOut()) / (1.0f - curve.getOut()));
        }
    }
}

void SPLColorAnim::apply(SPLParticle& ptcl, const SPLResource& resource, f32 lifeRate) const {
    const float in = curve.getIn();
    const float peak = curve.getPeak();
    const float out = curve.getOut();

    if (lifeRate < in) {
        ptcl.color = start;
    } else if (lifeRate < peak) {
        if (flags.interpolate) {
            ptcl.color = glm::mix(start, resource.header.color, (lifeRate - in) / (peak - in));
        } else {
            ptcl.color = resource.header.color;
        }
    } else if (lifeRate < out) {
        if (flags.interpolate) {
            ptcl.color = glm::mix(resource.header.color, end, (lifeRate - peak) / (out - peak));
        } else {
            ptcl.color = end;
        }
    } else {
        ptcl.color = end;
    }
}

void SPLColorAnim::plot(const SPLResource& resource, std::span<f32> xs, std::span<glm::vec3> ys) const {
    const float in = curve.getIn();
    const float peak = curve.getPeak();
    const float out = curve.getOut();

    const size_t samples = std::min(xs.size(), ys.size());
    for (size_t i = 0; i < samples; i++) {
        const f32 lifeRate = (f32)i / (f32)samples;
        xs[i] = lifeRate;

        if (lifeRate < in) {
            ys[i] = start;
        } else if (lifeRate < peak) {
            if (flags.interpolate) {
                ys[i] = glm::mix(start, resource.header.color, (lifeRate - in) / (peak - in));
            } else {
                ys[i] = resource.header.color;
            }
        } else if (lifeRate < out) {
            if (flags.interpolate) {
                ys[i] = glm::mix(resource.header.color, end, (lifeRate - peak) / (out - peak));
            } else {
                ys[i] = end;
            }
        } else {
            ys[i] = end;
        }
    }
}

void SPLAlphaAnim::apply(SPLParticle& ptcl, const SPLResource& resource, f32 lifeRate) const {
    const f32 in = curve.getIn();
    const f32 out = curve.getOut();

    if (lifeRate < in) {
        ptcl.visibility.animAlpha = glm::mix(alpha.start, alpha.mid, lifeRate / in);
    } else if (lifeRate < out) {
        ptcl.visibility.animAlpha = alpha.mid;
    } else {
        ptcl.visibility.animAlpha = glm::mix(alpha.mid, alpha.end, (lifeRate - out) / (1.0f - out));
    }

    ptcl.visibility.animAlpha = glm::clamp(
        SPLRandom::scaledRange(ptcl.visibility.animAlpha, flags.randomRange), 
        0.0f, 1.0f
    );
}

void SPLAlphaAnim::plot(std::span<f32> xs, std::span<f32> ys) const {
    plotWith(xs, ys, flags.randomRange);
}

void SPLAlphaAnim::plotWith(std::span<f32> xs, std::span<f32> ys, float variance) const {
    const size_t samples = std::min(xs.size(), ys.size());
    for (size_t i = 0; i < samples; i++) {
        const f32 lifeRate = (f32)i / (f32)samples;
        xs[i] = lifeRate;

        if (lifeRate < curve.getIn()) {
            ys[i] = glm::mix(alpha.start, alpha.mid, lifeRate / curve.getIn());
        }
        else if (lifeRate < curve.getOut()) {
            ys[i] = alpha.mid;
        }
        else {
            ys[i] = glm::mix(alpha.mid, alpha.end, (lifeRate - curve.getOut()) / (1.0f - curve.getOut()));
        }

        ys[i] = SPLRandom::scaledRange(ys[i], variance);
    }
}

void SPLAlphaAnim::plotWith(std::span<f32> xs, std::span<f32> ys, float variance, u8 factor) const {
    const size_t samples = std::min(xs.size(), ys.size());
    for (size_t i = 0; i < samples; i++) {
        const f32 lifeRate = (f32)i / (f32)samples;
        xs[i] = lifeRate;

        if (lifeRate < curve.getIn()) {
            ys[i] = glm::mix(alpha.start, alpha.mid, lifeRate / curve.getIn());
        }
        else if (lifeRate < curve.getOut()) {
            ys[i] = alpha.mid;
        }
        else {
            ys[i] = glm::mix(alpha.mid, alpha.end, (lifeRate - curve.getOut()) / (1.0f - curve.getOut()));
        }

        ys[i] = SPLRandom::scaledRange(ys[i], variance, factor);
    }
}

void SPLTexAnim::apply(SPLParticle& ptcl, const SPLResource& resource, f32 lifeRate) const {
    
    for (int i = 0; i < param.textureCount; i++) {
        if (lifeRate < param.step * (i + 1)) {
            ptcl.texture = textures[i];
            break;
        }
    }
}

void SPLChildResource::applyScaleAnim(SPLParticle& ptcl, f32 lifeRate) const {
    ptcl.animScale = glm::mix(0.0f, endScale, lifeRate); // scale up
}

void SPLChildResource::applyAlphaAnim(SPLParticle& ptcl, f32 lifeRate) const {
    ptcl.visibility.animAlpha = glm::mix(1.0f, 0.0f, lifeRate); // fade out
}
