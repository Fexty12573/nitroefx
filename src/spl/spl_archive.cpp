#include "spl_archive.h"
#include "gfx/gl_util.h"

#include <GL/glew.h>
#include <glm/gtc/constants.hpp>
#include <spdlog/spdlog.h>
#include <stb_image_write.h>
#include <spng.h>
#include <fstream>
#include <concepts>
#include <ranges>

#include "spl_random.h"


template<class T> requires std::is_trivially_copyable_v<T>
std::istream& operator>>(std::istream& stream, T& v) {
    return stream.read(reinterpret_cast<char*>(&v), sizeof(T));
}

template<class T> requires std::is_trivially_copyable_v<T>
std::ostream& operator<<(std::ostream& stream, const T& v) {
    return stream.write(reinterpret_cast<const char*>(&v), sizeof(T));
}

namespace {

#define ROW(i0, i1, i2, i3, i4, i5, i6, i7) (((i3 << 6) | (i2 << 4) | (i1 << 2) | (i0))), (((i7 << 6) | (i6 << 4) | (i5 << 2) | (i4)))

constexpr std::array<u8, 8 * 8 / 4> DEFAULT_TEXTURE = {
    ROW(0, 0, 1, 1, 0, 0, 1, 1),
    ROW(0, 0, 1, 1, 0, 0, 1, 1),
    ROW(1, 1, 0, 0, 1, 1, 0, 0),
    ROW(1, 1, 0, 0, 1, 1, 0, 0),
    ROW(0, 0, 1, 1, 0, 0, 1, 1),
    ROW(0, 0, 1, 1, 0, 0, 1, 1),
    ROW(1, 1, 0, 0, 1, 1, 0, 0),
    ROW(1, 1, 0, 0, 1, 1, 0, 0)
};

#undef ROW

constexpr std::array<GXRgba, 4> DEFAULT_PALETTE = {
    GXRgba::fromRGBA(255, 0, 255, 255), // Pink
    GXRgba::fromRGBA(0, 0, 0, 255), // Black
    GXRgba::fromRGBA(0, 0, 0, 0),
    GXRgba::fromRGBA(0, 0, 0, 0)
};

void writePng(const SPLTexture& texture, const std::filesystem::path& file);
void writePngIndexed(const SPLTexture& texture, const std::filesystem::path& file);
void writePngRgba(const SPLTexture& texture, const std::filesystem::path& file);

}


SPLArchive::SPLArchive(const std::filesystem::path& filename) : m_header() {
    load(filename);
}

SPLArchive::SPLArchive() {
    m_header = {
        .magic = SPA_MAGIC,
        .version = SPA_VERSION,
        .resCount = 0,
        .texCount = 1, // At least one texture is required
        .reserved0 = 0,
        .resSize = 0,
        .texSize = 0,
        .texOffset = 0,
        .reserved1 = 0
    };

    // Create a default 8x8 texture
    SPLTexture defaultTexture;
    defaultTexture.param = {
        .format = TextureFormat::Palette4,
        .s = 0,
        .t = 0,
        .repeat = TextureRepeat::None,
        .flip = TextureFlip::None,
        .palColor0Transparent = false,
        .useSharedTexture = false,
        .sharedTexID = 0,
    };

    defaultTexture.width = 8;
    defaultTexture.height = 8;

    defaultTexture.textureData = std::span<const u8>(DEFAULT_TEXTURE.data(), DEFAULT_TEXTURE.size());
    defaultTexture.paletteData = std::span<const u8>((u8*)DEFAULT_PALETTE.data(), DEFAULT_PALETTE.size() * sizeof(GXRgba));

    defaultTexture.glTexture = std::make_shared<GLTexture>(defaultTexture);

    m_textures.push_back(defaultTexture);
}

