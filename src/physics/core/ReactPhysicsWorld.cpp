#include "physics/core/ReactPhysicsWorld.h"

#include "DebugLog.h"

#include <array>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
class NearestRaycastCallback final : public reactphysics3d::RaycastCallback
{
public:
    explicit NearestRaycastCallback(const std::unordered_map<const reactphysics3d::Body*, ReactPhysicsWorld::BodyMetadata>& bodyMetadata)
        : bodyMetadata_(bodyMetadata)
    {
    }

    reactphysics3d::decimal notifyRaycastHit(const reactphysics3d::RaycastInfo& raycastInfo) override
    {
        const auto iterator = bodyMetadata_.find(raycastInfo.body);
        if (iterator == bodyMetadata_.end())
        {
            return reactphysics3d::decimal(-1.0);
        }

        hit_.hit = true;
        hit_.distance = static_cast<float>(raycastInfo.hitFraction * maxDistance_);
        hit_.point = ReactPhysicsWorld::FromRp3d(raycastInfo.worldPoint);
        hit_.normal = glm::normalize(ReactPhysicsWorld::FromRp3d(raycastInfo.worldNormal));
        hit_.colliderName = iterator->second.name;
        hit_.layerBits = iterator->second.categoryBits;
        return raycastInfo.hitFraction;
    }

    void SetMaxDistance(float maxDistance)
    {
        maxDistance_ = maxDistance;
    }

    [[nodiscard]] const RaycastHit& GetHit() const noexcept
    {
        return hit_;
    }

private:
    const std::unordered_map<const reactphysics3d::Body*, ReactPhysicsWorld::BodyMetadata>& bodyMetadata_;
    RaycastHit hit_;
    float maxDistance_ = 0.0f;
};

reactphysics3d::Vector3 ToRp3dVector(const glm::vec3& value)
{
    return reactphysics3d::Vector3(value.x, value.y, value.z);
}

void AppendDebugLine(
    PhysicsDebugFrame& frame,
    const glm::vec3& start,
    const glm::vec3& end,
    const glm::vec3& color)
{
    frame.lines.push_back(PhysicsDebugLine { start, end, color, color });
}

glm::vec3 RotateByInverse(const glm::quat& rotation, const glm::vec3& value)
{
    return glm::inverse(rotation) * value;
}

void AppendWireBox(
    PhysicsDebugFrame& frame,
    const glm::vec3& center,
    const glm::vec3& halfExtents,
    const glm::quat& rotation,
    const glm::vec3& color)
{
    const std::array<glm::vec3, 8> localCorners {
        glm::vec3(-halfExtents.x, -halfExtents.y, -halfExtents.z),
        glm::vec3( halfExtents.x, -halfExtents.y, -halfExtents.z),
        glm::vec3( halfExtents.x, -halfExtents.y,  halfExtents.z),
        glm::vec3(-halfExtents.x, -halfExtents.y,  halfExtents.z),
        glm::vec3(-halfExtents.x,  halfExtents.y, -halfExtents.z),
        glm::vec3( halfExtents.x,  halfExtents.y, -halfExtents.z),
        glm::vec3( halfExtents.x,  halfExtents.y,  halfExtents.z),
        glm::vec3(-halfExtents.x,  halfExtents.y,  halfExtents.z)
    };

    std::array<glm::vec3, 8> worldCorners {};
    for (std::size_t index = 0; index < localCorners.size(); ++index)
    {
        worldCorners[index] = center + (rotation * localCorners[index]);
    }

    constexpr std::array<std::pair<int, int>, 12> kEdges {{
        { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 },
        { 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 },
        { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 }
    }};

    for (const auto& edge : kEdges)
    {
        AppendDebugLine(frame, worldCorners[edge.first], worldCorners[edge.second], color);
    }
}

bool IntersectsVerticalCapsuleAabb(const CapsuleQuery& query, const PhysicsAabb& bounds)
{
    const float capsuleMinY = query.basePosition.y;
    const float capsuleMaxY = query.basePosition.y + query.height;
    if (capsuleMaxY < bounds.min.y || capsuleMinY > bounds.max.y)
    {
        return false;
    }

    const float closestX = std::clamp(query.basePosition.x, bounds.min.x, bounds.max.x);
    const float closestZ = std::clamp(query.basePosition.z, bounds.min.z, bounds.max.z);
    const float deltaX = query.basePosition.x - closestX;
    const float deltaZ = query.basePosition.z - closestZ;
    return ((deltaX * deltaX) + (deltaZ * deltaZ)) <= (query.radius * query.radius);
}

