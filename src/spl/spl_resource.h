#pragma once

#include <memory>
#include <optional>
#include <span>
#include <unordered_set>
#include <vector>

#include <glm/glm.hpp>

#include "spl_behavior.h"
#include "types.h"
#include "fx.h"
#include "gfx/gl_texture.h"


struct SPLFileHeader {
    u32 magic;
    u32 version;
    u16 resCount;
    u16 texCount;
    u32 reserved0;
    u32 resSize;
    u32 texSize;
    u32 texOffset;
    u32 reserved1;
};

enum class SPLEmissionType : u8 {
    Point = 0,
    SphereSurface,
    CircleBorder,
    CircleBorderUniform,
    Sphere,
    Circle,
    CylinderSurface,
    Cylinder,
    HemisphereSurface,
    Hemisphere
};

enum class SPLDrawType : u8 {
    Billboard = 0,
    DirectionalBillboard,
    Polygon,
    DirectionalPolygon,
    DirectionalPolygonCenter
};

enum class SPLEmissionAxis : u8 {
    Z = 0,
    Y,
    X,
    Emitter
};

enum class SPLPolygonRotAxis : u8 {
    Y = 0,
    XYZ
};

enum class SPLChildRotationType : u8 {
    None = 0,
    InheritAngle,
    InheritAngleAndVelocity
};

enum class SPLScaleAnimDir : u8 {
    XY = 0,
    X,
    Y
};

union SPLResourceFlagsNative {
    u32 all;
    struct {
        u32 emissionType : 4; // Maps to SPLEmissionType
        u32 drawType : 2;
        u32 circleAxis : 2; // Maps to SPLEmissionAxis
        u32 hasScaleAnim : 1;
        u32 hasColorAnim : 1;
        u32 hasAlphaAnim : 1;
        u32 hasTexAnim : 1;
        u32 hasRotation : 1;
        u32 randomInitAngle : 1;
        // Whether the emitter manages itself or not.
        // If set, the emitter will automatically terminate when it reaches the end of its life
        // and all of its particles have died
        u32 selfMaintaining : 1;
        u32 followEmitter : 1;
        u32 hasChildResource : 1;
        u32 polygonRotAxis : 2; // The axis to rotate the polygon around when using the 'polygon' draw types
        u32 polygonReferencePlane : 1;
        u32 randomizeLoopedAnim : 1;
        u32 drawChildrenFirst : 1; // If set, child particles will be rendered before parent particles
        u32 hideParent : 1; // If set, only child particles will be rendered
        u32 useViewSpace : 1; // If set, the rendering calculations will be done in view space
        u32 hasGravityBehavior : 1;
        u32 hasRandomBehavior : 1;
        u32 hasMagnetBehavior : 1;
        u32 hasSpinBehavior : 1;
        u32 hasCollisionPlaneBehavior : 1;
        u32 hasConvergenceBehavior : 1;
        u32 hasFixedPolygonID : 1;
        u32 childHasFixedPolygonID : 1;
    };
};

struct SPLResourceFlags {
    SPLEmissionType emissionType;
    SPLDrawType drawType;
    SPLEmissionAxis emissionAxis;
    bool hasScaleAnim;
    bool hasColorAnim;
    bool hasAlphaAnim;
    bool hasTexAnim;
    bool hasRotation;
    bool randomInitAngle;
    // Whether the emitter manages itself or not.
    // If set, the emitter will automatically terminate when it reaches the end of its life
    // and all of its particles have died
    bool selfMaintaining;
    bool followEmitter;
    bool hasChildResource;
    SPLPolygonRotAxis polygonRotAxis; // The axis to rotate the polygon around when using the 'polygon' draw types
    int polygonReferencePlane;
    bool randomizeLoopedAnim;
    bool drawChildrenFirst; // If set, child particles will be rendered before parent particles
    bool hideParent; // If set, only child particles will be rendered
    bool useViewSpace; // If set, the rendering calculations will be done in view space
    bool hasGravityBehavior;
    bool hasRandomBehavior;
    bool hasMagnetBehavior;
    bool hasSpinBehavior;
    bool hasCollisionPlaneBehavior;
    bool hasConvergenceBehavior;
    bool hasFixedPolygonID;
    bool childHasFixedPolygonID;
};

