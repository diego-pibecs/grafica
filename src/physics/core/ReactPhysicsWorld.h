#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

#include <reactphysics3d/reactphysics3d.h>

#include "physics/core/IPhysicsWorld.h"

class ReactPhysicsWorld final : public IPhysicsWorld
{
public:
    struct BodyMetadata
    {
        std::string name;
        PhysicsAabb bounds;
        std::uint16_t categoryBits = CollisionLayers::None;
        std::uint16_t maskBits = CollisionLayers::All;
        bool blocking = true;
    };

    ReactPhysicsWorld();
    ~ReactPhysicsWorld() override;

    void Clear() override;
    void Step(float deltaTime) override;

    void AddStaticRegion(const StaticRegionDesc& region) override;
    void AddStaticPrimitive(const StaticPrimitiveDesc& primitive) override;
    void AddDynamicBody(const DynamicBodyDesc& dynamicBody) override;
    void AddTrigger(const TriggerDesc& trigger) override;

    bool Raycast(const RaycastRequest& request, RaycastHit& hit) const override;
    bool OverlapCapsule(const CapsuleQuery& query, std::vector<OverlapHit>& hits) override;
    [[nodiscard]] PhysicsDebugFrame BuildDebugFrame() const override;

    [[nodiscard]] static glm::vec3 FromRp3d(const reactphysics3d::Vector3& value);
    [[nodiscard]] static glm::vec3 DecodeColor(std::uint32_t color);

private:
    struct StaticRegionRecord
    {
        BodyMetadata metadata;
        std::vector<float> vertices;
        std::vector<int> indices;
        std::unique_ptr<reactphysics3d::TriangleVertexArray> triangleArray;
        reactphysics3d::TriangleMesh* triangleMesh = nullptr;
        reactphysics3d::ConcaveMeshShape* shape = nullptr;
        reactphysics3d::RigidBody* body = nullptr;
        reactphysics3d::Collider* collider = nullptr;
    };

    struct BoxBodyRecord
    {
        BodyMetadata metadata;
        reactphysics3d::BoxShape* shape = nullptr;
        reactphysics3d::RigidBody* body = nullptr;
        reactphysics3d::Collider* collider = nullptr;
        bool isStatic = false;
    };

    struct QueryVolume
    {
        std::string name;
        PhysicsAabb bounds;
        glm::vec3 center { 0.0f };
        glm::vec3 halfExtents { 0.5f };
        glm::quat rotation { 1.0f, 0.0f, 0.0f, 0.0f };
        bool oriented = false;
        std::uint16_t categoryBits = CollisionLayers::None;
        std::uint16_t maskBits = CollisionLayers::All;
    };

    reactphysics3d::PhysicsCommon physicsCommon_;
    reactphysics3d::PhysicsWorld* world_ = nullptr;
    std::vector<StaticRegionRecord> staticRegions_;
    std::vector<BoxBodyRecord> primitiveBodies_;
    std::vector<QueryVolume> queryVolumes_;
    std::unordered_map<const reactphysics3d::Body*, BodyMetadata> bodyMetadata_;

    void CreateWorld();
    void RegisterBodyMetadata(reactphysics3d::Body* body, const BodyMetadata& metadata);
    [[nodiscard]] static reactphysics3d::Transform ToRp3dTransform(const glm::vec3& position);
    [[nodiscard]] static reactphysics3d::Transform ToRp3dTransform(const glm::vec3& position, const glm::quat& rotation);
};
