#include "particle_renderer.h"
#include "gfx/gl_util.h"
#include "editor/camera.h"
#include "spl/spl_particle.h"
#include "spl/spl_emitter.h"

#include <algorithm>
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/geometric.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtx/norm.hpp>
#include <numeric>
#include <ranges>
#include <spdlog/spdlog.h>

namespace {

using namespace std::string_view_literals;

constexpr f32 s_quadVertices[12] = {
    -1.0f, -1.0f, 0.0f, // bottom left
     1.0f, -1.0f, 0.0f, // bottom right
     1.0f,  1.0f, 0.0f, // top right
    -1.0f,  1.0f, 0.0f  // top left
};

constexpr u32 s_quadIndices[6] = {
    0, 1, 2,
    2, 3, 0
};

constexpr auto s_lineVertexShader = R"(
#version 450 core

layout(location = 0) in vec3 position;
layout(location = 1) in vec4 color;
layout(location = 2) in mat4 transform;
layout(location = 6) in vec2 texCoords[4];

out vec4 fragColor;
out vec2 texCoord;

uniform mat4 view;
uniform mat4 proj;

void main() {
    gl_Position = proj * view * transform * vec4(position, 1.0);
    fragColor = color;
    texCoord = texCoords[gl_VertexID];
}
)"sv;

constexpr auto s_fragmentShader = R"(
#version 450 core

layout(location = 0) out vec4 color;

in vec4 fragColor;
in vec2 texCoord;

uniform sampler2D tex;

void main() {
    vec4 outColor = fragColor * texture(tex, texCoord);
    color = outColor;
}
)"sv;

}

ModernParticleRenderer::ModernParticleRenderer(u32 maxInstances, std::span<const SPLTexture> textures)
    : m_maxInstances(maxInstances), m_shader(s_lineVertexShader, s_fragmentShader)
    , m_textures(textures), m_view(1.0f), m_proj(1.0f) {

    for (u32 i = 0; i < textures.size(); i++) {
        m_particles.emplace_back();
        m_particles.back().reserve(maxInstances / textures.size()); // Rough distribution for fewer reallocations
    }

    // Create VAO
    glCall(glGenVertexArrays(1, &m_vao));
    glCall(glBindVertexArray(m_vao));

    glCall(glGenBuffers(1, &m_vbo));
    glCall(glBindBuffer(GL_ARRAY_BUFFER, m_vbo));
    glCall(glBufferData(GL_ARRAY_BUFFER, sizeof(s_quadVertices), s_quadVertices, GL_STATIC_DRAW));

    glCall(glEnableVertexAttribArray(0));
    glCall(glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(f32), nullptr));

    glCall(glGenBuffers(1, &m_ibo));
    glCall(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ibo));
    glCall(glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(s_quadIndices), s_quadIndices, GL_STATIC_DRAW));

    glCall(glGenBuffers(1, &m_transformVbo));
    glCall(glBindBuffer(GL_ARRAY_BUFFER, m_transformVbo));
    glCall(glBufferData(GL_ARRAY_BUFFER, m_maxInstances * sizeof(ParticleInstance), nullptr, GL_DYNAMIC_DRAW));

    // Color
    glCall(glEnableVertexAttribArray(1));
    glCall(glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(ParticleInstance), nullptr));
    glCall(glVertexAttribDivisor(1, 1));

    // Transform
    for (u32 i = 0; i < 4; i++) {
        const size_t offset = offsetof(ParticleInstance, transform) + sizeof(glm::vec4) * i;
        glCall(glEnableVertexAttribArray(2 + i));
        glCall(glVertexAttribPointer(2 + i, 4, GL_FLOAT, GL_FALSE, sizeof(ParticleInstance), (void*)offset));
        glCall(glVertexAttribDivisor(2 + i, 1));
    }

    // Tex Coords
    for (u32 i = 0; i < 4; i++) {
        const size_t offset = offsetof(ParticleInstance, texCoords) + sizeof(glm::vec2) * i;
        glCall(glEnableVertexAttribArray(6 + i));
        glCall(glVertexAttribPointer(6 + i, 2, GL_FLOAT, GL_FALSE, sizeof(ParticleInstance), (void*)offset));
        glCall(glVertexAttribDivisor(6 + i, 1));
    }

    glCall(glBindVertexArray(0));

    // Get uniform locations
    m_shader.bind();
    m_viewLocation = m_shader.getUniform("view");
    m_projLocation = m_shader.getUniform("proj");
    m_textureLocation = m_shader.getUniform("tex");
    m_shader.unbind();
}