bool SegmentIntersectsAabb(
    const glm::vec3& start,
    const glm::vec3& end,
    const glm::vec3& minBounds,
    const glm::vec3& maxBounds)
{
    const glm::vec3 delta = end - start;
    float tMin = 0.0f;
    float tMax = 1.0f;

    for (int axis = 0; axis < 3; ++axis)
    {
        if (std::abs(delta[axis]) <= 0.000001f)
        {
            if (start[axis] < minBounds[axis] || start[axis] > maxBounds[axis])
            {
                return false;
            }
            continue;
        }

        const float inverseDelta = 1.0f / delta[axis];
        float axisT1 = (minBounds[axis] - start[axis]) * inverseDelta;
        float axisT2 = (maxBounds[axis] - start[axis]) * inverseDelta;
        if (axisT1 > axisT2)
        {
            std::swap(axisT1, axisT2);
        }

        tMin = std::max(tMin, axisT1);
        tMax = std::min(tMax, axisT2);
        if (tMin > tMax)
        {
            return false;
        }
    }

    return true;
}

bool IntersectsCapsuleOrientedBox(
    const CapsuleQuery& query,
    const glm::vec3& center,
    const glm::vec3& halfExtents,
    const glm::quat& rotation)
{
    const glm::vec3 bottomCenterWorld = query.basePosition + glm::vec3(0.0f, query.radius, 0.0f);
    const glm::vec3 topCenterWorld = query.basePosition + glm::vec3(0.0f, std::max(query.height - query.radius, query.radius), 0.0f);

    const glm::vec3 bottomCenterLocal = RotateByInverse(rotation, bottomCenterWorld - center);
    const glm::vec3 topCenterLocal = RotateByInverse(rotation, topCenterWorld - center);
    const glm::vec3 expandedHalfExtents = halfExtents + glm::vec3(query.radius);

    return SegmentIntersectsAabb(
        bottomCenterLocal,
        topCenterLocal,
        -expandedHalfExtents,
        expandedHalfExtents);
}
}

ReactPhysicsWorld::ReactPhysicsWorld()
{
    DebugLog::Info("PhysicsWorld", "Constructing ReactPhysicsWorld");
    CreateWorld();
}

ReactPhysicsWorld::~ReactPhysicsWorld()
{
    Clear();
    if (world_ != nullptr)
    {
        physicsCommon_.destroyPhysicsWorld(world_);
        world_ = nullptr;
    }
}

void ReactPhysicsWorld::CreateWorld()
{
    if (world_ != nullptr)
    {
        return;
    }

    reactphysics3d::PhysicsWorld::WorldSettings settings;
    settings.worldName = "grafica_bbb_physics";
    settings.gravity = reactphysics3d::Vector3(0.0, -9.81, 0.0);
    world_ = physicsCommon_.createPhysicsWorld(settings);
    if (world_ == nullptr)
    {
        DebugLog::Error("PhysicsWorld", "Failed to create ReactPhysics3D world");
        throw std::runtime_error("Failed to create ReactPhysics3D world");
    }
    DebugLog::Info("PhysicsWorld", "ReactPhysics3D world created");

    world_->setIsDebugRenderingEnabled(true);
    reactphysics3d::DebugRenderer& debugRenderer = world_->getDebugRenderer();
    debugRenderer.setIsDebugItemDisplayed(reactphysics3d::DebugRenderer::DebugItem::COLLISION_SHAPE, true);
    debugRenderer.setIsDebugItemDisplayed(reactphysics3d::DebugRenderer::DebugItem::COLLIDER_AABB, true);
    debugRenderer.setIsDebugItemDisplayed(reactphysics3d::DebugRenderer::DebugItem::CONTACT_POINT, true);
    debugRenderer.setIsDebugItemDisplayed(reactphysics3d::DebugRenderer::DebugItem::CONTACT_NORMAL, true);
}