union SPLChildResourceFlagsNative {
    u16 all;
    struct {
        u16 usesBehaviors : 1;
        u16 hasScaleAnim : 1;
        u16 hasAlphaAnim : 1;
        u16 rotationType : 2;
        u16 followEmitter : 1;
        u16 useChildColor : 1;
        u16 drawType : 2;
        u16 polygonRotAxis : 2;
        u16 polygonReferencePlane : 1;
        u16 : 4;
    };
};

struct SPLChildResourceFlags {
    bool usesBehaviors;
    bool hasScaleAnim;
    bool hasAlphaAnim;
    SPLChildRotationType rotationType;
    bool followEmitter;
    bool useChildColor;
    SPLDrawType drawType;
    SPLPolygonRotAxis polygonRotAxis;
    int polygonReferencePlane; // 0=XY, 1=XZ
};

struct SPLCurveInOut {
    u8 in;
    u8 out;

    f32 getIn() const {
        return in / 255.0f;
    }

    f32 getOut() const {
        return out / 255.0f;
    }
};

struct SPLCurveInPeakOut {
    u8 in;
    u8 peak;
    u8 out;
    u8 _;

    f32 getIn() const {
        return in / 255.0f;
    }

    f32 getPeak() const {
        return peak / 255.0f;
    }

    f32 getOut() const {
        return out / 255.0f;
    }
};

struct SPLResourceHeaderNative {
    SPLResourceFlagsNative flags;
    VecFx32 emitterBasePos;
    fx32 emissionCount; // Number of particles to emit per emission interval
    fx32 radius; // Used for circle, sphere, and cylinder emissions
    fx32 length; // Used for cylinder emission
    VecFx16 axis;
    GXRgb color;
    fx32 initVelPosAmplifier;
    fx32 initVelAxisAmplifier;
    fx32 baseScale;
    fx16 aspectRatio;
    u16 startDelay; // Delay, in frames, before the emitter starts emitting particles
    s16 minRotation;
    s16 maxRotation;
    u16 initAngle;
    u16 reserved;
    u16 emitterLifeTime;
    u16 particleLifeTime;

    // All of these values are mapped to the range [0, 1]
    // They are used to attenuate the particle's properties at initialization,
    // acting as a sort of randomization factor which scales down the initial values
    struct {
        u32 baseScale : 8; // Damping factor for the base scale of the particles (0 = no damping)
        u32 lifeTime : 8;
        u32 initVel : 8; // Attenuation factor for the initial velocity of the particles (0 = no attenuation)
        u32 : 8;
    } variance;

    struct {
        u32 emissionInterval : 8;
        u32 baseAlpha : 8;
        u32 airResistance : 8; // Air resistance factor (0=0.75, 255=1.25)
        u32 textureIndex : 8;
        u32 loopFrames : 8;
        u32 dbbScale : 16;
        u32 textureTileCountS : 2; // Number of times to tile the texture in the S direction
        u32 textureTileCountT : 2; // Number of times to tile the texture in the T direction
        u32 scaleAnimDir : 3; // Maps to SPLScaleAnimDir
        u32 dpolFaceEmitter : 1; // If set, the polygon will face the emitter
        u32 flipTextureS : 1;
        u32 flipTextureT : 1;
        u32 : 30;
    } misc;
    fx16 polygonX;
    fx16 polygonY;
    struct {
        u32 flags : 8;
        u32 : 24;
    } userData;
};

struct SPLResourceHeader {
    SPLResourceFlags flags;
    glm::vec3 emitterBasePos;
    u32 emissionCount; // Number of particles to emit per emission interval
    f32 radius; // Used for circle, sphere, and cylinder emissions
    f32 length; // Used for cylinder emission
    glm::vec3 axis;
    glm::vec3 color;
    f32 initVelPosAmplifier;
    f32 initVelAxisAmplifier;
    f32 baseScale;
    f32 aspectRatio;
    f32 startDelay; // Delay, in seconds, before the emitter starts emitting particles
    f32 minRotation;
    f32 maxRotation;
    f32 initAngle;
    u16 reserved;
    f32 emitterLifeTime; // Time, in seconds, the emitter will live for
    f32 particleLifeTime; // Time, in seconds, the particles will live for