void ModernParticleRenderer::begin(const glm::mat4& view, const glm::mat4& proj) {
    for (auto& particles : m_particles) {
        particles.clear();
    }

    m_isRendering = true;
    m_particleCount = 0;
    m_view = view;
    m_proj = proj;

    // Transparent particles should blend with each other without writing depth
    // so later particles are not rejected by the depth test.
    glCall(glEnable(GL_BLEND));
    glCall(glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));
    glCall(glDepthMask(GL_FALSE));
}

void ModernParticleRenderer::end() {

    m_shader.bind();
    glCall(glActiveTexture(GL_TEXTURE0));
    glCall(glUniformMatrix4fv(m_viewLocation, 1, GL_FALSE, glm::value_ptr(m_view)));
    glCall(glUniformMatrix4fv(m_projLocation, 1, GL_FALSE, glm::value_ptr(m_proj)));
    glCall(glUniform1i(m_textureLocation, 0));
    glCall(glBindVertexArray(m_vao));

    for (u32 i = 0; i < m_particles.size(); i++) {
        if (m_particles[i].empty()) {
            continue;
        }

        glCall(glBindTexture(GL_TEXTURE_2D, m_textures[i].glTexture->getHandle()));
        glCall(glBindBuffer(GL_ARRAY_BUFFER, m_transformVbo));
        glCall(glBufferSubData(GL_ARRAY_BUFFER, 0, m_particles[i].size() * sizeof(ParticleInstance), m_particles[i].data()));
        glCall(glDrawElementsInstanced(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr, (s32)m_particles[i].size()));
    }

    glCall(glBindVertexArray(0));
    m_shader.unbind();

    // Re-enable depth writes after transparent pass
    glCall(glDepthMask(GL_TRUE));

    m_isRendering = false;
}

void ModernParticleRenderer::submit(u32 texture, const ParticleInstance& instance) {
    if (m_particleCount >= m_maxInstances) {
        return;
    }

    if (texture >= m_textures.size()) {
        spdlog::warn("Invalid texture index: {}", texture);
        texture = 0;
    }

    m_particles[texture].push_back(instance);
    ++m_particleCount;
}

void ModernParticleRenderer::renderBillboard(const SPLParticle& particle, const CameraParams& params, f32 s, f32 t) {
    const auto* resource = particle.emitter->getResource();
    glm::vec3 scale = { particle.baseScale * resource->header.aspectRatio, particle.baseScale, 1 };

    switch (resource->header.misc.scaleAnimDir) {
    case SPLScaleAnimDir::XY:
        scale.x *= particle.animScale; scale.y *= particle.animScale; break;
    case SPLScaleAnimDir::X:
        scale.x *= particle.animScale; break;
    case SPLScaleAnimDir::Y:
        scale.y *= particle.animScale; break;
    }

    const glm::vec3 particlePos = particle.emitterPos + particle.position;
    const glm::vec3 viewAxis = glm::normalize(params.pos - particlePos);

    glm::mat4 orientation(1.0f);
    orientation[0][0] = params.right.x; orientation[0][1] = params.right.y; orientation[0][2] = params.right.z; orientation[0][3] = 0.0f;
    orientation[1][0] = params.up.x;    orientation[1][1] = params.up.y;    orientation[1][2] = params.up.z;    orientation[1][3] = 0.0f;
    orientation[2][0] = viewAxis.x;     orientation[2][1] = viewAxis.y;     orientation[2][2] = viewAxis.z;     orientation[2][3] = 0.0f;

    const auto transform = glm::translate(glm::mat4(1), particlePos)
        * orientation
        * glm::rotate(glm::mat4(1), particle.rotation, { 0, 0, 1 })
        * glm::scale(glm::mat4(1), scale);

    ParticleInstance inst{};
    inst.color = glm::vec4(
        glm::mix(particle.color, resource->header.color, 0.5f),
        particle.visibility.baseAlpha * particle.visibility.animAlpha
    );
    inst.transform = transform;
    inst.texCoords[0] = { 0, t };
    inst.texCoords[1] = { s, t };
    inst.texCoords[2] = { s, 0 };
    inst.texCoords[3] = { 0, 0 };

    submit(particle.texture, inst);
}

