#pragma once

#include <vector>

#include "physics/core/PhysicsTypes.h"

class IPhysicsWorld
{
public:
    virtual ~IPhysicsWorld() = default;

    virtual void Clear() = 0;
    virtual void Step(float deltaTime) = 0;

    virtual void AddStaticRegion(const StaticRegionDesc& region) = 0;
    virtual void AddStaticPrimitive(const StaticPrimitiveDesc& primitive) = 0;
    virtual void AddDynamicBody(const DynamicBodyDesc& dynamicBody) = 0;
    virtual void AddTrigger(const TriggerDesc& trigger) = 0;

    virtual bool Raycast(const RaycastRequest& request, RaycastHit& hit) const = 0;
    virtual bool OverlapCapsule(const CapsuleQuery& query, std::vector<OverlapHit>& hits) = 0;

    [[nodiscard]] virtual PhysicsDebugFrame BuildDebugFrame() const = 0;
};
