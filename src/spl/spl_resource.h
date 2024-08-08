#pragma once

#include <memory>
#include <optional>
#include <span>
#include <vector>

#include <glm/glm.hpp>

#include "spl_behavior.h"
#include "types.h"
#include "fx.h"
#include "gl_texture.h"

 

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

enum class SPLCircleAxis : u8 {
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
        u32 circleAxis : 2; // Maps to SPLCircleAxis
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
    SPLCircleAxis circleAxis;
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
    u8 polygonReferencePlane;
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
    u8 polygonReferencePlane; // 0=XY, 1=XZ
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
    } randomAttenuation;

    struct {
        u32 emissionInterval : 8;
        u32 baseAlpha : 8;
        u32 airResistance : 8;
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
    f32 emissionCount; // Number of particles to emit per emission interval
    f32 radius; // Used for circle, sphere, and cylinder emissions
    f32 length; // Used for cylinder emission
    glm::vec3 axis;
    glm::vec3 color;
    f32 initVelPosAmplifier;
    f32 initVelAxisAmplifier;
    f32 baseScale;
    f32 aspectRatio;
    f32 startDelay; // Delay, in seconds, before the emitter starts emitting particles
    s16 minRotation;
    s16 maxRotation;
    u16 initAngle;
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
    } randomAttenuation;

    struct {
        f32 emissionInterval; // Time, in seconds, between particle emissions
        f32 baseAlpha;
        u8 airResistance;
        u8 textureIndex;
        f32 loopTime; // Time, in seconds, for the texture animation to loop
        u16 dbbScale;
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
};

struct SPLResource;
struct SPLAnim {
    virtual void apply(SPLParticle& ptcl, SPLResource& resource, f32 lifeRate) = 0;
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

    void apply(SPLParticle& ptcl, SPLResource& resource, f32 lifeRate) override;
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

    void apply(SPLParticle& ptcl, SPLResource& resource, f32 lifeRate) override;
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

    void apply(SPLParticle& ptcl, SPLResource& resource, f32 lifeRate) override;
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
    u8 textures[8];
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

    void apply(SPLParticle& ptcl, SPLResource& resource, f32 lifeRate) override;
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
        u8 emissionCount; // Number of particles to emit per emission interval
        f32 emissionDelay; // Delay, as a fraction of the particle's lifetime, before the particle starts emitting
        f32 emissionInterval; // Time, in seconds, between particle emissions
        u8 texture;
        u8 textureTileCountS;
        u8 textureTileCountT;
        bool flipTextureS;
        bool flipTextureT;
        bool dpolFaceEmitter; // If set, the polygon will face the emitter
    } misc;

    void applyScaleAnim(SPLParticle& ptcl, f32 lifeRate);
    void applyAlphaAnim(SPLParticle& ptcl, f32 lifeRate);
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
    u32 id;
    SPLTextureParamNative param;
    u32 textureSize; // size of the texture data
    u32 paletteOffset; // offset to the palette data from the start of the header
    u32 paletteSize; // size of the palette data
    u32 unused0;
    u32 unused1;
    u32 resourceSize; // total size of the resource (header + data)
};

struct SPLTexture {
    const SPLTextureResource* resource;
    SPLTextureParam param;
    u16 width;
    u16 height;
    std::span<const u8> textureData;
    std::span<const u8> paletteData;
    std::shared_ptr<GLTexture> glTexture;
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

    std::shared_ptr<SPLTexture> texture;
};