void ModernParticleRenderer::renderDirectionalBillboard(const SPLParticle& particle, const CameraParams& params, f32 s, f32 t) {
    const auto* resource = particle.emitter->getResource();
    const auto& hdr = resource->header;
    glm::vec3 scale = { particle.baseScale * hdr.aspectRatio, particle.baseScale, 1 };

    switch (resource->header.misc.scaleAnimDir) {
    case SPLScaleAnimDir::XY:
        scale.x *= particle.animScale;
        scale.y *= particle.animScale;
        break;
    case SPLScaleAnimDir::X:
        scale.x *= particle.animScale;
        break;
    case SPLScaleAnimDir::Y:
        scale.y *= particle.animScale;
        break;
    }

    //glm::vec3 dir = glm::cross(particle.velocity, params.forward);
    //if (glm::dot(dir, dir) < 0.0001f) {
    //    return;
    //}

    //dir = glm::normalize(dir);
    //const auto velDir = glm::normalize(particle.velocity);

    //f32 dot = glm::dot(velDir, -params.forward);
    //if (dot < 0.0f) { // Particle is behind the camera
    //    dot = -dot;
    //}

    //scale.y *= (1.0f - dot) * resource->header.misc.dbbScale + 1.0f;
    //const auto pos = glm::vec4(particle.emitterPos + particle.position, 1) * params.view;
    //const auto transform = glm::mat4(
    //    dir.x * scale.x, dir.y * scale.x, 0, 0,
    //    -dir.y * scale.y, dir.x * scale.y, 0, 0,
    //    0, 0, 1, 0,
    //    pos.x, pos.y, pos.z, 1
    //);

    const glm::vec3 v = particle.velocity;
    const glm::vec3 f = params.forward;
    glm::vec3 d = glm::cross(v, f);

    if (glm::length2(d) == 0.0f) {
        return;
    }

    d = glm::normalize(d);

    const glm::vec3 y = glm::normalize(glm::cross(f, d));
    const glm::vec3 vhat = glm::length2(v) > 0.0f ? glm::normalize(v) : glm::vec3(0.0f, 0.0f, 0.0f);

    const f32 dot = std::abs(glm::dot(vhat, -f));
    const f32 dotScale = scale.y * (1.0f + (1.0f - dot) * hdr.misc.dbbScale);

    glm::mat4 mtx(1.0f);
    mtx[0] = glm::vec4(d * scale.x, 0.0f);
    mtx[1] = glm::vec4(y * dotScale, 0.0f);
    mtx[2] = glm::vec4(f, 0.0f);
    mtx[3] = glm::vec4(particle.emitterPos + particle.position, 1.0f);

    // Apply local-space polygon offset like the legacy path (drawXYPlane with x/y)
    const glm::mat4 localOffset = glm::translate(glm::mat4(1.0f), glm::vec3(hdr.polygonX, hdr.polygonY, 0.0f));

    ParticleInstance inst{};
    inst.color = glm::vec4(
        glm::mix(particle.color, hdr.color, 0.5f),
        particle.visibility.baseAlpha * particle.visibility.animAlpha
    );
    inst.transform = mtx * localOffset;
    inst.texCoords[0] = { 0, t };
    inst.texCoords[1] = { s, t };
    inst.texCoords[2] = { s, 0 };
    inst.texCoords[3] = { 0, 0 };

    submit(particle.texture, inst);
}