void SPLArchive::load(const std::filesystem::path& filename) {
    std::ifstream file(filename, std::ios::binary | std::ios::in);
    if (!file) {
        spdlog::error("Failed to open file: {}", filename.string());
        return;
    }

    file >> m_header;

    if (m_header.magic != SPA_MAGIC) {
        spdlog::error("Invalid SPL archive magic: {}", m_header.magic);
        return;
    }

    m_resources.resize(m_header.resCount);
    
    for (size_t i = 0; i < m_header.resCount; i++) {
        SPLResource& res = m_resources[i];

        SPLResourceHeaderNative header;
        file >> header;

        res.header = fromNative(header);
        const auto& flags = res.header.flags;

        // Animations
        if (flags.hasScaleAnim) {
            SPLScaleAnimNative scaleAnim;
            file >> scaleAnim;
            res.scaleAnim = fromNative(scaleAnim);
        }

        if (flags.hasColorAnim) {
            SPLColorAnimNative colorAnim;
            file >> colorAnim;
            res.colorAnim = fromNative(colorAnim);
        }

        if (flags.hasAlphaAnim) {
            SPLAlphaAnimNative alphaAnim;
            file >> alphaAnim;
            res.alphaAnim = fromNative(alphaAnim);
        }

        if (flags.hasTexAnim) {
            SPLTexAnimNative texAnim;
            file >> texAnim;
            res.texAnim = fromNative(texAnim);
        }

        if (flags.hasChildResource) {
            SPLChildResourceNative childResource;
            file >> childResource;
            res.childResource = fromNative(childResource);
        }

        // Behaviors
        if (flags.hasGravityBehavior) {
            SPLGravityBehaviorNative gravityBehavior;
            file >> gravityBehavior;
            res.behaviors.push_back(fromNative(gravityBehavior));
        }

        if (flags.hasRandomBehavior) {
            SPLRandomBehaviorNative randomBehavior;
            file >> randomBehavior;
            res.behaviors.push_back(fromNative(randomBehavior));
        }

        if (flags.hasMagnetBehavior) {
            SPLMagnetBehaviorNative magnetBehavior;
            file >> magnetBehavior;
            res.behaviors.push_back(fromNative(magnetBehavior));
        }

        if (flags.hasSpinBehavior) {
            SPLSpinBehaviorNative spinBehavior;
            file >> spinBehavior;
            res.behaviors.push_back(fromNative(spinBehavior));
        }

        if (flags.hasCollisionPlaneBehavior) {
            SPLCollisionPlaneBehaviorNative collisionPlaneBehavior;
            file >> collisionPlaneBehavior;
            res.behaviors.push_back(fromNative(collisionPlaneBehavior));
        }

        if (flags.hasConvergenceBehavior) {
            SPLConvergenceBehaviorNative convergenceBehavior;
            file >> convergenceBehavior;
            res.behaviors.push_back(fromNative(convergenceBehavior));
        }
    }

    m_textures.resize(m_header.texCount);

    for (size_t i = 0; i < m_header.texCount; i++) {
        SPLTexture& tex = m_textures[i];

        SPLTextureResource texRes;
        s64 offset = file.tellg();
        file >> texRes;

        if (texRes.magic != SPT_MAGIC) {
            spdlog::error("Invalid texture resource magic: {}", texRes.magic);
            return;
        }

        tex.resource = nullptr;
        tex.param = fromNative(texRes.param);
        tex.width = 1 << (texRes.param.s + 3);
        tex.height = 1 << (texRes.param.t + 3);

        if (!texRes.param.useSharedTexture) { // Handle shared textures later
            if (texRes.textureSize > 0) {
                m_textureData.emplace_back(texRes.textureSize);
                file.read((char*)m_textureData.back().data(), texRes.textureSize);
            }

            if (texRes.textureSize > 0) {
                m_paletteData.emplace_back(texRes.paletteSize);
                file.seekg(offset + texRes.paletteOffset, std::ios::beg);
                file.read((char*)m_paletteData.back().data(), texRes.paletteSize);
            } else {
                // No palette data (TextureFormat::Direct)
                m_paletteData.emplace_back();
            }

            tex.textureData = m_textureData.back();
            tex.paletteData = m_paletteData.back();

            tex.glTexture = std::make_shared<GLTexture>(tex);
        }

        file.seekg(offset + texRes.resourceSize, std::ios::beg);
    }

    // Resolve shared textures
    for (auto& tex : m_textures) {
        if (tex.param.useSharedTexture) {
            tex.textureData = m_textureData[tex.param.sharedTexID];
            tex.paletteData = m_paletteData[tex.param.sharedTexID];
            tex.glTexture = m_textures[tex.param.sharedTexID].glTexture;
        }
    }
}