    // All of these values are mapped to the range [0, 1]
    // They are used to attenuate the particle's properties at initialization,
    // acting as a sort of randomization factor which scales down the initial values
    struct {
        f32 baseScale; // Damping factor for the base scale of the particles (0 = no damping)
        f32 lifeTime;
        f32 initVel; // Attenuation factor for the initial velocity of the particles (0 = no attenuation)
    } variance;

    struct {
        f32 emissionInterval; // Time, in seconds, between particle emissions
        f32 baseAlpha;
        f32 airResistance; // Air resistance factor (0.75-1.25)
        u8 textureIndex;
        f32 loopTime; // Time, in seconds, for the texture animation to loop
        f32 dbbScale;
        u8 textureTileCountS; // Number of times to tile the texture in the S direction
        u8 textureTileCountT; // Number of times to tile the texture in the T direction
        SPLScaleAnimDir scaleAnimDir;
        bool dpolFaceEmitter; // If set, the polygon will face the emitter
        bool flipTextureS;
        bool flipTextureT;
    } misc;
    f32 polygonX;
    f32 polygonY;
    struct {
        u8 flags;
        u8 _[3];
    } userData;

    void addScaleAnim() { flags.hasScaleAnim = true; }
    void removeScaleAnim() { flags.hasScaleAnim = false; }
    void addColorAnim() { flags.hasColorAnim = true; }
    void removeColorAnim() { flags.hasColorAnim = false; }
    void addAlphaAnim() { flags.hasAlphaAnim = true; }
    void removeAlphaAnim() { flags.hasAlphaAnim = false; }
    void addTexAnim() { flags.hasTexAnim = true; }
    void removeTexAnim() { flags.hasTexAnim = false; }

    void addBehavior(SPLBehaviorType type) {
        switch (type) {
        case SPLBehaviorType::Gravity: flags.hasGravityBehavior = true; break;
        case SPLBehaviorType::Random: flags.hasRandomBehavior = true; break;
        case SPLBehaviorType::Magnet: flags.hasMagnetBehavior = true; break;
        case SPLBehaviorType::Spin: flags.hasSpinBehavior = true; break;
        case SPLBehaviorType::CollisionPlane: flags.hasCollisionPlaneBehavior = true; break;
        case SPLBehaviorType::Convergence: flags.hasConvergenceBehavior = true; break;
        }
    }

    void removeBehavior(SPLBehaviorType type) {
        switch (type) {
        case SPLBehaviorType::Gravity: flags.hasGravityBehavior = false; break;
        case SPLBehaviorType::Random: flags.hasRandomBehavior = false; break;
        case SPLBehaviorType::Magnet: flags.hasMagnetBehavior = false; break;
        case SPLBehaviorType::Spin: flags.hasSpinBehavior = false; break;
        case SPLBehaviorType::CollisionPlane: flags.hasCollisionPlaneBehavior = false; break;
        case SPLBehaviorType::Convergence: flags.hasConvergenceBehavior = false; break;
        }
    }
};

struct SPLResource;
struct SPLAnim {
    virtual void apply(SPLParticle& ptcl, const SPLResource& resource, f32 lifeRate) const = 0;
};

struct SPLScaleAnimNative {
    fx16 start;
    fx16 mid;
    fx16 end;
    SPLCurveInOut curve;
    struct {
        u16 loop : 1;
        u16 : 15;
    } flags;
    u16 padding;
};

struct SPLScaleAnim final : SPLAnim {
    f32 start;
    f32 mid;
    f32 end;
    SPLCurveInOut curve;
    struct {
        bool loop;
    } flags;

    explicit SPLScaleAnim(const SPLScaleAnimNative& native) {
        start = FX_FX32_TO_F32(native.start);
        mid = FX_FX32_TO_F32(native.mid);
        end = FX_FX32_TO_F32(native.end);
        curve = native.curve;
        flags.loop = native.flags.loop;
    }

    void apply(SPLParticle& ptcl, const SPLResource& resource, f32 lifeRate) const override;
    void plot(std::span<f32> xs, std::span<f32> ys) const;

    static SPLScaleAnim createDefault() {
        return SPLScaleAnim(SPLScaleAnimNative{
            .start = FX16_CONST(1.0f),
            .mid = FX16_CONST(1.0f),
            .end = FX16_CONST(1.0f),
            .curve = { .in = 0, .out = 255 },
            .flags = { .loop = false }
        });
    }
};