void ModernParticleRenderer::renderPolygon(const SPLParticle& particle, const CameraParams& params, f32 s, f32 t) {
    const auto resource = particle.emitter->getResource();
    const auto& hdr = resource->header;

    glm::vec3 rotAxis;

    if (hdr.flags.polygonRotAxis == SPLPolygonRotAxis::Y) {
        rotAxis = { 0, 1, 0 };
    } else if (hdr.flags.polygonRotAxis == SPLPolygonRotAxis::XYZ) {
        rotAxis = { 1, 1, 1 };
    }

    glm::vec3 scale = { particle.baseScale * hdr.aspectRatio, particle.baseScale, 1 };

    switch (hdr.misc.scaleAnimDir) {
    case SPLScaleAnimDir::XY:
        scale.x *= particle.animScale;
        scale.y *= particle.animScale;
        break;
    case SPLScaleAnimDir::X:
        scale.x *= particle.animScale;
        break;
    case SPLScaleAnimDir::Y:
        scale.y *= particle.animScale;
        break;
    }

    glm::mat4 rot = glm::rotate(glm::mat4(1), particle.rotation, rotAxis);
    if (hdr.flags.polygonReferencePlane == 1) { // XZ plane
        rot = glm::rotate(rot, glm::half_pi<f32>(), { 1, 0, 0 });
    }

    const auto pos = particle.emitterPos + particle.position;
    const auto transform = glm::translate(glm::mat4(1), pos)
        * rot
        * glm::scale(glm::mat4(1), scale);

    ParticleInstance inst{};
    inst.color = glm::vec4(
        glm::mix(particle.color, hdr.color, 0.5f),
        particle.visibility.baseAlpha * particle.visibility.animAlpha
    );
    inst.transform = transform;
    inst.texCoords[0] = { 0, t };
    inst.texCoords[1] = { s, t };
    inst.texCoords[2] = { s, 0 };
    inst.texCoords[3] = { 0, 0 };

    submit(particle.texture, inst);
}

void ModernParticleRenderer::renderDirectionalPolygon(const SPLParticle& particle, const CameraParams& params, f32 s, f32 t) {
    const auto resource = particle.emitter->getResource();
    const auto& hdr = resource->header;

    glm::vec3 rotAxis;
    if (hdr.flags.polygonRotAxis == SPLPolygonRotAxis::Y) {
        rotAxis = { 0, 1, 0 };
    } else if (hdr.flags.polygonRotAxis == SPLPolygonRotAxis::XYZ) {
        rotAxis = { 1, 1, 1 };
    }

    glm::vec3 scale = { particle.baseScale * hdr.aspectRatio, particle.baseScale, 1.0f };

    switch (hdr.misc.scaleAnimDir) {
    case SPLScaleAnimDir::XY:
        scale.x *= particle.animScale;
        scale.y *= particle.animScale;
        break;
    case SPLScaleAnimDir::X:
        scale.x *= particle.animScale;
        break;
    case SPLScaleAnimDir::Y:
        scale.y *= particle.animScale;
        break;
    }

    glm::vec3 facingDir = hdr.misc.dpolFaceEmitter
        ? -glm::normalize(particle.position)
        : glm::normalize(particle.velocity);

    glm::vec3 axis(0, 1, 0);

    const auto dot = glm::dot(facingDir, axis);
    if (dot > 0.8f || dot < -0.8f) {
        // Facing up or down, use XZ plane
        axis = { 1, 0, 0 };
    }

    const auto dir1 = glm::cross(facingDir, axis);
    const auto dir2 = glm::cross(facingDir, dir1);

    glm::mat4 dirRot(
        dir1.x, dir1.y, dir1.z, 0.0f,
        facingDir.x, facingDir.y, facingDir.z, 0.0f,
        dir2.x, dir2.y, dir2.z, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    );

    glm::mat4 rot = glm::rotate(glm::mat4(1), particle.rotation, rotAxis) * dirRot;
    if (hdr.flags.polygonReferencePlane == 1) { // XZ plane
        rot = glm::rotate(rot, glm::half_pi<f32>(), { 1, 0, 0 });
    }

    const auto pos = particle.emitterPos + particle.position;
    const auto transform = glm::translate(glm::mat4(1), pos)
        * rot
        * glm::scale(glm::mat4(1), scale);

    const auto color = glm::mix(particle.color, hdr.color, 0.5f);

    ParticleInstance inst{};
    inst.color = glm::vec4(color, particle.visibility.baseAlpha * particle.visibility.animAlpha);
    inst.transform = transform;
    inst.texCoords[0] = { 0, t };
    inst.texCoords[1] = { s, t };
    inst.texCoords[2] = { s, 0 };
    inst.texCoords[3] = { 0, 0 };

    submit(particle.texture, inst);
}