void SPLArchive::save(const std::filesystem::path& filename) {
    std::ofstream file(filename, std::ios::binary | std::ios::out);
    if (!file) {
        spdlog::error("Failed to open file for writing: {}", filename.string());
        return;
    }

    m_header.resCount = (u16)m_resources.size();
    m_header.texCount = (u16)m_textures.size();
    // resSize and texSize are set after writing them

    file << m_header;

    const auto resPos = file.tellp();

    for (auto& res : m_resources) {
        res.header.flags.hasScaleAnim = res.scaleAnim.has_value();
        res.header.flags.hasColorAnim = res.colorAnim.has_value();
        res.header.flags.hasAlphaAnim = res.alphaAnim.has_value();
        res.header.flags.hasTexAnim = res.texAnim.has_value();
        res.header.flags.hasChildResource = res.childResource.has_value();
        res.header.flags.hasGravityBehavior = res.hasBehavior(SPLBehaviorType::Gravity);
        res.header.flags.hasRandomBehavior = res.hasBehavior(SPLBehaviorType::Random);
        res.header.flags.hasMagnetBehavior = res.hasBehavior(SPLBehaviorType::Magnet);
        res.header.flags.hasSpinBehavior = res.hasBehavior(SPLBehaviorType::Spin);
        res.header.flags.hasCollisionPlaneBehavior = res.hasBehavior(SPLBehaviorType::CollisionPlane);
        res.header.flags.hasConvergenceBehavior = res.hasBehavior(SPLBehaviorType::Convergence);

        file << toNative(res.header);

        const auto& flags = res.header.flags;
        if (flags.hasScaleAnim) {
            file << toNative(*res.scaleAnim);
        }

        if (flags.hasColorAnim) {
            file << toNative(*res.colorAnim);
        }

        if (flags.hasAlphaAnim) {
            file << toNative(*res.alphaAnim);
        }

        if (flags.hasTexAnim) {
            file << toNative(*res.texAnim);
        }

        if (flags.hasChildResource) {
            file << toNative(*res.childResource);
        }


        if (flags.hasGravityBehavior) {
            file << toNative(*res.getBehavior<SPLGravityBehavior>(SPLBehaviorType::Gravity));
        }

        if (flags.hasRandomBehavior) {
            file << toNative(*res.getBehavior<SPLRandomBehavior>(SPLBehaviorType::Random));
        }

        if (flags.hasMagnetBehavior) {
            file << toNative(*res.getBehavior<SPLMagnetBehavior>(SPLBehaviorType::Magnet));
        }

        if (flags.hasSpinBehavior) {
            file << toNative(*res.getBehavior<SPLSpinBehavior>(SPLBehaviorType::Spin));
        }

        if (flags.hasCollisionPlaneBehavior) {
            file << toNative(*res.getBehavior<SPLCollisionPlaneBehavior>(SPLBehaviorType::CollisionPlane));
        }

        if (flags.hasConvergenceBehavior) {
            file << toNative(*res.getBehavior<SPLConvergenceBehavior>(SPLBehaviorType::Convergence));
        }
    }

    const auto texPos = file.tellp();
    const auto resSize = texPos - resPos;

    for (auto& tex : m_textures) {
        SPLTextureResource texRes;
        texRes.magic = SPT_MAGIC;
        texRes.param = toNative(tex.param);
        texRes.textureSize = (u32)tex.textureData.size();
        texRes.paletteOffset = sizeof(SPLTextureResource) + texRes.textureSize;
        texRes.paletteSize = (u32)tex.paletteData.size();
        texRes.unused0 = texRes.unused1 = 0;
        texRes.resourceSize = sizeof(SPLTextureResource) + texRes.textureSize + texRes.paletteSize;

        file << texRes;
        file.write((char*)tex.textureData.data(), texRes.textureSize);
        file.write((char*)tex.paletteData.data(), texRes.paletteSize);
    }

    const auto texSize = file.tellp() - texPos;

    m_header.resSize = (u32)resSize;
    m_header.texSize = (u32)texSize;
    m_header.texOffset = (u32)texPos;

    file.seekp(0, std::ios::beg);
    file << m_header;
}

void SPLArchive::exportTextures(const std::filesystem::path& directory, const std::filesystem::path& backupDir) const {
    const bool doBackup = !backupDir.empty();
    std::filesystem::path backupDest;
    if (doBackup) {
        backupDest = backupDir / fmt::format("{:08x}", SPLRandom::crcHash());
        std::filesystem::create_directories(backupDest);
        spdlog::info("Backing up existing textures to {}", backupDest.string());
        spdlog::warn("This directory will be cleared the next time the program is run.");
    }

    for (const auto [i, tex] : std::views::enumerate(m_textures)) {
        if (!tex.glTexture) {
            spdlog::warn("Texture {} does not have a GL texture, skipping export", i);
            continue;
        }

        const auto fileName = fmt::format("{}.png", i);
        std::filesystem::path path = directory / fileName;
        if (std::filesystem::exists(path)) {
            if (doBackup) {
                std::filesystem::path backupPath = backupDest / fileName;
                std::filesystem::copy(path, backupPath);
                spdlog::info("Backed up existing texture {}", path.filename().string());
            } else {
                spdlog::warn("No backup directory specified, skipping backup for {}", path.filename().string());
            }
        }

        const auto rgba = tex.convertToRGBA8888();
        if (!rgba.empty()) {
            if (stbi_write_png(path.string().c_str(), tex.width, tex.height, 4, rgba.data(), tex.width * 4)) {
                spdlog::info("Exported texture {} to {}", i, path.string());
            } else {
                spdlog::error("Failed to write texture {} to {}", i, path.string());
            }
        } else {
            spdlog::error("Failed to convert texture {} to RGBA8888", i);
        }
    }
}

void SPLArchive::exportTexture(size_t index, const std::filesystem::path& file) const {
    if (index >= m_textures.size()) {
        spdlog::error("Invalid texture index: {}", index);
        return;
    }

    const auto& tex = m_textures[index];
    if (!tex.glTexture) {
        spdlog::warn("Texture {} does not have a GL texture, skipping export", index);
        return;
    }

    int ok = -1;
    
    const auto ext = file.extension().string();
    if (ext == ".png") {
        writePng(tex, file);
        ok = 1;
    } else {
        const auto rgba = tex.convertToRGBA8888();
        if (ext == ".jpg" || ext == ".jpeg") {
            ok = stbi_write_jpg(file.string().c_str(), tex.width, tex.height, 4, rgba.data(), 100);
        } else if (ext == ".bmp") {
            ok = stbi_write_bmp(file.string().c_str(), tex.width, tex.height, 4, rgba.data());
        } else if (ext == ".tga") {
            ok = stbi_write_tga(file.string().c_str(), tex.width, tex.height, 4, rgba.data());
        } else {
            spdlog::error("Unsupported texture format: {}", ext);
            return;
        }
    }

    if (ok) {
        spdlog::info("Exported texture {} to {}", index, file.string());
    } else {
        spdlog::error("Failed to write texture {} to {}", index, file.string());
    }
}