struct SPLColorAnimNative {
    GXRgb start;
    GXRgb end;
    SPLCurveInPeakOut curve;
    struct {
        u16 randomStartColor : 1;
        u16 loop : 1;
        u16 interpolate : 1;
        u16 : 13;
    } flags;
    u16 padding;
};

struct SPLColorAnim final : SPLAnim {
    glm::vec3 start;
    glm::vec3 end;
    SPLCurveInPeakOut curve;
    struct {
        bool randomStartColor;
        bool loop;
        bool interpolate;
    } flags;

    explicit SPLColorAnim(const SPLColorAnimNative& native) {
        start = native.start.toVec3();
        end = native.end.toVec3();
        curve = native.curve;
        flags.randomStartColor = native.flags.randomStartColor;
        flags.loop = native.flags.loop;
        flags.interpolate = native.flags.interpolate;
    }

    SPLColorAnim(const glm::vec3& start, const glm::vec3& end, const SPLCurveInPeakOut& curve, decltype(flags) flags)
        : start(start), end(end), curve(curve), flags(flags) {}

    void apply(SPLParticle& ptcl, const SPLResource& resource, f32 lifeRate) const override;
    void plot(const SPLResource& resource, std::span<f32> xs, std::span<glm::vec3> ys) const;

    static SPLColorAnim createDefault() {
        return SPLColorAnim(
            { 1.0f, 1.0f, 1.0f },
            { 1.0f, 1.0f, 1.0f },
            { .in = 0, .peak = 127, .out = 255 },
            { .randomStartColor = false, .loop = false, .interpolate = true }
        );
    }
};

struct SPLAlphaAnimNative {
    union {
        u16 all;
        struct {
            u16 start : 5;
            u16 mid : 5;
            u16 end : 5;
            u16 : 1;
        };
    } alpha;
    struct {
        u16 randomRange : 8;
        u16 loop : 1;
        u16 : 7;
    } flags;
    SPLCurveInOut curve;
    u16 padding;
};

struct SPLAlphaAnim final : SPLAnim {
    struct {
        f32 start;
        f32 mid;
        f32 end;
    } alpha;
    struct {
        float randomRange;
        bool loop;
    } flags;
    SPLCurveInOut curve;

    explicit SPLAlphaAnim(const SPLAlphaAnimNative& native) {
        alpha.start = (f32)native.alpha.start / 31.0f;
        alpha.mid = (f32)native.alpha.mid / 31.0f;
        alpha.end = (f32)native.alpha.end / 31.0f;
        flags.randomRange = (f32)native.flags.randomRange / 255.0f;
        flags.loop = native.flags.loop;
        curve = native.curve;
    }

    SPLAlphaAnim(const glm::vec3& alpha, const SPLCurveInOut& curve, decltype(flags) flags)
        : alpha({ alpha.r, alpha.g, alpha.b }), curve(curve), flags(flags) {}

    void apply(SPLParticle& ptcl, const SPLResource& resource, f32 lifeRate) const override;
    void plot(std::span<f32> xs, std::span<f32> ys) const;

    static SPLAlphaAnim createDefault() {
        return SPLAlphaAnim(
            { 1.0f, 15.0f / 31.0f, 0.0f },
            { .in = 128, .out = 128 },
            { .randomRange = 0.0f, .loop = false }
        );
    }
};

struct SPLTexAnimNative {
    u8 textures[8];
    struct {
        u32 frameCount : 8;
        u32 step : 8; // Number of frames between each texture frame
        u32 randomizeInit : 1; // Randomize the initial texture frame
        u32 loop : 1;
        u32 : 14;
    } param;
};

struct SPLTexAnim final : SPLAnim {
    constexpr static size_t MAX_TEXTURES = 8;

    u8 textures[MAX_TEXTURES];
    struct {
        u8 textureCount;
        f32 step; // Fraction of the particles lifetime for which each frame lasts
        bool randomizeInit; // Randomize the initial texture frame
        bool loop;
    } param;

    explicit SPLTexAnim(const SPLTexAnimNative& native) {
        std::ranges::copy(native.textures, std::begin(textures));
        param.textureCount = native.param.frameCount;
        param.step = native.param.step / 255.0f;
        param.randomizeInit = native.param.randomizeInit;
        param.loop = native.param.loop;
    }

    void addTexture(u8 texture = 0) {
        if (param.textureCount < MAX_TEXTURES) {
            textures[param.textureCount++] = texture;
        }
    }