void ModernParticleRenderer::setTextures(std::span<const SPLTexture> textures) {
    if (m_isRendering) {
        throw std::runtime_error("Cannot set textures while rendering");
    }

    m_textures = textures;
    m_particles.clear();

    for (u32 i = 0; i < textures.size(); i++) {
        m_particles.emplace_back().reserve(m_maxInstances / textures.size()); // Rough distribution for fewer reallocations
    }
}

void ModernParticleRenderer::setMaxInstances(u32 maxInstances) {
    if (m_isRendering) {
        throw std::runtime_error("Cannot set max instances while rendering");
    }

    m_maxInstances = maxInstances;
    glCall(glBindBuffer(GL_ARRAY_BUFFER, m_transformVbo));
    glCall(glBufferData(GL_ARRAY_BUFFER, m_maxInstances * sizeof(ParticleInstance), nullptr, GL_DYNAMIC_DRAW));
}

void ModernParticleRenderer::renderParticle(const SPLParticle& particle, const CameraParams& params, f32 s, f32 t) {
    const auto* resource = particle.emitter->getResource();
    const auto& drawType = resource->header.flags.drawType;

    switch (drawType) {
    case SPLDrawType::Billboard:
        renderBillboard(particle, params, s, t);
        break;
    case SPLDrawType::DirectionalBillboard:
        renderDirectionalBillboard(particle, params, s, t);
        break;
    case SPLDrawType::Polygon:
        renderPolygon(particle, params, s, t);
        break;
    case SPLDrawType::DirectionalPolygon: [[fallthrough]];
    case SPLDrawType::DirectionalPolygonCenter:
        renderDirectionalPolygon(particle, params, s, t);
        break;
    }
}

void LegacyParticleRenderer::begin(const glm::mat4& view, const glm::mat4& proj) {
    // Load camera projection and view matrices
    glCall(glMatrixMode(GL_PROJECTION));
    glCall(glPushMatrix());
    glCall(glLoadMatrixf(glm::value_ptr(proj)));

    glCall(glMatrixMode(GL_MODELVIEW));
    glCall(glPushMatrix());
    glCall(glLoadMatrixf(glm::value_ptr(view)));

    glCall(glActiveTexture(GL_TEXTURE0));
    glCall(glEnable(GL_TEXTURE_2D));
}

void LegacyParticleRenderer::end() {
    // Restore matrices
    glCall(glMatrixMode(GL_MODELVIEW));
    glCall(glPopMatrix());
    glCall(glMatrixMode(GL_PROJECTION));
    glCall(glPopMatrix());
}

void LegacyParticleRenderer::renderParticle(const SPLParticle& particle, const CameraParams& params, f32 s, f32 t) {
    bindTexture(particle.texture);

    const auto* resource = particle.emitter->getResource();
    const auto drawType = resource->header.flags.drawType;

    switch (drawType) {
    case SPLDrawType::Billboard:
        renderBillboard(particle, params, s, t);
        break;
    case SPLDrawType::DirectionalBillboard:
        renderDirectionalBillboard(particle, params, s, t);
        break;
    case SPLDrawType::Polygon:
        renderPolygon(particle, params, s, t);
        break;
    case SPLDrawType::DirectionalPolygon: [[fallthrough]];
    case SPLDrawType::DirectionalPolygonCenter:
        renderDirectionalPolygon(particle, params, s, t);
        break;
    }
}

void LegacyParticleRenderer::drawXYPlane(f32 s, f32 t, f32 x, f32 y) const {
    begin(PolygonMode::Modulate, CullMode::None, false);

    glTexCoord2f(0, 0);
    glVertex3f(x - 1.0f, y + 1.0f, 0.0f); // Top left

    glTexCoord2f(s, 0);
    glVertex3f(x + 1.0f, y + 1.0f, 0.0f); // Top right

    glTexCoord2f(s, t);
    glVertex3f(x + 1.0f, y - 1.0f, 0.0f); // Bottom right

    glTexCoord2f(0, t);
    glVertex3f(x - 1.0f, y - 1.0f, 0.0f); // Bottom left

    glEnd();
}