void SPLArchive::deleteTexture(size_t index) {
    if (index >= m_textures.size()) {
        spdlog::error("Invalid texture index: {}", index);
        return;
    }

    // Should never be <1 but just in case
    if (m_textures.size() <= 1) {
        spdlog::warn("Cannot delete the last texture in the archive");
        return;
    }

    auto& tex = m_textures[index];
    if (tex.glTexture) {
        tex.glTexture.reset();
    }

    // Not gonna delete the texture data or palette data here, as they might be shared
    // and it would just complicate things unnecessarily.

    m_textures.erase(m_textures.begin() + index);

    // Update shared texture IDs
    for (auto& t : m_textures) {
        if (t.param.sharedTexID > index) {
            t.param.sharedTexID--;
        }

        if (t.param.sharedTexID >= m_textures.size()) {
            t.param.sharedTexID = 0; // Reset to 0 if invalid
        }
    }

    for (auto& res : m_resources) {
        if (res.header.misc.textureIndex > index) {
            res.header.misc.textureIndex--;
        }

        if (res.header.misc.textureIndex >= m_textures.size()) {
            res.header.misc.textureIndex = 0; // Reset to 0 if invalid
        }

        // Update child resource texture index if it exists
        if (res.childResource) {
            if (res.childResource->misc.texture > index) {
                res.childResource->misc.texture--;
            }

            if (res.childResource->misc.texture >= m_textures.size()) {
                res.childResource->misc.texture = 0; // Reset to 0 if invalid
            }
        }
    }

    m_header.texCount = (u16)m_textures.size();

    spdlog::info("Deleted texture {}", index);
}

SPLResourceHeader SPLArchive::fromNative(const SPLResourceHeaderNative &native) {
    return SPLResourceHeader {
        .flags = {
            .emissionType = (SPLEmissionType)native.flags.emissionType,
            .drawType = (SPLDrawType)native.flags.drawType,
            .emissionAxis = (SPLEmissionAxis)native.flags.circleAxis,
            .hasScaleAnim = !!native.flags.hasScaleAnim,
            .hasColorAnim = !!native.flags.hasColorAnim,
            .hasAlphaAnim = !!native.flags.hasAlphaAnim,
            .hasTexAnim = !!native.flags.hasTexAnim,
            .hasRotation = !!native.flags.hasRotation,
            .randomInitAngle = !!native.flags.randomInitAngle,
            .selfMaintaining = !!native.flags.selfMaintaining,
            .followEmitter = !!native.flags.followEmitter,
            .hasChildResource = !!native.flags.hasChildResource,
            .polygonRotAxis = (SPLPolygonRotAxis)native.flags.polygonRotAxis,
            .polygonReferencePlane = (u8)native.flags.polygonReferencePlane,
            .randomizeLoopedAnim = !!native.flags.randomizeLoopedAnim,
            .drawChildrenFirst = !!native.flags.drawChildrenFirst,
            .hideParent = !!native.flags.hideParent,
            .useViewSpace = !!native.flags.useViewSpace,
            .hasGravityBehavior = !!native.flags.hasGravityBehavior,
            .hasRandomBehavior = !!native.flags.hasRandomBehavior,
            .hasMagnetBehavior = !!native.flags.hasMagnetBehavior,
            .hasSpinBehavior = !!native.flags.hasSpinBehavior,
            .hasCollisionPlaneBehavior = !!native.flags.hasCollisionPlaneBehavior,
            .hasConvergenceBehavior = !!native.flags.hasConvergenceBehavior,
            .hasFixedPolygonID = !!native.flags.hasFixedPolygonID,
            .childHasFixedPolygonID = !!native.flags.childHasFixedPolygonID
        },
        .emitterBasePos = native.emitterBasePos.toVec3(),
        .emissionCount = (u32)native.emissionCount >> FX32_SHIFT,
        .radius = FX_FX32_TO_F32(native.radius),
        .length = FX_FX32_TO_F32(native.length),
        .axis = native.axis.toVec3(),
        .color = native.color.toVec3(),
        .initVelPosAmplifier = FX_FX32_TO_F32(native.initVelPosAmplifier),
        .initVelAxisAmplifier = FX_FX32_TO_F32(native.initVelAxisAmplifier),
        .baseScale = FX_FX32_TO_F32(native.baseScale),
        .aspectRatio = FX_FX16_TO_F32(native.aspectRatio),
        .startDelay = toSeconds(native.startDelay),
        .minRotation = toAngle(native.minRotation),
        .maxRotation = toAngle(native.maxRotation),
        .initAngle = toAngle(native.initAngle),
        .emitterLifeTime = toSeconds(native.emitterLifeTime),
        .particleLifeTime = toSeconds(native.particleLifeTime),
        .variance = {
            .baseScale = (f32)native.variance.baseScale / 255.0f,
            .lifeTime = (f32)native.variance.lifeTime / 255.0f,
            .initVel = (f32)native.variance.initVel / 255.0f
        },
        .misc = {
            .emissionInterval = toSeconds(native.misc.emissionInterval),
            .baseAlpha = native.misc.baseAlpha / 31.0f,
            // 0 - 255 -> 0.75 - 1.25 (almost)
            .airResistance = 0.75f + (f32)native.misc.airResistance / 256.0f * 0.5f,
            .textureIndex = (u8)native.misc.textureIndex,
            .loopTime = toSeconds(native.misc.loopFrames),
            .dbbScale = FX_FX16_TO_F32(native.misc.dbbScale),
            .textureTileCountS = (u8)native.misc.textureTileCountS,
            .textureTileCountT = (u8)native.misc.textureTileCountT,
            .scaleAnimDir = (SPLScaleAnimDir)native.misc.scaleAnimDir,
            .dpolFaceEmitter = !!native.misc.dpolFaceEmitter,
            .flipTextureS = !!native.misc.flipTextureS,
            .flipTextureT = !!native.misc.flipTextureT
        },
        .polygonX = FX_FX16_TO_F32(native.polygonX),
        .polygonY = FX_FX16_TO_F32(native.polygonY),
    };
}

