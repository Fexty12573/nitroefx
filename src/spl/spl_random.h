#pragma once
#include "types.h"
#include "util/crc32.h"

#include <random>
#include <memory>

#include <glm/glm.hpp>

#define SPL_ACCURATE_RANDOM 1

class SPLRandom {
public:
    static u64 nextU64() {
        return getInstance()->dist();
    }

    static u32 nextU32() {
        return getInstance()->dist32();
    }

    static f32 nextF32() {
        return getInstance()->distf();
    }

    static f32 nextF32N() {
        return nextF32() * 2.0f - 1.0f;
    }

    static u32 nextU32(int bits) {
        return nextU32() >> (sizeof(u32) * 8 - bits);
    }

    static glm::vec3 unitVector() {
        return glm::normalize(glm::vec3(nextF32N(), nextF32N(), nextF32N()));
    }

    static glm::vec3 unitXY() {
        return glm::normalize(glm::vec3(nextF32N(), nextF32N(), 0.0f));
    }

    static u32 crcHash() {
        const auto inst = getInstance();
        const auto value = nextU64();
        const auto hash = detail::crc::crc32_impl((const char*)&value, sizeof(value), inst->m_crcSeed);
        inst->m_crcSeed = hash; // Update seed for next call

        return hash;
    }

    // variance must be in the range [0, 1]
    // Generates a random float in the range (n * (variance / 2)) around n
    // So the value range is [n * (1 - variance / 2), n * (1 + variance / 2)]
    static f32 scaledRange(f32 n, f32 variance) {
#if SPL_ACCURATE_RANDOM
        const fx32 nx = FX_F32_TO_FX32(n);
        const fx32 range = (fx32)(variance * 255.0f);
        const fx32 v = (nx * (255 - ((range * (fx32)nextU32(8)) >> 8))) >> 8;
        return FX_FX32_TO_F32(v);
#else
        variance = glm::clamp(variance, 0.0f, 1.0f);
        const f32 min = n * (1.0f - variance / 2.0f);
        const f32 max = n * (1.0f + variance / 2.0f);
        return min + nextF32() * (max - min);
#endif
    }

    // Generates a random float in the range [n, n * (1 + variance)]
    static f32 scaledRange2(f32 n, f32 variance) {
#if SPL_ACCURATE_RANDOM
        const fx32 nx = FX_F32_TO_FX32(n);
        const fx32 range = (fx32)(variance * 255.0f);
        const fx32 v = (nx * (255 + range - ((range * (fx32)nextU32(8)) >> 7))) >> 8;
        return FX_FX32_TO_F32(v);
#else
        const f32 min = n;
        const f32 max = n * (1.0f + variance);

        return min + nextF32() * (max - min);
#endif
    }

    static f32 range(f32 min, f32 max) {
        return min + nextF32() * (max - min);
    }

    static f32 aroundZero(f32 range) {
#if SPL_ACCURATE_RANDOM
        const fx32 rangeFX = FX_F32_TO_FX32(range);
        return FX_FX32_TO_F32(
            (rangeFX * (s32)nextU32(9) - (rangeFX << 8)) >> 8
        );
#else
        return SPLRandom::range(-range, range);
#endif
    }

    SPLRandom(const SPLRandom&) = delete;
    SPLRandom& operator=(const SPLRandom&) = delete;
    SPLRandom(SPLRandom&&) = delete;
    SPLRandom& operator=(SPLRandom&&) = delete;

private:
    SPLRandom() : m_gen(m_rd()), m_distf(0.0f, 1.0f) {}

    static SPLRandom* getInstance() {
        if (!s_instance) {
            s_instance = new SPLRandom();
        }

        return s_instance;
    }

    u64 dist() {
        return m_dist(m_gen);
    }

    u32 dist32() {
        return m_dist32(m_gen);
    }

    f32 distf() {
        return m_distf(m_gen);
    }

private:
    static inline SPLRandom* s_instance;

    std::random_device m_rd;
    std::mt19937_64 m_gen;
    std::uniform_int_distribution<u64> m_dist;
    std::uniform_int_distribution<u32> m_dist32;
    std::uniform_real_distribution<f32> m_distf;
    u32 m_crcSeed = ~0; // Initial seed for CRC hash generation
};