void LegacyParticleRenderer::drawXZPlane(f32 s, f32 t, f32 x, f32 z) const {
    begin(PolygonMode::Modulate, CullMode::None, false);

    glTexCoord2f(0, 0);
    glVertex3f(x - 1.0f, 0.0f, z + 1.0f); // Top left

    glTexCoord2f(s, 0);
    glVertex3f(x + 1.0f, 0.0f, z + 1.0f); // Top right

    glTexCoord2f(s, t);
    glVertex3f(x + 1.0f, 0.0f, z - 1.0f); // Bottom right

    glTexCoord2f(0, t);
    glVertex3f(x - 1.0f, 0.0f, z - 1.0f); // Bottom left

    glEnd();
}

glm::mat4 LegacyParticleRenderer::rotate(SPLPolygonRotAxis axis, f32 sin, f32 cos) const {
    switch (axis) {
    case SPLPolygonRotAxis::Y:
        return rotateY(sin, cos);
    case SPLPolygonRotAxis::XYZ:
        return rotateXYZ(sin, cos);
    }

    return glm::identity<glm::mat4>();
}

glm::mat4 LegacyParticleRenderer::rotateY(f32 sin, f32 cos) const {
    return {
        cos, 0, sin, 0,
        0, 1, 0, 0,
        -sin, 0, cos, 0,
        0, 0, 0, 1
    };
}

glm::mat4 LegacyParticleRenderer::rotateXYZ(f32 sin, f32 cos) const {
    f32 C = (1.0f - cos) / 3.0f;
    const f32 Sm = C + sin * glm::sqrt(1.0f / 3.0f);
    const f32 Sp = C - sin * glm::sqrt(1.0f / 3.0f);
    C += cos;

    return {
        C, Sm, Sp, 0,
        Sp, C, Sm, 0,
        Sm, Sp, C, 0,
        0, 0, 0, 1
    };
}

void LegacyParticleRenderer::bindTexture(u32 textureIndex) const {
    const auto& texture = m_textures[textureIndex];
    if (!texture.glTexture) {
        spdlog::warn("Attempted to bind invalid texture at index {}", textureIndex);
        return;
    }

    glCall(glBindTexture(GL_TEXTURE_2D, texture.glTexture->getHandle()));
    glCall(glMatrixMode(GL_TEXTURE));
    glCall(glLoadIdentity());
    glCall(glScalef(1.0f, 1.0f, 1.0f));
    glCall(glMatrixMode(GL_MODELVIEW));
}

void LegacyParticleRenderer::begin(PolygonMode polygonMode, CullMode cullMode, bool fog) const {
    switch (polygonMode) {
    case PolygonMode::Modulate:
    case PolygonMode::Toon:
    case PolygonMode::Shadow:
        glCall(glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE));
        break;
    case PolygonMode::Decal:
        glCall(glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_DECAL));
        break;
    }

    if (cullMode == CullMode::None) {
        glCall(glDisable(GL_CULL_FACE));
    } else {
        glCall(glEnable(GL_CULL_FACE));
        switch (cullMode) {
        case CullMode::Back:
            glCall(glCullFace(GL_BACK));
            break;
        case CullMode::Front:
            glCall(glCullFace(GL_FRONT));
            break;
        case CullMode::Both:
            glCall(glCullFace(GL_FRONT_AND_BACK));
            break;
        default:
            break;
        }
    }

    if (fog) {
        glCall(glEnable(GL_FOG));
    } else {
        glCall(glDisable(GL_FOG));
    }

    // Ensure no VAO or shader program is bound
    glBindVertexArray(0);
    glUseProgram(0);

    glBegin(GL_QUADS);
}