SPLScaleAnim SPLArchive::fromNative(const SPLScaleAnimNative &native) {
    return SPLScaleAnim(native);
}

SPLColorAnim SPLArchive::fromNative(const SPLColorAnimNative &native) {
    return SPLColorAnim(native);
}

SPLAlphaAnim SPLArchive::fromNative(const SPLAlphaAnimNative &native) {
    return SPLAlphaAnim(native);
}

SPLTexAnim SPLArchive::fromNative(const SPLTexAnimNative &native) {
    return SPLTexAnim(native);
}

SPLChildResource SPLArchive::fromNative(const SPLChildResourceNative &native) {
    return SPLChildResource {
        .flags = {
            .usesBehaviors = !!native.flags.usesBehaviors,
            .hasScaleAnim = !!native.flags.hasScaleAnim,
            .hasAlphaAnim = !!native.flags.hasAlphaAnim,
            .rotationType = (SPLChildRotationType)native.flags.rotationType,
            .followEmitter = !!native.flags.followEmitter,
            .useChildColor = !!native.flags.useChildColor,
            .drawType = (SPLDrawType)native.flags.drawType,
            .polygonRotAxis = (SPLPolygonRotAxis)native.flags.polygonRotAxis,
            .polygonReferencePlane = (u8)native.flags.polygonReferencePlane,
        },
        .randomInitVelMag = FX_FX16_TO_F32(native.randomInitVelMag),
        .endScale = FX_FX16_TO_F32(native.endScale),
        .lifeTime = toSeconds(native.lifeTime),
        .velocityRatio = native.velocityRatio / 255.0f,
        .scaleRatio = ((int)native.scaleRatio + 1) / 64.0f,
        .color = native.color,
        .misc = {
            .emissionCount = (u8)native.misc.emissionCount,
            .emissionDelay = (f32)native.misc.emissionDelay / 255.0f,
            .emissionInterval = toSeconds(native.misc.emissionInterval),
            .texture = (u8)native.misc.texture,
            .textureTileCountS = (u8)native.misc.textureTileCountS,
            .textureTileCountT = (u8)native.misc.textureTileCountT,
            .flipTextureS = !!native.misc.flipTextureS,
            .flipTextureT = !!native.misc.flipTextureT,
            .dpolFaceEmitter = !!native.misc.dpolFaceEmitter
        }
    };
}

std::shared_ptr<SPLGravityBehavior> SPLArchive::fromNative(const SPLGravityBehaviorNative& native) {
    return std::make_shared<SPLGravityBehavior>(native);
}

std::shared_ptr<SPLRandomBehavior> SPLArchive::fromNative(const SPLRandomBehaviorNative& native) {
    return std::make_shared<SPLRandomBehavior>(native);
}

std::shared_ptr<SPLMagnetBehavior> SPLArchive::fromNative(const SPLMagnetBehaviorNative& native) {
    return std::make_shared<SPLMagnetBehavior>(native);
}

std::shared_ptr<SPLSpinBehavior> SPLArchive::fromNative(const SPLSpinBehaviorNative& native) {
    return std::make_shared<SPLSpinBehavior>(native);
}

std::shared_ptr<SPLCollisionPlaneBehavior> SPLArchive::fromNative(const SPLCollisionPlaneBehaviorNative& native) {
    return std::make_shared<SPLCollisionPlaneBehavior>(native);
}

std::shared_ptr<SPLConvergenceBehavior> SPLArchive::fromNative(const SPLConvergenceBehaviorNative& native) {
    return std::make_shared<SPLConvergenceBehavior>(native);
}

SPLTextureParam SPLArchive::fromNative(const SPLTextureParamNative& native) {
    return SPLTextureParam{
        .format = (TextureFormat)native.format,
        .s = (u8)native.s,
        .t = (u8)native.t,
        .repeat = (TextureRepeat)native.repeat,
        .flip = (TextureFlip)native.flip,
        .palColor0Transparent = !!native.palColor0,
        .useSharedTexture = !!native.useSharedTexture,
        .sharedTexID = (u8)native.sharedTexID
    };
}

