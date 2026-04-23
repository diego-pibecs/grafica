#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

struct PhysicsAabb
{
    glm::vec3 min { 0.0f };
    glm::vec3 max { 0.0f };
};

struct PhysicsDebugPoint
{
    glm::vec3 position { 0.0f };
    glm::vec3 color { 1.0f };
    float size = 6.0f;
};

struct PhysicsDebugLine
{
    glm::vec3 start { 0.0f };
    glm::vec3 end { 0.0f };
    glm::vec3 startColor { 1.0f };
    glm::vec3 endColor { 1.0f };
};

struct PhysicsDebugTriangle
{
    glm::vec3 a { 0.0f };
    glm::vec3 b { 0.0f };
    glm::vec3 c { 0.0f };
    glm::vec3 colorA { 1.0f };
    glm::vec3 colorB { 1.0f };
    glm::vec3 colorC { 1.0f };
};

struct PhysicsDebugFrame
{
    std::vector<PhysicsDebugPoint> points;
    std::vector<PhysicsDebugLine> lines;
    std::vector<PhysicsDebugTriangle> triangles;

    void Clear()
    {
        points.clear();
        lines.clear();
        triangles.clear();
    }
};

namespace CollisionLayers
{
constexpr std::uint16_t None = 0u;
constexpr std::uint16_t WorldStatic = 1u << 0u;
constexpr std::uint16_t Actor = 1u << 1u;
constexpr std::uint16_t Trigger = 1u << 2u;
constexpr std::uint16_t Dynamic = 1u << 3u;
constexpr std::uint16_t Query = 1u << 4u;
constexpr std::uint16_t All = 0xFFFFu;
}

struct RaycastRequest
{
    glm::vec3 origin { 0.0f };
    glm::vec3 direction { 0.0f, -1.0f, 0.0f };
    float maxDistance = 0.0f;
    std::uint16_t maskBits = CollisionLayers::All;
};

struct RaycastHit
{
    bool hit = false;
    float distance = 0.0f;
    glm::vec3 point { 0.0f };
    glm::vec3 normal { 0.0f, 1.0f, 0.0f };
    std::string colliderName;
    std::uint16_t layerBits = CollisionLayers::None;
};

struct CapsuleQuery
{
    glm::vec3 basePosition { 0.0f };
    float radius = 0.35f;
    float height = 1.8f;
    std::uint16_t maskBits = CollisionLayers::All;
};

struct OverlapHit
{
    std::string colliderName;
    PhysicsAabb bounds;
    glm::vec3 center { 0.0f };
    glm::vec3 halfExtents { 0.5f };
    glm::quat rotation { 1.0f, 0.0f, 0.0f, 0.0f };
    bool oriented = false;
    std::uint16_t layerBits = CollisionLayers::None;
};

struct StaticQueryProxy
{
    std::string name;
    PhysicsAabb bounds;
    glm::vec3 center { 0.0f };
    glm::vec3 halfExtents { 0.5f };
    glm::quat rotation { 1.0f, 0.0f, 0.0f, 0.0f };
    bool oriented = false;
    std::uint16_t categoryBits = CollisionLayers::WorldStatic;
    std::uint16_t maskBits = CollisionLayers::All;
};

struct SweepHit
{
    bool hit = false;
    float fraction = 1.0f;
    glm::vec3 point { 0.0f };
    glm::vec3 normal { 0.0f, 1.0f, 0.0f };
    std::string colliderName;
};

struct StaticRegionDesc
{
    std::string name;
    std::string regionId;
    std::vector<glm::vec3> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<StaticQueryProxy> queryProxies;
    PhysicsAabb bounds;
    std::uint16_t categoryBits = CollisionLayers::WorldStatic;
    std::uint16_t maskBits = CollisionLayers::All;
    bool contributesToCharacterQueries = false;
};

struct StaticPrimitiveDesc
{
    std::string name;
    glm::vec3 center { 0.0f };
    glm::vec3 halfExtents { 0.5f };
    glm::quat rotation { 1.0f, 0.0f, 0.0f, 0.0f };
    PhysicsAabb bounds;
    std::uint16_t categoryBits = CollisionLayers::WorldStatic;
    std::uint16_t maskBits = CollisionLayers::All;
    bool contributesToCharacterQueries = false;
};

struct DynamicBodyDesc
{
    std::string name;
    PhysicsAabb bounds;
    std::uint16_t categoryBits = CollisionLayers::Dynamic;
    std::uint16_t maskBits = CollisionLayers::All;
};

struct TriggerDesc
{
    std::string name;
    PhysicsAabb bounds;
    std::uint16_t categoryBits = CollisionLayers::Trigger;
    std::uint16_t maskBits = CollisionLayers::Actor | CollisionLayers::Query;
};
