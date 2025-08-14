#pragma once

#include "types.h"

#include <span>
#include <vector>

#include <glm/glm.hpp>

// Forward decls to avoid circular includes
class SPLParticle;
struct CameraParams;
struct SPLTexture;

// Abstract renderer interface so backends (modern/legacy GL) can be swapped.
class ParticleRenderer {
public:
    virtual ~ParticleRenderer() = default;

    virtual void begin(const glm::mat4& view, const glm::mat4& proj) = 0;
    virtual void end() = 0;

    virtual void setTextures(std::span<const SPLTexture> textures) = 0;
    virtual void setMaxInstances(u32 maxInstances) = 0;

    // Draw a single particle. Backend is responsible for implementing drawType behavior.
    virtual void renderParticle(const SPLParticle& particle, const CameraParams& params, f32 s, f32 t) = 0;
};

// -------------------- Modern OpenGL backend (existing implementation) --------------------

#include "gfx/gl_shader.h"
#include "spl/spl_resource.h"

struct ParticleInstance {
    glm::vec4 color;
    glm::mat4 transform;
    glm::vec2 texCoords[4];
};

class ModernParticleRenderer final : public ParticleRenderer {
public:
    explicit ModernParticleRenderer(u32 maxInstances, std::span<const SPLTexture> textures);

    void begin(const glm::mat4& view, const glm::mat4& proj) override;
    void end() override;

    void setTextures(std::span<const SPLTexture> textures) override;
    void setMaxInstances(u32 maxInstances) override;

    void renderParticle(const SPLParticle& particle, const CameraParams& params, f32 s, f32 t) override;

private:
    void submit(u32 texture, const ParticleInstance& instance);
    void renderBillboard(const SPLParticle& particle, const CameraParams& params, f32 s, f32 t);
    void renderDirectionalBillboard(const SPLParticle& particle, const CameraParams& params, f32 s, f32 t);

private:
    u32 m_maxInstances;
    u32 m_vao;
    u32 m_vbo;
    u32 m_ibo;
    u32 m_transformVbo;
    GLShader m_shader;

    std::span<const SPLTexture> m_textures;
    glm::mat4 m_view;
    glm::mat4 m_proj;
    s32 m_viewLocation;
    s32 m_projLocation;
    s32 m_textureLocation;
    bool m_isRendering = false;

    size_t m_particleCount = 0;
    std::vector<std::vector<ParticleInstance>> m_particles;
};

// -------------------- Legacy OpenGL backend (skeleton) --------------------

class LegacyParticleRenderer final : public ParticleRenderer {
    enum class PolygonMode {
        Modulate,
        Decal,
        Toon,
        Shadow
    };

    enum class CullMode {
        None,
        Back,
        Front,
        Both
    };

public:
    explicit LegacyParticleRenderer(u32 /*maxInstances*/, std::span<const SPLTexture> textures) { setTextures(textures); }

    void begin(const glm::mat4& view, const glm::mat4& proj) override;
    void end() override;

    void setTextures(std::span<const SPLTexture> textures) override { m_textures = textures; }
    void setMaxInstances(u32 /*maxInstances*/) override {}

    void renderParticle(const SPLParticle& particle, const CameraParams& params, f32 s, f32 t) override;

private:
    void drawXYPlane(f32 s, f32 t, f32 x, f32 y) const;
    void drawXZPlane(f32 s, f32 t, f32 x, f32 z) const;

    glm::mat4 rotateY(f32 sin, f32 cos) const;
    glm::mat4 rotateXYZ(f32 sin, f32 cos) const;

    void bindTexture(u32 textureIndex) const;

    void begin(PolygonMode polygonMode, CullMode cullMode, bool fog) const;
    void renderBillboard(const SPLParticle& particle, const CameraParams& params, f32 s, f32 t);
    void renderDirectionalBillboard(const SPLParticle& particle, const CameraParams& params, f32 s, f32 t);

private:
    std::span<const SPLTexture> m_textures;
};