SPLResourceHeaderNative SPLArchive::toNative(const SPLResourceHeader& header) {
    return SPLResourceHeaderNative{
        .flags = {
            .emissionType = (u8)header.flags.emissionType,
            .drawType = (u8)header.flags.drawType,
            .circleAxis = (u8)header.flags.emissionAxis,
            .hasScaleAnim = (u8)header.flags.hasScaleAnim,
            .hasColorAnim = (u8)header.flags.hasColorAnim,
            .hasAlphaAnim = (u8)header.flags.hasAlphaAnim,
            .hasTexAnim = (u8)header.flags.hasTexAnim,
            .hasRotation = (u8)header.flags.hasRotation,
            .randomInitAngle = (u8)header.flags.randomInitAngle,
            .selfMaintaining = (u8)header.flags.selfMaintaining,
            .followEmitter = (u8)header.flags.followEmitter,
            .hasChildResource = (u8)header.flags.hasChildResource,
            .polygonRotAxis = (u8)header.flags.polygonRotAxis,
            .polygonReferencePlane = (u8)header.flags.polygonReferencePlane,
            .randomizeLoopedAnim = (u8)header.flags.randomizeLoopedAnim,
            .drawChildrenFirst = (u8)header.flags.drawChildrenFirst,
            .hideParent = (u8)header.flags.hideParent,
            .useViewSpace = (u8)header.flags.useViewSpace,
            .hasGravityBehavior = (u8)header.flags.hasGravityBehavior,
            .hasRandomBehavior = (u8)header.flags.hasRandomBehavior,
            .hasMagnetBehavior = (u8)header.flags.hasMagnetBehavior,
            .hasSpinBehavior = (u8)header.flags.hasSpinBehavior,
            .hasCollisionPlaneBehavior = (u8)header.flags.hasCollisionPlaneBehavior,
            .hasConvergenceBehavior = (u8)header.flags.hasConvergenceBehavior,
            .hasFixedPolygonID = (u8)header.flags.hasFixedPolygonID,
            .childHasFixedPolygonID = (u8)header.flags.childHasFixedPolygonID
        },
        .emitterBasePos = header.emitterBasePos,
        .emissionCount = FX_F32_TO_FX32((f32)header.emissionCount),
        .radius = FX_F32_TO_FX32(header.radius),
        .length = FX_F32_TO_FX32(header.length),
        .axis = header.axis,
        .color = header.color,
        .initVelPosAmplifier = FX_F32_TO_FX32(header.initVelPosAmplifier),
        .initVelAxisAmplifier = FX_F32_TO_FX32(header.initVelAxisAmplifier),
        .baseScale = FX_F32_TO_FX32(header.baseScale),
        .aspectRatio = FX_F32_TO_FX16(header.aspectRatio),
        .startDelay = toFrames<u16>(header.startDelay),
        .minRotation = toIndex<s16>(header.minRotation),
        .maxRotation = toIndex<s16>(header.maxRotation),
        .initAngle = toIndex<u16>(header.initAngle),
        .reserved = 0,
        .emitterLifeTime = toFrames<u16>(header.emitterLifeTime),
        .particleLifeTime = toFrames<u16>(header.particleLifeTime),
        .variance = {
            .baseScale = (u8)(header.variance.baseScale * 255.0f),
            .lifeTime = (u8)(header.variance.lifeTime * 255.0f),
            .initVel = (u8)(header.variance.initVel * 255.0f)
        },
        .misc = {
            .emissionInterval = toFrames<u8>(header.misc.emissionInterval),
            .baseAlpha = (u8)(header.misc.baseAlpha * 31.0f),
            .airResistance = (u8)((header.misc.airResistance - 0.75f) / 0.5f * 256.0f),
            .textureIndex = (u8)header.misc.textureIndex,
            .loopFrames = toFrames<u8>(header.misc.loopTime),
            .dbbScale = (u16)FX_F32_TO_FX16(header.misc.dbbScale),
            .textureTileCountS = (u8)header.misc.textureTileCountS,
            .textureTileCountT = (u8)header.misc.textureTileCountT,
            .scaleAnimDir = (u8)header.misc.scaleAnimDir,
            .dpolFaceEmitter = (u8)header.misc.dpolFaceEmitter,
            .flipTextureS = (u8)header.misc.flipTextureS,
            .flipTextureT = (u8)header.misc.flipTextureT
        },
        .polygonX = FX_F32_TO_FX16(header.polygonX),
        .polygonY = FX_F32_TO_FX16(header.polygonY),
        .userData = {
            .flags = 0
        }
    };
}

SPLScaleAnimNative SPLArchive::toNative(const SPLScaleAnim& anim) {
    return SPLScaleAnimNative{
        .start = FX_F32_TO_FX16(anim.start),
        .mid = FX_F32_TO_FX16(anim.mid),
        .end = FX_F32_TO_FX16(anim.end),
        .curve = anim.curve,
        .flags = {
            .loop = (u16)anim.flags.loop
        },
        .padding = 0
    };
}