void LegacyParticleRenderer::renderBillboard(const SPLParticle& particle, const CameraParams& params, f32 s, f32 t) {
    const auto& resource = particle.emitter->getResource();
    const auto& misc = resource->header.misc;

    glm::vec3 scale = { particle.baseScale * resource->header.aspectRatio, particle.baseScale, 1.0f };

    switch (misc.scaleAnimDir) {
    case SPLScaleAnimDir::XY:
        scale.x *= particle.animScale;
        scale.y *= particle.animScale;
        break;
    case SPLScaleAnimDir::X:
        scale.x *= particle.animScale;
        break;
    case SPLScaleAnimDir::Y:
        scale.y *= particle.animScale;
        break;
    }

    // Get world position of the particle
    const glm::vec3 particlePos = particle.emitterPos + particle.position;

    // Create proper billboard orientation using camera vectors
    const auto sin = glm::sin(particle.rotation);
    const auto cos = glm::cos(particle.rotation);

    // Create rotation matrix for particle rotation around view axis
    glm::mat4 rotationMatrix(1.0f);
    rotationMatrix[0] = glm::vec4(cos * params.right + sin * params.up, 0.0f);
    rotationMatrix[1] = glm::vec4(-sin * params.right + cos * params.up, 0.0f);
    rotationMatrix[2] = glm::vec4(params.forward, 0.0f); // View direction
    rotationMatrix[3] = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);

    rotationMatrix[0] *= scale.x;
    rotationMatrix[1] *= scale.y;

    glm::mat4 mtx = glm::translate(glm::mat4(1.0f), particlePos) * rotationMatrix;

    const auto color = glm::mix(particle.color, resource->header.color, 0.5f);

    // Apply the transformation
    glCall(glMatrixMode(GL_MODELVIEW));
    glCall(glPushMatrix());
    glCall(glMultMatrixf(glm::value_ptr(mtx)));
    glCall(glColor4f(color.r, color.g, color.b, particle.visibility.baseAlpha * particle.visibility.animAlpha));

    drawXYPlane(s, t, resource->header.polygonX, resource->header.polygonY);

    glCall(glPopMatrix());
}

void LegacyParticleRenderer::renderDirectionalBillboard(const SPLParticle& particle, const CameraParams& params, f32 s, f32 t) {
    const auto& resource = particle.emitter->getResource();
    const auto& hdr = resource->header;
    const auto& misc = hdr.misc;

    float sclY = particle.baseScale;
    float sclX = sclY * hdr.aspectRatio;

    switch (misc.scaleAnimDir) {
    case SPLScaleAnimDir::XY:
        sclX *= particle.animScale;
        sclY *= particle.animScale;
        break;
    case SPLScaleAnimDir::X:
        sclX *= particle.animScale;
        break;
    case SPLScaleAnimDir::Y:
        sclY *= particle.animScale;
        break;
    }

    const glm::vec3 v = particle.velocity;
    const glm::vec3 f = params.forward;
    glm::vec3 d = glm::cross(v, f);

    if (glm::length2(d) == 0.0f) {
        return;
    }

    d = glm::normalize(d);

    const glm::vec3 y = glm::normalize(glm::cross(f, d));
    const glm::vec3 vhat = glm::length2(v) > 0.0f ? glm::normalize(v) : glm::vec3(0.0f, 0.0f, 0.0f);

    const f32 dot = std::abs(glm::dot(vhat, -f));
    const f32 dotScale = sclY * (1.0f + (1.0f - dot) * misc.dbbScale);

    glm::mat4 mtx(1.0f);
    mtx[0] = glm::vec4(d * sclX, 0.0f);
    mtx[1] = glm::vec4(y * dotScale, 0.0f);
    mtx[2] = glm::vec4(f, 0.0f);
    mtx[3] = glm::vec4(particle.emitterPos + particle.position, 1.0f);

    glCall(glMatrixMode(GL_MODELVIEW));
    glCall(glPushMatrix());
    glCall(glMultMatrixf(glm::value_ptr(mtx)));

    const auto color = glm::mix(particle.color, resource->header.color, 0.5f);

    glCall(glColor4f(color.r, color.g, color.b,
        particle.visibility.baseAlpha * particle.visibility.animAlpha));

    drawXYPlane(s, t, hdr.polygonX, hdr.polygonY);

    glCall(glPopMatrix());
}

