#pragma once

#include <concepts>
#include <filesystem>
#include <memory>
#include <string_view>
#include <vector>

#include "spl_resource.h"
#include "glm/gtc/constants.hpp"

enum {
    SPA_MAGIC = 0x53504120, // " ASP"
    SPT_MAGIC = 0x53505420, // " TPS"

    SPA_VERSION = 0x315F3231, // "1_21"
};


class SPLArchive {
public:
    explicit SPLArchive(const std::filesystem::path& filename);
    explicit SPLArchive(std::span<const char> data);
    SPLArchive();

    static bool isValid(const std::filesystem::path& filename);
    static bool isValid(std::span<const char> data);

    const SPLResource& getResource(size_t index) const { return m_resources[index]; }
    SPLResource& getResource(size_t index) { return m_resources[index]; }

    const std::vector<SPLResource>& getResources() const { return m_resources; }
    std::vector<SPLResource>& getResources() { return m_resources; }

    const SPLTexture& getTexture(size_t index) const { return m_textures[index]; }
    SPLTexture& getTexture(size_t index) { return m_textures[index]; }

    const std::vector<SPLTexture>& getTextures() const { return m_textures; }
    std::vector<SPLTexture>& getTextures() { return m_textures; }

    const std::vector<std::vector<u8>>& getTextureData() const { return m_textureData; }
    std::vector<std::vector<u8>>& getTextureData() { return m_textureData; }

    const std::vector<std::vector<u8>>& getPaletteData() const { return m_paletteData; }
    std::vector<std::vector<u8>>& getPaletteData() { return m_paletteData; }

    u32 getTextureArray() const { return m_textureArray; }

    size_t getResourceCount() const { return m_resources.size(); }
    size_t getTextureCount() const { return m_textures.size(); }

    void save(const std::filesystem::path& filename);
    void save(std::vector<u8> data) const;
    void exportTextures(const std::filesystem::path& directory, const std::filesystem::path& backupDir = {}) const;
    void exportTexture(size_t index, const std::filesystem::path& file) const;

    void deleteTexture(size_t index);

    static constexpr u32 SPL_FRAMES_PER_SECOND = 30;

private:
    void load(std::istream& stream);
    static bool isValid(std::istream& stream);

    static SPLResourceHeader fromNative(const SPLResourceHeaderNative& native);

    SPLScaleAnim fromNative(const SPLScaleAnimNative& native);
    SPLColorAnim fromNative(const SPLColorAnimNative& native);
    SPLAlphaAnim fromNative(const SPLAlphaAnimNative& native);
    SPLTexAnim fromNative(const SPLTexAnimNative& native);
    SPLChildResource fromNative(const SPLChildResourceNative& native);

    std::shared_ptr<SPLGravityBehavior> fromNative(const SPLGravityBehaviorNative& native);
    std::shared_ptr<SPLRandomBehavior> fromNative(const SPLRandomBehaviorNative& native);
    std::shared_ptr<SPLMagnetBehavior> fromNative(const SPLMagnetBehaviorNative& native);
    std::shared_ptr<SPLSpinBehavior> fromNative(const SPLSpinBehaviorNative& native);
    std::shared_ptr<SPLCollisionPlaneBehavior> fromNative(const SPLCollisionPlaneBehaviorNative& native);
    std::shared_ptr<SPLConvergenceBehavior> fromNative(const SPLConvergenceBehaviorNative& native);

    SPLTextureParam fromNative(const SPLTextureParamNative& native);

    static SPLResourceHeaderNative toNative(const SPLResourceHeader& header);

    SPLScaleAnimNative toNative(const SPLScaleAnim& anim);
    SPLColorAnimNative toNative(const SPLColorAnim& anim);
    SPLAlphaAnimNative toNative(const SPLAlphaAnim& anim);
    SPLTexAnimNative toNative(const SPLTexAnim& anim);
    SPLChildResourceNative toNative(const SPLChildResource& anim);

    SPLGravityBehaviorNative toNative(const SPLGravityBehavior& behavior);
    SPLRandomBehaviorNative toNative(const SPLRandomBehavior& behavior);
    SPLMagnetBehaviorNative toNative(const SPLMagnetBehavior& behavior);
    SPLSpinBehaviorNative toNative(const SPLSpinBehavior& behavior);
    SPLCollisionPlaneBehaviorNative toNative(const SPLCollisionPlaneBehavior& behavior);
    SPLConvergenceBehaviorNative toNative(const SPLConvergenceBehavior& behavior);

    SPLTextureParamNative toNative(const SPLTextureParam& param);

    template<std::integral T = u32>
    static f32 toSeconds(T frames) {
        return static_cast<f32>(frames) / SPL_FRAMES_PER_SECOND;
    }

    template<std::integral T = u32>
    static T toFrames(f32 seconds) {
        return static_cast<T>(seconds * SPL_FRAMES_PER_SECOND);
    }

    template<std::integral T = u16>
    static f32 toAngle(T index) {
        const auto angle = static_cast<f32>(index) / 65535.0f * glm::two_pi<f32>();
        if (angle > glm::pi<f32>()) {
            return angle - glm::two_pi<f32>();
        }

        return angle;
    }

    template<std::integral T = u16>
    static T toIndex(f32 angle) {
        // Normalize the angle to the range [0, 2π)
        if (angle < 0) {
            angle += glm::two_pi<f32>();
        }

        return static_cast<T>(angle * 0xFFFF / glm::two_pi<f32>());
    }

private:
    SPLFileHeader m_header;
    std::vector<SPLResource> m_resources;
    std::vector<SPLTexture> m_textures;
    std::vector<std::vector<u8>> m_textureData;
    std::vector<std::vector<u8>> m_paletteData;
    u32 m_textureArray;

    friend struct SPLBehavior;
};