void ReactPhysicsWorld::Clear()
{
    if (world_ == nullptr)
    {
        return;
    }

    for (StaticRegionRecord& region : staticRegions_)
    {
        if (region.body != nullptr)
        {
            world_->destroyRigidBody(region.body);
            region.body = nullptr;
        }
        if (region.shape != nullptr)
        {
            physicsCommon_.destroyConcaveMeshShape(region.shape);
            region.shape = nullptr;
        }
        if (region.triangleMesh != nullptr)
        {
            physicsCommon_.destroyTriangleMesh(region.triangleMesh);
            region.triangleMesh = nullptr;
        }
    }
    staticRegions_.clear();

    for (BoxBodyRecord& body : primitiveBodies_)
    {
        if (body.body != nullptr)
        {
            world_->destroyRigidBody(body.body);
            body.body = nullptr;
        }
        if (body.shape != nullptr)
        {
            physicsCommon_.destroyBoxShape(body.shape);
            body.shape = nullptr;
        }
    }
    primitiveBodies_.clear();
    queryVolumes_.clear();
    bodyMetadata_.clear();
}

void ReactPhysicsWorld::Step(float deltaTime)
{
    if (world_ == nullptr)
    {
        return;
    }

    world_->update(std::max(deltaTime, 0.0f));
}

void ReactPhysicsWorld::RegisterBodyMetadata(reactphysics3d::Body* body, const BodyMetadata& metadata)
{
    if (body != nullptr)
    {
        bodyMetadata_[body] = metadata;
        body->setUserData(nullptr);
        body->setIsDebugEnabled(true);
    }
}

reactphysics3d::Transform ReactPhysicsWorld::ToRp3dTransform(const glm::vec3& position)
{
    return reactphysics3d::Transform(
        reactphysics3d::Vector3(position.x, position.y, position.z),
        reactphysics3d::Quaternion::identity());
}

reactphysics3d::Transform ReactPhysicsWorld::ToRp3dTransform(const glm::vec3& position, const glm::quat& rotation)
{
    return reactphysics3d::Transform(
        reactphysics3d::Vector3(position.x, position.y, position.z),
        reactphysics3d::Quaternion(rotation.x, rotation.y, rotation.z, rotation.w));
}

glm::vec3 ReactPhysicsWorld::FromRp3d(const reactphysics3d::Vector3& value)
{
    return glm::vec3(value.x, value.y, value.z);
}

glm::vec3 ReactPhysicsWorld::DecodeColor(std::uint32_t color)
{
    const float red = static_cast<float>((color >> 16u) & 0xFFu) / 255.0f;
    const float green = static_cast<float>((color >> 8u) & 0xFFu) / 255.0f;
    const float blue = static_cast<float>(color & 0xFFu) / 255.0f;
    return glm::vec3(red, green, blue);
}