void LegacyParticleRenderer::renderPolygon(const SPLParticle& particle, const CameraParams& params, f32 s, f32 t) {
    const auto resource = particle.emitter->getResource();
    const auto& hdr = resource->header;

    const auto rot = rotate(hdr.flags.polygonRotAxis, glm::sin(particle.rotation), glm::cos(particle.rotation));

    glm::vec3 scale = { particle.baseScale * hdr.aspectRatio, particle.baseScale, 1.0f };

    switch (hdr.misc.scaleAnimDir) {
    case SPLScaleAnimDir::XY:
        scale.x *= particle.animScale;
        scale.y *= particle.animScale;
        break;
    case SPLScaleAnimDir::X:
        scale.x *= particle.animScale;
        break;
    case SPLScaleAnimDir::Y:
        scale.y *= particle.animScale;
        break;
    }

    const auto pos = particle.emitterPos + particle.position;

    const glm::mat4 transform = glm::translate(glm::mat4(1), pos)
        * rot
        * glm::scale(glm::mat4(1), scale);

    const auto color = glm::mix(particle.color, hdr.color, 0.5f);

    glCall(glMatrixMode(GL_MODELVIEW));
    glCall(glPushMatrix());
    glCall(glMultMatrixf(glm::value_ptr(transform)));
    glCall(glColor4f(color.r, color.g, color.b, particle.visibility.baseAlpha * particle.visibility.animAlpha));

    if (hdr.flags.polygonReferencePlane == 1) { // XZ plane
        drawXZPlane(s, t, hdr.polygonX, hdr.polygonY);
    } else {
        drawXYPlane(s, t, hdr.polygonX, hdr.polygonY);
    }

    glCall(glPopMatrix());
}

void LegacyParticleRenderer::renderDirectionalPolygon(const SPLParticle& particle, const CameraParams& params, f32 s, f32 t) {
    const auto resource = particle.emitter->getResource();
    const auto& hdr = resource->header;

    glm::mat4 rot = rotate(hdr.flags.polygonRotAxis, glm::sin(particle.rotation), glm::cos(particle.rotation));

    glm::vec3 scale = { particle.baseScale * hdr.aspectRatio, particle.baseScale, 1.0f };

    switch (hdr.misc.scaleAnimDir) {
    case SPLScaleAnimDir::XY:
        scale.x *= particle.animScale;
        scale.y *= particle.animScale;
        break;
    case SPLScaleAnimDir::X:
        scale.x *= particle.animScale;
        break;
    case SPLScaleAnimDir::Y:
        scale.y *= particle.animScale;
        break;
    }

    glm::vec3 facingDir = hdr.misc.dpolFaceEmitter
        ? -glm::normalize(particle.position)
        : glm::normalize(particle.velocity);

    glm::vec3 axis(0, 1, 0);

    const auto dot = glm::dot(facingDir, axis);
    if (dot > 0.8f || dot < -0.8f) {
        // Facing up or down, use XZ plane
        axis = { 1, 0, 0 };
    }

    const auto dir1 = glm::cross(facingDir, axis);
    const auto dir2 = glm::cross(facingDir, dir1);

    glm::mat4 dirRot(
        dir1.x, dir1.y, dir1.z, 0.0f,
        facingDir.x, facingDir.y, facingDir.z, 0.0f,
        dir2.x, dir2.y, dir2.z, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    );

    rot = rot * dirRot;

    const auto pos = particle.emitterPos + particle.position;
    const auto transform = glm::translate(glm::mat4(1), pos)
        * rot
        * glm::scale(glm::mat4(1), scale);

    const auto color = glm::mix(particle.color, hdr.color, 0.5f);

    glCall(glMatrixMode(GL_MODELVIEW));
    glCall(glPushMatrix());
    glCall(glMultMatrixf(glm::value_ptr(transform)));
    glCall(glColor4f(color.r, color.g, color.b, particle.visibility.baseAlpha * particle.visibility.animAlpha));

    if (hdr.flags.polygonReferencePlane == 1) { // XZ plane
        drawXZPlane(s, t, hdr.polygonX, hdr.polygonY);
    } else {
        drawXYPlane(s, t, hdr.polygonX, hdr.polygonY);
    }

    glCall(glPopMatrix());
}