SPLColorAnimNative SPLArchive::toNative(const SPLColorAnim& anim) {
    return SPLColorAnimNative{
        .start = anim.start,
        .end = anim.end,
        .curve = anim.curve,
        .flags = {
            .randomStartColor = (u16)anim.flags.randomStartColor,
            .loop = (u16)anim.flags.loop,
            .interpolate = (u16)anim.flags.interpolate
        },
        .padding = 0
    };
}

SPLAlphaAnimNative SPLArchive::toNative(const SPLAlphaAnim& anim) {
    return SPLAlphaAnimNative{
        .alpha = {
            .start = (u16)(anim.alpha.start * 31.0f),
            .mid = (u16)(anim.alpha.mid * 31.0f),
            .end = (u16)(anim.alpha.end * 31.0f)
        },
        .flags = {
            .randomRange = (u16)(anim.flags.randomRange * 255.0f),
            .loop = (u16)anim.flags.loop
        },
        .curve = anim.curve,
        .padding = 0
    };
}

SPLTexAnimNative SPLArchive::toNative(const SPLTexAnim& anim) {
    return SPLTexAnimNative{
        .textures = {
            anim.textures[0],
            anim.textures[1],
            anim.textures[2],
            anim.textures[3],
            anim.textures[4],
            anim.textures[5],
            anim.textures[6],
            anim.textures[7]
        },
        .param = {
            .frameCount = (u8)anim.param.textureCount,
            .step = (u8)(anim.param.step * 255.0f),
            .randomizeInit = (u8)anim.param.randomizeInit,
            .loop = (u8)anim.param.loop
        }
    };
}

SPLChildResourceNative SPLArchive::toNative(const SPLChildResource& anim) {
    return SPLChildResourceNative{
        .flags = {
            .usesBehaviors = (u16)anim.flags.usesBehaviors,
            .hasScaleAnim = (u16)anim.flags.hasScaleAnim,
            .hasAlphaAnim = (u16)anim.flags.hasAlphaAnim,
            .rotationType = (u16)anim.flags.rotationType,
            .followEmitter = (u16)anim.flags.followEmitter,
            .useChildColor = (u16)anim.flags.useChildColor,
            .drawType = (u16)anim.flags.drawType,
            .polygonRotAxis = (u16)anim.flags.polygonRotAxis,
            .polygonReferencePlane = (u8)anim.flags.polygonReferencePlane
        },
        .randomInitVelMag = FX_F32_TO_FX16(anim.randomInitVelMag),
        .endScale = FX_F32_TO_FX16(anim.endScale),
        .lifeTime = toFrames<u16>(anim.lifeTime),
        .velocityRatio = (u8)(anim.velocityRatio * 255.0f),
        .scaleRatio = (u8)glm::clamp(anim.scaleRatio * 64.0f - 1.0f, 0.0f, 255.0f),
        .color = anim.color,
        .misc = {
            .emissionCount = (u8)anim.misc.emissionCount,
            .emissionDelay = (u8)(anim.misc.emissionDelay * 255.0f),
            .emissionInterval = toFrames<u8>(anim.misc.emissionInterval),
            .texture = (u8)anim.misc.texture,
            .textureTileCountS = (u8)anim.misc.textureTileCountS,
            .textureTileCountT = (u8)anim.misc.textureTileCountT,
            .flipTextureS = (u8)anim.misc.flipTextureS,
            .flipTextureT = (u8)anim.misc.flipTextureT,
            .dpolFaceEmitter = (u8)anim.misc.dpolFaceEmitter
        }
    };
}

SPLGravityBehaviorNative SPLArchive::toNative(const SPLGravityBehavior& behavior) {
    return SPLGravityBehaviorNative{
        .magnitude = behavior.magnitude,
        .padding = 0
    };
}

SPLRandomBehaviorNative SPLArchive::toNative(const SPLRandomBehavior& behavior) {
    return SPLRandomBehaviorNative{
        .magnitude = behavior.magnitude,
        .applyInterval = toFrames<u16>(behavior.applyInterval),
    };
}

SPLMagnetBehaviorNative SPLArchive::toNative(const SPLMagnetBehavior& behavior) {
    return SPLMagnetBehaviorNative{
        .target = behavior.target,
        .force = FX_F32_TO_FX16(behavior.force),
        .padding = 0
    };
}

SPLSpinBehaviorNative SPLArchive::toNative(const SPLSpinBehavior& behavior) {
    return SPLSpinBehaviorNative{
        .angle = toIndex<u16>(behavior.angle),
        .axis = (u16)behavior.axis,
    };
}

SPLCollisionPlaneBehaviorNative SPLArchive::toNative(const SPLCollisionPlaneBehavior& behavior) {
    return SPLCollisionPlaneBehaviorNative{
        .y = FX_F32_TO_FX32(behavior.y),
        .elasticity = FX_F32_TO_FX16(behavior.elasticity),
        .flags = {
            .collisionType = (u16)behavior.collisionType
        }
    };
}

SPLConvergenceBehaviorNative SPLArchive::toNative(const SPLConvergenceBehavior& behavior) {
    return SPLConvergenceBehaviorNative{
        .target = behavior.target,
        .force = FX_F32_TO_FX16(behavior.force),
        .padding = 0
    };
}

