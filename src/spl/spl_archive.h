#pragma once

#include <concepts>
#include <filesystem>
#include <memory>
#include <string_view>
#include <vector>

#include "spl_resource.h"
#include "glm/gtc/constants.hpp"


class SPLArchive {
public:
    explicit SPLArchive(const std::filesystem::path& filename);

    const SPLResource& getResource(size_t index) const { return m_resources[index]; }
    SPLResource& getResource(size_t index) { return m_resources[index]; }

    const std::vector<SPLResource>& getResources() const { return m_resources; }
    std::vector<SPLResource>& getResources() { return m_resources; }

    const SPLTexture& getTexture(size_t index) const { return m_textures[index]; }
    SPLTexture& getTexture(size_t index) { return m_textures[index]; }

    const std::vector<SPLTexture>& getTextures() const { return m_textures; }
    std::vector<SPLTexture>& getTextures() { return m_textures; }

    u32 getTextureArray() const { return m_textureArray; }

    size_t getResourceCount() const { return m_resources.size(); }
    size_t getTextureCount() const { return m_header.texCount; }

    static constexpr u32 SPL_FRAMES_PER_SECOND = 30;

private:
    void load(const std::filesystem::path& filename);

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
        return static_cast<f32>(index) / 65535.0f * glm::two_pi<f32>();
    }

    template<std::integral T = u16>
    static T toIndex(f32 angle) {
        return static_cast<T>(angle / glm::two_pi<f32>()) * (T)0xFFFF;
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

