#pragma once

#include "renderer.h"
#include "mesh_generator.h"
#include "gfx/gl_util.h"
#include "gfx/gl_shader.h"
#include <types.h>

#include <glm/glm.hpp>
#include <vector>


class DebugRenderer : public Renderer {
public:
    DebugRenderer(u32 maxLines, u32 maxBoxes = 64, u32 maxSpheres = 64, u32 maxCylinders = 64, u32 maxHemispheres = 64);

    void render(const glm::mat4& view, const glm::mat4& proj) override;

    void addLine(const glm::vec3& start, const glm::vec3& end, const glm::vec4& color = glm::vec4(1.0f));
    void addPlane(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b, const glm::vec4& color = glm::vec4(1.0f));
    void addBox(const glm::vec3& pos, const glm::vec3& scale, const glm::vec4& color);
    void addSphere(const glm::vec3& center, f32 radius, const glm::vec4& color);
    void addCircle(const glm::vec3& center, const glm::vec3& normal, f32 radius, const glm::vec4& color);
    void addCylinder(const glm::vec3& center, const glm::vec3& axis, f32 length, f32 radius, const glm::vec4& color);
    void addHemisphere(const glm::vec3& center, const glm::vec3& axis, f32 radius, const glm::vec4& color);

private:
    struct Vertex {
        glm::vec3 pos;
        f32 pad;
        glm::vec4 color;
    };

    struct ObjectInstace {
        glm::vec4 color;
        glm::mat4 transform;
    };

    struct ObjectRenderData {
        u32 vao = 0;
        u32 vbo = 0;
        u32 ibo = 0;
        u32 instanceVbo = 0;

        u32 indexCount = 0;
        u32 maxInstances = 0;
        std::vector<ObjectInstace> instances;

        void init(const GeneratedMesh& mesh, u32 maxInstances);
        void render();
    };

    GLShader m_lineShader;
    GLShader m_objectShader;

    std::vector<Vertex> m_vertices;

    ObjectRenderData m_boxRenderData;
    ObjectRenderData m_sphereRenderData;
    ObjectRenderData m_cylinderRenderData;
    ObjectRenderData m_hemisphereRenderData;

    u32 m_lineVao = 0;
    u32 m_lineVbo = 0;

    u32 m_viewLocation = 0;
    u32 m_projLocation = 0;

    u32 m_maxLines = 0;


    // struct Sphere {
    //     glm::vec3 center;
    //     f32 radius;
    //     glm::vec3 color;
    // };

    // struct Circle {
    //     glm::vec3 center;
    //     glm::vec3 normal;
    //     f32 radius;
    //     glm::vec3 color;
    // };

    // struct Cylinder {
    //     glm::vec3 start;
    //     glm::vec3 end;
    //     f32 radius;
    //     glm::vec3 color;
    // };
};