void ReactPhysicsWorld::AddStaticRegion(const StaticRegionDesc& region)
{
    if (world_ == nullptr || region.vertices.empty() || region.indices.empty())
    {
        DebugLog::Info(
            "PhysicsWorld",
            "Skipping static region name=", region.name,
            " world=", world_ != nullptr,
            " vertices=", region.vertices.size(),
            " indices=", region.indices.size());
        return;
    }

    DebugLog::Info(
        "PhysicsWorld",
        "AddStaticRegion name=", region.name,
        " vertices=", region.vertices.size(),
        " indices=", region.indices.size(),
        " queryProxies=", region.queryProxies.size(),
        " contributesToQueries=", region.contributesToCharacterQueries);

    StaticRegionRecord record;
    record.metadata.name = region.name;
    record.metadata.bounds = region.bounds;
    record.metadata.categoryBits = region.categoryBits;
    record.metadata.maskBits = region.maskBits;
    record.metadata.blocking = true;

    record.vertices.reserve(region.vertices.size() * 3u);
    for (const glm::vec3& vertex : region.vertices)
    {
        record.vertices.push_back(vertex.x);
        record.vertices.push_back(vertex.y);
        record.vertices.push_back(vertex.z);
    }

    record.indices.reserve(region.indices.size());
    for (std::uint32_t index : region.indices)
    {
        record.indices.push_back(static_cast<int>(index));
    }

    DebugLog::Info(
        "PhysicsWorld",
        "Static region buffers prepared name=", region.name,
        " triangleCount=", region.indices.size() / 3u,
        " vertexFloatCount=", record.vertices.size(),
        " indexCount=", record.indices.size());

    std::vector<reactphysics3d::Message> messages;
    DebugLog::Info("PhysicsWorld", "Creating TriangleVertexArray for ", region.name);
    record.triangleArray = std::make_unique<reactphysics3d::TriangleVertexArray>(
        static_cast<reactphysics3d::uint32>(region.vertices.size()),
        record.vertices.data(),
        3u * sizeof(float),
        static_cast<reactphysics3d::uint32>(region.indices.size() / 3u),
        record.indices.data(),
        3u * sizeof(int),
        reactphysics3d::TriangleVertexArray::VertexDataType::VERTEX_FLOAT_TYPE,
        reactphysics3d::TriangleVertexArray::IndexDataType::INDEX_INTEGER_TYPE);
    DebugLog::Info("PhysicsWorld", "TriangleVertexArray ready for ", region.name);

    DebugLog::Info("PhysicsWorld", "Calling createTriangleMesh() for ", region.name);
    record.triangleMesh = physicsCommon_.createTriangleMesh(*record.triangleArray, messages);
    if (record.triangleMesh == nullptr)
    {
        throw std::runtime_error("Failed to create triangle mesh for region " + region.name);
    }
    DebugLog::Info("PhysicsWorld", "Triangle mesh ready for ", region.name);

    DebugLog::Info("PhysicsWorld", "Calling createConcaveMeshShape() for ", region.name);
    record.shape = physicsCommon_.createConcaveMeshShape(record.triangleMesh);
    DebugLog::Info("PhysicsWorld", "Concave mesh shape ready for ", region.name);

    DebugLog::Info("PhysicsWorld", "Creating rigid body for ", region.name);
    record.body = world_->createRigidBody(ToRp3dTransform(glm::vec3(0.0f)));
    record.body->setType(reactphysics3d::BodyType::STATIC);
    DebugLog::Info("PhysicsWorld", "Adding collider for ", region.name);
    record.collider = record.body->addCollider(record.shape, reactphysics3d::Transform::identity());
    record.collider->setCollisionCategoryBits(region.categoryBits);
    record.collider->setCollideWithMaskBits(region.maskBits);
    DebugLog::Info("PhysicsWorld", "Collider ready for ", region.name);

    if (!messages.empty())
    {
        std::cerr << "ReactPhysics3D mesh warnings for region " << region.name << ": " << messages.size() << '\n';
        DebugLog::Info("PhysicsWorld", "Region ", region.name, " emitted ", messages.size(), " ReactPhysics3D warning(s)");
    }

    RegisterBodyMetadata(record.body, record.metadata);
    if (region.contributesToCharacterQueries)
    {
        for (const StaticQueryProxy& proxy : region.queryProxies)
        {
            queryVolumes_.push_back(QueryVolume {
                proxy.name,
                proxy.bounds,
                proxy.center,
                proxy.halfExtents,
                proxy.rotation,
                proxy.oriented,
                proxy.categoryBits,
                proxy.maskBits
            });
        }
    }
    staticRegions_.push_back(std::move(record));
    DebugLog::Info("PhysicsWorld", "Static region registered name=", region.name);
}

void ReactPhysicsWorld::AddDynamicBody(const DynamicBodyDesc& dynamicBody)
{
    DebugLog::Info("PhysicsWorld", "AddDynamicBody name=", dynamicBody.name);
    BoxBodyRecord record;
    record.metadata.name = dynamicBody.name;
    record.metadata.bounds = dynamicBody.bounds;
    record.metadata.categoryBits = dynamicBody.categoryBits;
    record.metadata.maskBits = dynamicBody.maskBits;
    record.metadata.blocking = true;

    const glm::vec3 size = dynamicBody.bounds.max - dynamicBody.bounds.min;
    const glm::vec3 halfExtents = glm::max(size * 0.5f, glm::vec3(0.05f));
    const glm::vec3 center = (dynamicBody.bounds.min + dynamicBody.bounds.max) * 0.5f;

    record.shape = physicsCommon_.createBoxShape(ToRp3dVector(halfExtents));
    record.body = world_->createRigidBody(ToRp3dTransform(center));
    record.body->setType(reactphysics3d::BodyType::KINEMATIC);
    record.isStatic = false;
    record.body->enableGravity(false);
    record.collider = record.body->addCollider(record.shape, reactphysics3d::Transform::identity());
    record.collider->setCollisionCategoryBits(dynamicBody.categoryBits);
    record.collider->setCollideWithMaskBits(dynamicBody.maskBits);

    RegisterBodyMetadata(record.body, record.metadata);
        queryVolumes_.push_back(QueryVolume {
            dynamicBody.name,
            dynamicBody.bounds,
            (dynamicBody.bounds.min + dynamicBody.bounds.max) * 0.5f,
            glm::max((dynamicBody.bounds.max - dynamicBody.bounds.min) * 0.5f, glm::vec3(0.05f)),
            glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
            false,
            dynamicBody.categoryBits,
            dynamicBody.maskBits
        });
    primitiveBodies_.push_back(std::move(record));
    DebugLog::Info("PhysicsWorld", "Dynamic body registered name=", dynamicBody.name);
}

