#pragma once

#include <types.h>

#include <vector>
#include <memory>
#include <array>

#include <glm/glm.hpp>


struct GeneratedMesh {
    std::vector<glm::vec3> vertices;
    std::vector<s32> indices;
};

class MeshGenerator {
public:
    static GeneratedMesh generateBox(const glm::vec3& extent);
    static GeneratedMesh generateSphere(float radius, int segments, int rings);
    static GeneratedMesh generateCylinder(float radius, float height, int segments);
    static GeneratedMesh generateHemisphere(float radius, int segments, int rings);
    static GeneratedMesh generateCircle(float radius, int segments);

    MeshGenerator() = delete;
    MeshGenerator(const MeshGenerator&) = delete;
    MeshGenerator(MeshGenerator&&) = delete;
    MeshGenerator& operator=(const MeshGenerator&) = delete;
    MeshGenerator& operator=(MeshGenerator&&) = delete;
};