    void removeTexture(size_t index) {
        if (index < param.textureCount) {
            param.textureCount--;
            std::memmove(textures + index, textures + index + 1, (param.textureCount - index) * sizeof(u8));
        }
    }

    void apply(SPLParticle& ptcl, const SPLResource& resource, f32 lifeRate) const override;

    static SPLTexAnim createDefault() {
        return SPLTexAnim(SPLTexAnimNative{
            .textures = { 0, 0, 0, 0, 0, 0, 0, 0 },
            .param = { .frameCount = 1, .step = 255, .randomizeInit = false, .loop = false }
        });
    }
};

struct SPLChildResourceNative {
    SPLChildResourceFlagsNative flags;
    fx16 randomInitVelMag; // Randomization factor for the initial velocity magnitude (0 = no randomization)
    fx16 endScale; // For scaling animations
    u16 lifeTime;
    u8 velocityRatio; // Ratio of the parent particle's velocity to inherit (255 = 100%)
    u8 scaleRatio; // Ratio of the parent particle's scale to inherit (255 = 100%)
    GXRgb color;
    struct {
        u32 emissionCount : 8; // Number of particles to emit per emission interval
        u32 emissionDelay : 8; // Delay, as a fraction of the particle's lifetime, before the particle starts emitting
        u32 emissionInterval : 8;
        u32 texture : 8;
        u32 textureTileCountS : 2;
        u32 textureTileCountT : 2;
        u32 flipTextureS : 1;
        u32 flipTextureT : 1;
        u32 dpolFaceEmitter : 1; // If set, the polygon will face the emitter
        u32 : 25;
    } misc;
};

struct SPLChildResource {
    SPLChildResourceFlags flags;
    f32 randomInitVelMag; // Randomization factor for the initial velocity magnitude (0 = no randomization)
    f32 endScale; // For scaling animations
    f32 lifeTime; // Time, in seconds, the particles will live for
    f32 velocityRatio; // Ratio of the parent particle's velocity to inherit (1 = 100%)
    f32 scaleRatio; // Ratio of the parent particle's scale to inherit (1 = 100%)
    glm::vec3 color;
    struct {
        u32 emissionCount; // Number of particles to emit per emission interval
        f32 emissionDelay; // Delay, as a fraction of the particle's lifetime, before the particle starts emitting
        f32 emissionInterval; // Time, in seconds, between particle emissions
        u8 texture;
        u8 textureTileCountS;
        u8 textureTileCountT;
        bool flipTextureS;
        bool flipTextureT;
        bool dpolFaceEmitter; // If set, the polygon will face the emitter
    } misc;

    void applyScaleAnim(SPLParticle& ptcl, f32 lifeRate) const;
    void applyAlphaAnim(SPLParticle& ptcl, f32 lifeRate) const;
};

union SPLTextureParamNative {
    u32 all;
    struct {
        u32 format : 4; // Maps to GXTexFmt
        u32 s : 4; // Maps to GXTexSizeS
        u32 t : 4; // Maps to GXTexSizeT
        u32 repeat : 2; // Maps to GXTexRepeat
        u32 flip : 2; // Maps to GXTexFlip
        u32 palColor0 : 1; // Maps to GXTexPlttColor0
        u32 useSharedTexture : 1;
        u32 sharedTexID : 8;
        u32 : 6;
    };
};

struct SPLTextureParam {
    TextureFormat format;
    u8 s; // Maps to GXTexSizeS
    u8 t; // Maps to GXTexSizeT
    TextureRepeat repeat;
    TextureFlip flip;
    bool palColor0Transparent;
    bool useSharedTexture;
    u8 sharedTexID;
};

struct SPLTextureResource {
    u32 magic;
    SPLTextureParamNative param;
    u32 textureSize; // size of the texture data
    u32 paletteOffset; // offset to the palette data from the start of the header
    u32 paletteSize; // size of the palette data
    u32 unused0;
    u32 unused1;
    u32 resourceSize; // total size of the resource (header + data)
};

enum class TextureConversionPreference {
    ColorDepth,
    AlphaDepth,
};

enum class TextureAttributes {
    None = 0,
    HasTransparentPixels = 1 << 0,
    HasTranslucentPixels = 1 << 1,
};

