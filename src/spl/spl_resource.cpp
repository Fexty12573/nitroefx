#include "spl_resource.h"


SPLResource SPLResource::duplicate() const {
    SPLResource res{
        .header = header,
        .scaleAnim = scaleAnim,
        .colorAnim = colorAnim,
        .alphaAnim = alphaAnim,
        .texAnim = texAnim,
        .childResource = childResource,
    };

    for (auto& anim : behaviors) {
        if (anim->type == SPLBehaviorType::Gravity) {
            res.behaviors.push_back(std::make_shared<SPLGravityBehavior>(*std::dynamic_pointer_cast<SPLGravityBehavior>(anim)));
        } else if (anim->type == SPLBehaviorType::Random) {
            res.behaviors.push_back(std::make_shared<SPLRandomBehavior>(*std::dynamic_pointer_cast<SPLRandomBehavior>(anim)));
        } else if (anim->type == SPLBehaviorType::Magnet) {
            res.behaviors.push_back(std::make_shared<SPLMagnetBehavior>(*std::dynamic_pointer_cast<SPLMagnetBehavior>(anim)));
        } else if (anim->type == SPLBehaviorType::Spin) {
            res.behaviors.push_back(std::make_shared<SPLSpinBehavior>(*std::dynamic_pointer_cast<SPLSpinBehavior>(anim)));
        } else if (anim->type == SPLBehaviorType::CollisionPlane) {
            res.behaviors.push_back(std::make_shared<SPLCollisionPlaneBehavior>(*std::dynamic_pointer_cast<SPLCollisionPlaneBehavior>(anim)));
        } else if (anim->type == SPLBehaviorType::Convergence) {
            res.behaviors.push_back(std::make_shared<SPLConvergenceBehavior>(*std::dynamic_pointer_cast<SPLConvergenceBehavior>(anim)));
        }
    }

    return res;
}

SPLResource SPLResource::create() {
    SPLResource res{};
    std::memset(&res.header, 0, sizeof(res.header));

    res.header.emissionCount = 1;
    res.header.color = { 1.0f, 1.0f, 1.0f };
    res.header.baseScale = 1.0f;
    res.header.aspectRatio = 1.0f;
    res.header.emitterLifeTime = 0.1f;
    res.header.particleLifeTime = 0.1f;
    res.header.variance.lifeTime = 0.0f;
    res.header.misc.emissionInterval = 0.1f;
    res.header.misc.baseAlpha = 1.0f;

    return res;
}
