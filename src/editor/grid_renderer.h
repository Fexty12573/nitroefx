#pragma once

#include "renderer.h"
#include "gfx/gl_util.h"
#include "gfx/gl_shader.h"
#include <types.h>

#include <glm/glm.hpp>
#include <vector>


class GridRenderer : public Renderer {
public:
    GridRenderer(const glm::ivec2& dimensions, const glm::vec2& spacing);

    void setHeight(f32 height) { m_height = height; }
    void setColor(const glm::vec4& color) { m_color = color; }

    void render(const glm::mat4& view, const glm::mat4& proj) override;

private:
    void createGrid();

private:
    glm::ivec2 m_dimensions;
    glm::vec2 m_spacing;
    std::vector<glm::vec3> m_vertices;

    f32 m_height = 0.0f;
    glm::vec4 m_color = glm::vec4(1.0f, 1.0f, 1.0f, 0.2f);

    GLShader m_shader;

    u32 m_vao = 0;
    u32 m_vbo = 0;

    u32 m_viewLocation = 0;
    u32 m_projLocation = 0;
    u32 m_heightLocation = 0;
    u32 m_colorLocation = 0;
};
