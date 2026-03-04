#pragma once

#include <types.h>
#include <glm/glm.hpp>


typedef s32 fx32;
typedef s16 fx16;

#define FX32_SHIFT          12
#define FX32_INT_SIZE       19
#define FX32_DEC_SIZE       12

#define FX32_INT_MASK       0x7ffff000
#define FX32_DEC_MASK       0x00000fff
#define FX32_SIGN_MASK      0x80000000

#define FX32_MAX            ((fx32)0x7fffffff)
#define FX32_MIN            ((fx32)0x80000000)
#define FX32_EPSILON        ((fx32)0x00000001)
#define FX32_F32_EPSILON    (FX_FX32_TO_F32(FX32_EPSILON))

#define FX_FX32_TO_F32(x)    ((f32)((x) / (f32)(1 << FX32_SHIFT)))
#define FX_F32_TO_FX32(x)    ((fx32)(((x) > 0) ? \
                                     ((x) * (1 << FX32_SHIFT) + 0.5f) : \
                                     ((x) * (1 << FX32_SHIFT) - 0.5f)))
#define FX32_CONST(x)        FX_F32_TO_FX32(x)


#define FX16_SHIFT          12
#define FX16_INT_SIZE       3
#define FX16_DEC_SIZE       12

#define FX16_INT_MASK       0x7000
#define FX16_DEC_MASK       0x0fff
#define FX16_SIGN_MASK      0x8000

#define FX16_MAX            ((fx16)0x7fff)
#define FX16_MIN            ((fx16)0x8000)

#define FX_FX16_TO_F32(x)    ((f32)((x) / (f32)(1 << FX16_SHIFT)))
#define FX_F32_TO_FX16(x)    ((fx16)(((x) > 0) ? \
                                     (fx16)((x) * (1 << FX16_SHIFT) + 0.5f) : \
                                     (fx16)((x) * (1 << FX16_SHIFT) - 0.5f)))
#define FX16_CONST(x)       FX_F32_TO_FX16(x)


struct VecFx32 {
    fx32 x;
    fx32 y;
    fx32 z;

    VecFx32(const glm::vec3& vec) {
        x = FX_F32_TO_FX32(vec.x);
        y = FX_F32_TO_FX32(vec.y);
        z = FX_F32_TO_FX32(vec.z);
    }

    VecFx32(const glm::vec4& vec) {
        x = FX_F32_TO_FX32(vec.x);
        y = FX_F32_TO_FX32(vec.y);
        z = FX_F32_TO_FX32(vec.z);
    }

    VecFx32() {
        x = 0;
        y = 0;
        z = 0;
    }

    glm::vec3 toVec3() const {
        return glm::vec3(FX_FX32_TO_F32(x), FX_FX32_TO_F32(y), FX_FX32_TO_F32(z));
    }

    glm::vec4 toVec4() const {
        return glm::vec4(FX_FX32_TO_F32(x), FX_FX32_TO_F32(y), FX_FX32_TO_F32(z), 1.0f);
    }

    operator glm::vec3() const {
        return toVec3();
    }

    operator glm::vec4() const {
        return toVec4();
    }
};

struct VecFx16 {
    fx16 x;
    fx16 y;
    fx16 z;

    VecFx16(const glm::vec3& vec) {
        x = FX_F32_TO_FX16(vec.x);
        y = FX_F32_TO_FX16(vec.y);
        z = FX_F32_TO_FX16(vec.z);
    }

    VecFx16(const glm::vec4& vec) {
        x = FX_F32_TO_FX16(vec.x);
        y = FX_F32_TO_FX16(vec.y);
        z = FX_F32_TO_FX16(vec.z);
    }

    VecFx16() {
        x = 0;
        y = 0;
        z = 0;
    }

    glm::vec3 toVec3() const {
        return glm::vec3(FX_FX16_TO_F32(x), FX_FX16_TO_F32(y), FX_FX16_TO_F32(z));
    }

    glm::vec4 toVec4() const {
        return glm::vec4(FX_FX16_TO_F32(x), FX_FX16_TO_F32(y), FX_FX16_TO_F32(z), 1.0f);
    }

    operator glm::vec3() const {
        return toVec3();
    }

    operator glm::vec4() const {
        return toVec4();
    }
};