SPLTextureParamNative SPLArchive::toNative(const SPLTextureParam& param) {
    return SPLTextureParamNative{
        .format = (u8)param.format,
        .s = (u8)param.s,
        .t = (u8)param.t,
        .repeat = (u8)param.repeat,
        .flip = (u8)param.flip,
        .palColor0 = (u8)param.palColor0Transparent,
        .useSharedTexture = (u8)param.useSharedTexture,
        .sharedTexID = (u8)param.sharedTexID,
    };
}

namespace {

void writePng(const SPLTexture& texture, const std::filesystem::path& file) {
    switch (texture.param.format) {
    case TextureFormat::Palette4:
    case TextureFormat::Palette16:
    case TextureFormat::Palette256:
        return writePngIndexed(texture, file);
    case TextureFormat::Comp4x4:
    case TextureFormat::A3I5:
    case TextureFormat::A5I3:
    case TextureFormat::Direct:
        return writePngRgba(texture, file);
    case TextureFormat::None:
    case TextureFormat::Count:
        break;
    }

    spdlog::error("Unsupported texture format for PNG export: {}", (int)texture.param.format);
}

void writePngIndexed(const SPLTexture& texture, const std::filesystem::path& file) {
    spng_ctx* ctx = spng_ctx_new(SPNG_CTX_ENCODER);
    if (!ctx) {
        spdlog::error("Failed to create SPNG context");
        return;
    }

    spng_set_option(ctx, SPNG_ENCODE_TO_BUFFER, true);

    spng_ihdr ihdr{};
    ihdr.width = texture.width;
    ihdr.height = texture.height;
    ihdr.color_type = SPNG_COLOR_TYPE_INDEXED;
    ihdr.bit_depth = 8;
    ihdr.interlace_method = SPNG_INTERLACE_NONE;

    spng_set_ihdr(ctx, &ihdr);

    const auto data = texture.convertTo8bpp();
    if (data.empty()) {
        spdlog::error("Failed to convert texture to 8bpp");
        spng_ctx_free(ctx);
        return;
    }

    spng_plte palette{};
    const auto paletteData = (GXRgb*)texture.paletteData.data();

    palette.n_entries = (u32)texture.getPaletteSize();
    for (size_t i = 0; i < palette.n_entries; i++) {
        const auto color = paletteData[i].toVec3();
        palette.entries[i].red = (u8)(color.r * 255.0f);
        palette.entries[i].green = (u8)(color.g * 255.0f);
        palette.entries[i].blue = (u8)(color.b * 255.0f);
    }

    spng_set_plte(ctx, &palette);

    int ret;
    if ((ret = spng_encode_image(ctx, data.data(), data.size(), SPNG_FMT_PNG, SPNG_ENCODE_FINALIZE)) != 0) {
        spdlog::error("Failed to encode texture to PNG: {}", spng_strerror(ret));
    }

    size_t pngSize;
    void* pngData = spng_get_png_buffer(ctx, &pngSize, &ret);
    if (pngData == nullptr) {
        spdlog::error("Failed to get PNG buffer: {}", spng_strerror(ret));
        spng_ctx_free(ctx);
        return;
    }

    std::ofstream outFile(file, std::ios::binary);
    if (!outFile) {
        spdlog::error("Failed to open file for writing: {}", file.string());
        free(pngData);
        spng_ctx_free(ctx);
        return;
    }

    outFile.write((const char*)pngData, pngSize);

    free(pngData);
    spng_ctx_free(ctx);
}

void writePngRgba(const SPLTexture& texture, const std::filesystem::path& file) {
    const auto rgba = texture.convertToRGBA8888();
    if (rgba.empty()) {
        spdlog::error("Failed to convert texture to RGBA8888");
        return;
    }

    spng_ctx* ctx = spng_ctx_new(SPNG_CTX_ENCODER);
    if (!ctx) {
        spdlog::error("Failed to create SPNG context");
        return;
    }

    spng_set_option(ctx, SPNG_ENCODE_TO_BUFFER, true);

    spng_ihdr ihdr{};
    ihdr.width = texture.width;
    ihdr.height = texture.height;
    ihdr.bit_depth = 8;
    ihdr.color_type = SPNG_COLOR_TYPE_TRUECOLOR_ALPHA;

    spng_set_ihdr(ctx, &ihdr);

    int ret;
    if ((ret = spng_encode_image(ctx, rgba.data(), rgba.size(), SPNG_FMT_PNG, SPNG_ENCODE_FINALIZE)) != 0) {
        spdlog::error("Failed to encode texture to PNG: {}", spng_strerror(ret));
    }

    size_t pngSize;
    void* pngData = spng_get_png_buffer(ctx, &pngSize, &ret);
    if (pngData == nullptr) {
        spdlog::error("Failed to get PNG buffer: {}", spng_strerror(ret));
        spng_ctx_free(ctx);
        return;
    }

    std::ofstream outFile(file, std::ios::binary);
    if (!outFile) {
        spdlog::error("Failed to open file for writing: {}", file.string());
        free(pngData);
        spng_ctx_free(ctx);
        return;
    }

    outFile.write((const char*)pngData, pngSize);

    free(pngData);
    spng_ctx_free(ctx);
}

}