struct TextureImportSpecification {
    bool color0Transparent;
    bool requiresColorCompression;
    bool requiresAlphaCompression;
    TextureFormat format;
    std::unordered_set<u32> uniqueColors;
    std::unordered_set<u8> uniqueAlphas;
    TextureAttributes flags;

    void setFormat(TextureFormat format);
    int getMaxColors() const;
    int getMaxAlphas() const;
    std::pair<int, int> getAlphaRange() const;
    bool needsAlpha() const;

    size_t getSizeEstimate(size_t width, size_t height) const;
};

struct SPLTextureCopy;
struct SPLTexture {
    SPLTexture() = default;
    SPLTexture(const SPLTexture&) = default;
    SPLTexture(SPLTexture&& other) noexcept;
    SPLTexture& operator=(const SPLTexture&) = default;
    SPLTexture& operator=(SPLTexture&& other) noexcept;

    const SPLTextureResource* resource;
    SPLTextureParam param;
    u16 width;
    u16 height;
    std::span<const u8> textureData;
    std::span<const u8> paletteData;
    std::shared_ptr<GLTexture> glTexture;

    std::vector<u8> convertToRGBA8888() const;
    std::vector<u8> convertTo8bpp() const;
    size_t getPaletteSize() const;

    SPLTextureCopy copy() const;

    static TextureImportSpecification suggestSpecification(
        s32 width, 
        s32 height, 
        s32 channels, 
        const u8* data,
        TextureConversionPreference preference
    );

    static bool convertFromRGBA8888(
        const u8* data,
        s32 width,
        s32 height,
        TextureFormat format,
        std::vector<u8>& outData,
        std::vector<u8>& outPalette
    );
};

struct SPLTextureCopy {
    std::vector<u8> data;
    std::vector<u8> pltt;
    SPLTexture texture;
};

//using SPLScaleAnim = SPLScaleAnimTemplate<f32>;
//using SPLScaleAnimNative = SPLScaleAnimTemplate<fx16>;
//
//using SPLColorAnim = SPLColorAnimTemplate<glm::vec3>;
//using SPLColorAnimNative = SPLColorAnimTemplate<GXRgb>;
//
//using SPLAlphaAnimNative = SPLAlphaAnim;
//
//using SPLTexAnimNative = SPLTexAnim;
//
//using SPLChildResource = SPLChildResourceTemplate<f32, glm::vec3>;
//using SPLChildResourceNative = SPLChildResourceTemplate<fx16, GXRgb>;

struct SPLResource {
    SPLResourceHeader header;
    std::optional<SPLScaleAnim> scaleAnim;
    std::optional<SPLColorAnim> colorAnim;
    std::optional<SPLAlphaAnim> alphaAnim;
    std::optional<SPLTexAnim> texAnim;
    std::optional<SPLChildResource> childResource;
    std::vector<std::shared_ptr<SPLBehavior>> behaviors;

    void addScaleAnim(const SPLScaleAnim& anim) { scaleAnim = anim; header.addScaleAnim(); }
    void addColorAnim(const SPLColorAnim& anim) { colorAnim = anim; header.addColorAnim(); }
    void addAlphaAnim(const SPLAlphaAnim& anim) { alphaAnim = anim; header.addAlphaAnim(); }
    void addTexAnim(const SPLTexAnim& anim) { texAnim = anim; header.addTexAnim(); }

    void removeScaleAnim() { scaleAnim.reset(); header.removeScaleAnim(); }
    void removeColorAnim() { colorAnim.reset(); header.removeColorAnim(); }
    void removeAlphaAnim() { alphaAnim.reset(); header.removeAlphaAnim(); }
    void removeTexAnim() { texAnim.reset(); header.removeTexAnim(); }

    template<typename T = SPLBehavior> requires std::derived_from<T, SPLBehavior>
    std::shared_ptr<T> getBehavior(SPLBehaviorType type) const {
        for (const auto& behavior : behaviors) {
            if (behavior->type == type) {
                return std::dynamic_pointer_cast<T>(behavior);
            }
        }

        return nullptr;
    }

    bool hasBehavior(SPLBehaviorType type) const {
        return getBehavior(type) != nullptr;
    }

    SPLResource duplicate() const;

    static SPLResource create();
};

struct SPLResourceCopy {
    std::string source;
    std::unique_ptr<SPLResource> resource;
    std::unique_ptr<SPLTextureCopy> texture;
};