void ReactPhysicsWorld::AddStaticPrimitive(const StaticPrimitiveDesc& primitive)
{
    if (world_ == nullptr)
    {
        DebugLog::Info("PhysicsWorld", "Skipping static primitive because world is null name=", primitive.name);
        return;
    }

    DebugLog::Info(
        "PhysicsWorld",
        "AddStaticPrimitive name=", primitive.name,
        " center=(", primitive.center.x, ", ", primitive.center.y, ", ", primitive.center.z, ")",
        " halfExtents=(", primitive.halfExtents.x, ", ", primitive.halfExtents.y, ", ", primitive.halfExtents.z, ")");

    BoxBodyRecord record;
    record.metadata.name = primitive.name;
    record.metadata.bounds = primitive.bounds;
    record.metadata.categoryBits = primitive.categoryBits;
    record.metadata.maskBits = primitive.maskBits;
    record.metadata.blocking = true;
    record.isStatic = true;

    record.shape = physicsCommon_.createBoxShape(ToRp3dVector(glm::max(primitive.halfExtents, glm::vec3(0.025f))));
    record.body = world_->createRigidBody(ToRp3dTransform(primitive.center, primitive.rotation));
    record.body->setType(reactphysics3d::BodyType::STATIC);
    record.collider = record.body->addCollider(record.shape, reactphysics3d::Transform::identity());
    record.collider->setCollisionCategoryBits(primitive.categoryBits);
    record.collider->setCollideWithMaskBits(primitive.maskBits);

    RegisterBodyMetadata(record.body, record.metadata);
    if (primitive.contributesToCharacterQueries)
    {
        queryVolumes_.push_back(QueryVolume {
            primitive.name,
            primitive.bounds,
            primitive.center,
            primitive.halfExtents,
            primitive.rotation,
            true,
            primitive.categoryBits,
            primitive.maskBits
        });
    }
    primitiveBodies_.push_back(std::move(record));
    DebugLog::Info(
        "PhysicsWorld",
        "Static primitive registered name=", primitive.name,
        " contributesToQueries=", primitive.contributesToCharacterQueries);
}

void ReactPhysicsWorld::AddTrigger(const TriggerDesc& trigger)
{
    DebugLog::Info("PhysicsWorld", "AddTrigger name=", trigger.name);
    BoxBodyRecord record;
    record.metadata.name = trigger.name;
    record.metadata.bounds = trigger.bounds;
    record.metadata.categoryBits = trigger.categoryBits;
    record.metadata.maskBits = trigger.maskBits;
    record.metadata.blocking = false;

    const glm::vec3 size = trigger.bounds.max - trigger.bounds.min;
    const glm::vec3 halfExtents = glm::max(size * 0.5f, glm::vec3(0.05f));
    const glm::vec3 center = (trigger.bounds.min + trigger.bounds.max) * 0.5f;

    record.shape = physicsCommon_.createBoxShape(ToRp3dVector(halfExtents));
    record.body = world_->createRigidBody(ToRp3dTransform(center));
    record.body->setType(reactphysics3d::BodyType::KINEMATIC);
    record.body->enableGravity(false);
    record.collider = record.body->addCollider(record.shape, reactphysics3d::Transform::identity());
    record.collider->setIsTrigger(true);
    record.collider->setCollisionCategoryBits(trigger.categoryBits);
    record.collider->setCollideWithMaskBits(trigger.maskBits);

    RegisterBodyMetadata(record.body, record.metadata);
    queryVolumes_.push_back(QueryVolume {
        trigger.name,
        trigger.bounds,
        center,
        halfExtents,
        glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
        false,
        trigger.categoryBits,
        trigger.maskBits
    });
    primitiveBodies_.push_back(std::move(record));
    DebugLog::Info("PhysicsWorld", "Trigger registered name=", trigger.name);
}

