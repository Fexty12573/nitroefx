#pragma once

#include <types.h>

#include <glm/glm.hpp>


class GLViewport {
public:
    GLViewport(const glm::vec2& size);

    void bind();
    void unbind();

    void resize(const glm::vec2& size, bool nearestFiltering = false);

    glm::vec2 getSize() const {
        return m_size;
    }

    u32 getTexture() const {
        return m_texture;
    }

private:
    void createFramebuffer();

private:
    glm::vec2 m_size;
    u32 m_fbo = 0;
    u32 m_texture = 0;
    u32 m_rbo = 0;
};