bool ReactPhysicsWorld::Raycast(const RaycastRequest& request, RaycastHit& hit) const
{
    hit = RaycastHit {};
    if (world_ == nullptr || request.maxDistance <= 0.0f || glm::dot(request.direction, request.direction) <= 0.0f)
    {
        return false;
    }

    const glm::vec3 normalizedDirection = glm::normalize(request.direction);
    const glm::vec3 target = request.origin + (normalizedDirection * request.maxDistance);
    const reactphysics3d::Ray ray(ToRp3dVector(request.origin), ToRp3dVector(target));

    NearestRaycastCallback callback(bodyMetadata_);
    callback.SetMaxDistance(request.maxDistance);
    world_->raycast(ray, &callback, request.maskBits);
    hit = callback.GetHit();
    return hit.hit;
}

bool ReactPhysicsWorld::OverlapCapsule(const CapsuleQuery& query, std::vector<OverlapHit>& hits)
{
    hits.clear();
    if (world_ == nullptr)
    {
        return false;
    }

    for (const QueryVolume& volume : queryVolumes_)
    {
        if ((volume.categoryBits & query.maskBits) == 0u)
        {
            continue;
        }

        if (!IntersectsVerticalCapsuleAabb(query, volume.bounds))
        {
            continue;
        }

        if (volume.oriented && !IntersectsCapsuleOrientedBox(query, volume.center, volume.halfExtents, volume.rotation))
        {
            continue;
        }

        hits.push_back(OverlapHit {
            volume.name,
            volume.bounds,
            volume.center,
            volume.halfExtents,
            volume.rotation,
            volume.oriented,
            volume.categoryBits
        });
    }
    return !hits.empty();
}

PhysicsDebugFrame ReactPhysicsWorld::BuildDebugFrame() const
{
    PhysicsDebugFrame frame;
    if (world_ == nullptr)
    {
        return frame;
    }

    reactphysics3d::DebugRenderer& debugRenderer = const_cast<reactphysics3d::PhysicsWorld*>(world_)->getDebugRenderer();
    const reactphysics3d::DebugRenderer::DebugLine* lines = debugRenderer.getLinesArray();
    for (reactphysics3d::uint32 index = 0; index < debugRenderer.getNbLines(); ++index)
    {
        PhysicsDebugLine line;
        line.start = FromRp3d(lines[index].point1);
        line.end = FromRp3d(lines[index].point2);
        line.startColor = DecodeColor(lines[index].color1);
        line.endColor = DecodeColor(lines[index].color2);
        frame.lines.push_back(std::move(line));
    }

    const reactphysics3d::DebugRenderer::DebugTriangle* triangles = debugRenderer.getTrianglesArray();
    for (reactphysics3d::uint32 index = 0; index < debugRenderer.getNbTriangles(); ++index)
    {
        PhysicsDebugTriangle triangle;
        triangle.a = FromRp3d(triangles[index].point1);
        triangle.b = FromRp3d(triangles[index].point2);
        triangle.c = FromRp3d(triangles[index].point3);
        triangle.colorA = DecodeColor(triangles[index].color1);
        triangle.colorB = DecodeColor(triangles[index].color2);
        triangle.colorC = DecodeColor(triangles[index].color3);
        frame.triangles.push_back(std::move(triangle));
    }

    for (const QueryVolume& volume : queryVolumes_)
    {
        const glm::vec3 color =
            (volume.categoryBits & CollisionLayers::Trigger) != 0u ? glm::vec3(0.95f, 0.35f, 0.95f) :
            (volume.categoryBits & CollisionLayers::Dynamic) != 0u ? glm::vec3(1.0f, 0.60f, 0.20f) :
            glm::vec3(0.15f, 0.95f, 0.95f);

        const glm::vec3 center = volume.oriented
            ? volume.center
            : (volume.bounds.min + volume.bounds.max) * 0.5f;
        const glm::vec3 halfExtents = volume.oriented
            ? glm::max(volume.halfExtents, glm::vec3(0.025f))
            : glm::max((volume.bounds.max - volume.bounds.min) * 0.5f, glm::vec3(0.025f));
        const glm::quat rotation = volume.oriented
            ? volume.rotation
            : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

        AppendWireBox(frame, center, halfExtents, rotation, color);
        frame.points.push_back(PhysicsDebugPoint { center, color, 5.0f });
    }

    return frame;
}
