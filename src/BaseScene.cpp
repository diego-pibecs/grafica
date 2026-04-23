#include "BaseScene.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <utility>

#include <glm/gtc/matrix_transform.hpp>

#include "CameraController.h"
#include "DebugLog.h"
#include "TextureUtils.h"
#include "render/debug/PhysicsDebugRenderer.h"

namespace
{
namespace fs = std::filesystem;

std::string ToLowerAscii(std::string value)
{
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char character)
        {
            return static_cast<char>(std::tolower(character));
        });
    return value;
}

std::vector<Vertex> MakeVertices(const std::initializer_list<float>& rawData)
{
    std::vector<Vertex> vertices;
    vertices.reserve(rawData.size() / 8);

    auto iterator = rawData.begin();
    while (iterator != rawData.end())
    {
        Vertex vertex {};
        vertex.position = glm::vec3(*iterator, *(iterator + 1), *(iterator + 2));
        vertex.normal = glm::vec3(*(iterator + 3), *(iterator + 4), *(iterator + 5));
        vertex.texCoords = glm::vec2(*(iterator + 6), *(iterator + 7));
        vertices.push_back(vertex);
        iterator += 8;
    }

    return vertices;
}

glm::vec2 Rotate2D(const glm::vec2& value, float radians)
{
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    return glm::vec2((value.x * c) - (value.y * s), (value.x * s) + (value.y * c));
}

bool RayIntersectsDoorBlocker(
    const glm::vec3& rayOrigin,
    const glm::vec3& rayDirection,
    const WalkableBlocker& blocker,
    float maxDistance,
    float& hitDistance)
{
    if (!blocker.enabled || glm::dot(rayDirection, rayDirection) < 0.000001f)
    {
        return false;
    }

    const glm::vec3 direction = glm::normalize(rayDirection);
    const float inverseYaw = -glm::radians(blocker.yawDegrees);
    const glm::vec2 localOrigin2 = Rotate2D(glm::vec2(rayOrigin.x, rayOrigin.z) - glm::vec2(blocker.center.x, blocker.center.z), inverseYaw);
    const glm::vec2 localDirection2 = Rotate2D(glm::vec2(direction.x, direction.z), inverseYaw);
    const glm::vec3 localOrigin(localOrigin2.x, rayOrigin.y - blocker.center.y, localOrigin2.y);
    const glm::vec3 localDirection(localDirection2.x, direction.y, localDirection2.y);
    const glm::vec3 halfExtents = blocker.halfExtents + glm::vec3(0.16f, 0.12f, 0.16f);

    float tMin = 0.0f;
    float tMax = maxDistance;
    auto clipAxis = [&](float origin, float directionValue, float extent) -> bool
    {
        if (std::abs(directionValue) < 0.000001f)
        {
            return origin >= -extent && origin <= extent;
        }

        float t1 = (-extent - origin) / directionValue;
        float t2 = (extent - origin) / directionValue;
        if (t1 > t2)
        {
            std::swap(t1, t2);
        }

        tMin = std::max(tMin, t1);
        tMax = std::min(tMax, t2);
        return tMin <= tMax;
    };

    if (!clipAxis(localOrigin.x, localDirection.x, halfExtents.x)
        || !clipAxis(localOrigin.y, localDirection.y, halfExtents.y)
        || !clipAxis(localOrigin.z, localDirection.z, halfExtents.z))
    {
        return false;
    }

    hitDistance = std::clamp(tMin, 0.0f, maxDistance);
    return hitDistance <= maxDistance;
}

float DoorEasedProgress(float openProgress)
{
    return openProgress * openProgress * (3.0f - (2.0f * openProgress));
}

float SpecularStrengthForEntity(const std::string& name)
{
    const std::string lowerName = ToLowerAscii(name);
    if (lowerName.find("tree") != std::string::npos
        || lowerName.find("bush") != std::string::npos
        || lowerName.find("flower") != std::string::npos
        || lowerName.find("rock") != std::string::npos)
    {
        return 0.0f;
    }
    if (lowerName.find("house") != std::string::npos)
    {
        return 0.04f;
    }
    if (lowerName.find("street-light") != std::string::npos)
    {
        return 0.18f;
    }
    if (lowerName.find("dumpster") != std::string::npos)
    {
        return 0.06f;
    }
    return 0.05f;
}

PhysicsAabb MakeEmptyPhysicsBounds()
{
    PhysicsAabb bounds;
    bounds.min = glm::vec3(std::numeric_limits<float>::max());
    bounds.max = glm::vec3(std::numeric_limits<float>::lowest());
    return bounds;
}

void ExpandPhysicsBounds(PhysicsAabb& bounds, const glm::vec3& point)
{
    bounds.min = glm::min(bounds.min, point);
    bounds.max = glm::max(bounds.max, point);
}

PhysicsAabb ComputeBounds(const std::vector<glm::vec3>& vertices)
{
    PhysicsAabb bounds = MakeEmptyPhysicsBounds();
    for (const glm::vec3& vertex : vertices)
    {
        ExpandPhysicsBounds(bounds, vertex);
    }
    return bounds;
}

void AppendBoxGeometry(StaticRegionDesc& region, const glm::vec3& center, const glm::vec3& halfExtents)
{
    const std::uint32_t base = static_cast<std::uint32_t>(region.vertices.size());
    const std::array<glm::vec3, 8> corners {
        center + glm::vec3(-halfExtents.x, -halfExtents.y, -halfExtents.z),
        center + glm::vec3( halfExtents.x, -halfExtents.y, -halfExtents.z),
        center + glm::vec3( halfExtents.x,  halfExtents.y, -halfExtents.z),
        center + glm::vec3(-halfExtents.x,  halfExtents.y, -halfExtents.z),
        center + glm::vec3(-halfExtents.x, -halfExtents.y,  halfExtents.z),
        center + glm::vec3( halfExtents.x, -halfExtents.y,  halfExtents.z),
        center + glm::vec3( halfExtents.x,  halfExtents.y,  halfExtents.z),
        center + glm::vec3(-halfExtents.x,  halfExtents.y,  halfExtents.z)
    };
    region.vertices.insert(region.vertices.end(), corners.begin(), corners.end());

    const std::array<std::uint32_t, 36> indices {
        0u, 2u, 1u, 0u, 3u, 2u,
        4u, 5u, 6u, 4u, 6u, 7u,
        0u, 4u, 7u, 0u, 7u, 3u,
        1u, 2u, 6u, 1u, 6u, 5u,
        3u, 7u, 6u, 3u, 6u, 2u,
        0u, 1u, 5u, 0u, 5u, 4u
    };
    for (std::uint32_t index : indices)
    {
        region.indices.push_back(base + index);
    }
}

std::array<glm::vec3, 4> BuildBoundaryWallCenters(
    const glm::vec3& sceneMin,
    const glm::vec3& sceneMax,
    float wallHeight,
    float wallThickness)
{
    const float centerZ = (sceneMin.z + sceneMax.z) * 0.5f;
    const float centerX = (sceneMin.x + sceneMax.x) * 0.5f;
    const float wallY = wallHeight * 0.5f;

    return {
        glm::vec3(sceneMin.x - (wallThickness * 0.5f), wallY, centerZ),
        glm::vec3(sceneMax.x + (wallThickness * 0.5f), wallY, centerZ),
        glm::vec3(centerX, wallY, sceneMin.z - (wallThickness * 0.5f)),
        glm::vec3(centerX, wallY, sceneMax.z + (wallThickness * 0.5f))
    };
}
}

BaseScene::BaseScene(std::filesystem::path assetsRoot)
    : assetsRoot_(std::move(assetsRoot))
{
}

void BaseScene::Init()
{
    DebugLog::ScopedTrace trace("BaseScene", "Init");
    litShader_ = std::make_unique<ShaderProgram>(
        assetsRoot_ / "shaders" / "lit.vs",
        assetsRoot_ / "shaders" / "lit.frag");
    lightMarkerShader_ = std::make_unique<ShaderProgram>(
        assetsRoot_ / "shaders" / "light_marker.vs",
        assetsRoot_ / "shaders" / "light_marker.frag");
    skyboxShader_ = std::make_unique<ShaderProgram>(
        assetsRoot_ / "shaders" / "skybox.vs",
        assetsRoot_ / "shaders" / "skybox.frag");
    physicsDebugRenderer_ = std::make_unique<PhysicsDebugRenderer>(assetsRoot_);

    const GLuint grassTexture = LoadTexture2D(assetsRoot_ / "textures" / "grass" / "Grass006_1K-JPG_Color.jpg");
    const GLuint fenceTexture = LoadTexture2D(assetsRoot_ / "textures" / "fence-texture.png");
    const float wallLengthZ = (sceneBoundsMax_.z - sceneBoundsMin_.z) + (boundaryWallThickness_ * 2.0f);
    const float wallLengthX = (sceneBoundsMax_.x - sceneBoundsMin_.x) + (boundaryWallThickness_ * 2.0f);
    floorMesh_ = std::make_unique<Mesh>(CreateFloorMesh(grassTexture, floorHalfExtents_, floorUvTiling_));
    boundarySideWallMesh_ = std::make_unique<Mesh>(CreateTexturedBoxMesh(
        fenceTexture,
        "fence-texture.png",
        glm::vec3(boundaryWallThickness_, boundaryWallHeight_, wallLengthZ),
        6.0f));
    boundaryEndWallMesh_ = std::make_unique<Mesh>(CreateTexturedBoxMesh(
        fenceTexture,
        "fence-texture.png",
        glm::vec3(wallLengthX, boundaryWallHeight_, boundaryWallThickness_),
        6.0f));
    lightMarkerMesh_ = std::make_unique<Mesh>(CreateCubeMesh());
    skyboxMesh_ = std::make_unique<Mesh>(CreateCubeMesh());

    skyCloudTextureA_ = LoadTexture2D(assetsRoot_ / "textures" / "skybox" / "434P-111_town_sky_cloud_A.png");
    skyCloudTextureB_ = LoadTexture2D(assetsRoot_ / "textures" / "skybox" / "434P-111_town_sky_cloud_D.png");
    DebugLog::Info("BaseScene", "Core shaders and textures loaded");

    LoadEntities();
    DebugLog::Info("BaseScene", "Init complete with ", entities_.size(), " visual entit(ies)");
}

void BaseScene::Update(const PlayerSnapshot& player, float absoluteTimeSeconds, float deltaTimeSeconds)
{
    (void)player;
    absoluteTimeSeconds_ = absoluteTimeSeconds;

    const float dt = std::clamp(deltaTimeSeconds, 0.0f, 0.1f);
    for (InteractiveDoor& door : doors_)
    {
        const float target = door.open ? 1.0f : 0.0f;
        if (door.openProgress < target)
        {
            door.openProgress = std::min(target, door.openProgress + (door.animationSpeed * dt));
        }
        else if (door.openProgress > target)
        {
            door.openProgress = std::max(target, door.openProgress - (door.animationSpeed * dt));
        }
    }
}

void BaseScene::Render(const CameraController& camera, const glm::mat4& projection) const
{
    static std::uint64_t renderCallCount = 0;
    ++renderCallCount;
    const bool traceRenderCall = renderCallCount <= 180u || (renderCallCount % 120u) == 0u;
    if (traceRenderCall)
    {
        DebugLog::Info("BaseScene", "Render call ", renderCallCount, " begin");
    }

    const glm::mat4 view = camera.GetViewMatrix();

    glDepthMask(GL_FALSE);
    glDepthFunc(GL_LEQUAL);
    skyboxShader_->Use();
    skyboxShader_->SetMat4("projection", projection);
    skyboxShader_->SetMat4("view", glm::mat4(glm::mat3(view)));
    skyboxShader_->SetInt("cloudTextureA", 0);
    skyboxShader_->SetInt("cloudTextureB", 1);
    skyboxShader_->SetFloat("time", absoluteTimeSeconds_);
    skyboxShader_->SetVec3("horizonColor", glm::vec3(0.66f, 0.84f, 0.98f));
    skyboxShader_->SetVec3("zenithColor", glm::vec3(0.20f, 0.46f, 0.78f));
    skyboxShader_->SetVec3("groundColor", glm::vec3(0.79f, 0.90f, 0.98f));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, skyCloudTextureA_);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, skyCloudTextureB_);
    skyboxMesh_->Draw();
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    if (traceRenderCall)
    {
        DebugLog::Info("BaseScene", "Render call ", renderCallCount, " skybox done");
    }

    litShader_->Use();
    litShader_->SetMat4("projection", projection);
    litShader_->SetMat4("view", view);
    litShader_->SetVec3("lightPos", lightPosition_);
    litShader_->SetVec3("viewPos", camera.GetPosition());
    litShader_->SetVec3("lightColor", glm::vec3(1.0f, 0.96f, 0.90f));
    litShader_->SetVec3("ambientColor", glm::vec3(0.18f, 0.20f, 0.24f));
    litShader_->SetFloat("shininess", 24.0f);
    litShader_->SetInt("texture_diffuse1", 0);

    litShader_->SetBool("useTexture", true);
    litShader_->SetFloat("specularStrength", 0.0f);
    litShader_->SetVec3("baseColor", glm::vec3(1.0f));
    litShader_->SetMat4("model", glm::mat4(1.0f));
    floorMesh_->Draw();
    if (traceRenderCall)
    {
        DebugLog::Info("BaseScene", "Render call ", renderCallCount, " floor done");
    }

    if (boundarySideWallMesh_ != nullptr && boundaryEndWallMesh_ != nullptr)
    {
        litShader_->SetBool("useTexture", true);
        litShader_->SetFloat("specularStrength", 0.0f);
        litShader_->SetVec3("baseColor", glm::vec3(1.0f));
        const std::array<glm::vec3, 4> wallCenters = BuildBoundaryWallCenters(
            sceneBoundsMin_,
            sceneBoundsMax_,
            boundaryWallHeight_,
            boundaryWallThickness_);
        for (std::size_t wallIndex = 0; wallIndex < wallCenters.size(); ++wallIndex)
        {
            litShader_->SetMat4("model", glm::translate(glm::mat4(1.0f), wallCenters[wallIndex]));
            if (wallIndex < 2u)
            {
                boundarySideWallMesh_->Draw();
            }
            else
            {
                boundaryEndWallMesh_->Draw();
            }
        }
        if (traceRenderCall)
        {
            DebugLog::Info("BaseScene", "Render call ", renderCallCount, " boundary walls done");
        }
    }

    for (const SceneEntity& entity : entities_)
    {
        if (entity.placement.model == nullptr)
        {
            continue;
        }

        litShader_->SetBool("useTexture", entity.placement.model->HasTextures());
        litShader_->SetFloat("specularStrength", SpecularStrengthForEntity(entity.name));
        litShader_->SetVec3("baseColor", glm::vec3(0.92f, 0.86f, 0.72f));
        litShader_->SetMat4("model", BuildStaticModelMatrix(entity));
        entity.placement.model->Draw();
        if (traceRenderCall)
        {
            DebugLog::Info("BaseScene", "Render call ", renderCallCount, " entity drawn ", entity.name);
        }
    }

    for (const InteractiveDoor& door : doors_)
    {
        if (door.placement.model == nullptr)
        {
            continue;
        }

        litShader_->SetBool("useTexture", door.placement.model->HasTextures());
        litShader_->SetFloat("specularStrength", 0.04f);
        litShader_->SetVec3("baseColor", glm::vec3(0.95f, 0.82f, 0.58f));
        litShader_->SetMat4("model", BuildDoorModelMatrix(door));
        door.placement.model->Draw();
        if (traceRenderCall)
        {
            DebugLog::Info("BaseScene", "Render call ", renderCallCount, " door drawn ", door.name);
        }
    }

    lightMarkerShader_->Use();
    lightMarkerShader_->SetMat4("projection", projection);
    lightMarkerShader_->SetMat4("view", view);
    lightMarkerShader_->SetVec3("color", glm::vec3(1.0f, 0.93f, 0.70f));
    glm::mat4 lightMarkerModel = glm::translate(glm::mat4(1.0f), lightPosition_);
    lightMarkerModel = glm::scale(lightMarkerModel, glm::vec3(0.18f));
    lightMarkerShader_->SetMat4("model", lightMarkerModel);
    lightMarkerMesh_->Draw();
    if (traceRenderCall)
    {
        DebugLog::Info("BaseScene", "Render call ", renderCallCount, " light marker done");
    }

    if (physicsDebugEnabled_ && physicsDebugRenderer_ != nullptr)
    {
        physicsDebugRenderer_->Render(physicsDebugFrame_, view, projection);
        if (traceRenderCall)
        {
            DebugLog::Info("BaseScene", "Render call ", renderCallCount, " physics debug done");
        }
    }

    if (traceRenderCall)
    {
        DebugLog::Info("BaseScene", "Render call ", renderCallCount, " end");
    }
}

void BaseScene::SetPhysicsDebugFrame(PhysicsDebugFrame frame)
{
    physicsDebugFrame_ = std::move(frame);
}

void BaseScene::SetPhysicsDebugEnabled(bool enabled)
{
    physicsDebugEnabled_ = enabled;
}

bool BaseScene::TryInteract(const glm::vec3& rayOrigin, const glm::vec3& rayDirection, const glm::vec3& playerPosition)
{
    InteractiveDoor* targetedDoor = nullptr;
    float nearestHitDistance = std::numeric_limits<float>::max();
    constexpr float kMaxDoorRayDistance = 4.2f;

    for (InteractiveDoor& door : doors_)
    {
        if (door.placement.model == nullptr)
        {
            continue;
        }

        const WalkableBlocker blocker = BuildDoorBlocker(door);
        const glm::vec2 player(playerPosition.x, playerPosition.z);
        const glm::vec2 center(blocker.center.x, blocker.center.z);
        const float playerDistance = glm::length(player - center);
        const float doorReach = door.interactRadius + std::max(blocker.halfExtents.x, blocker.halfExtents.z);
        if (playerDistance > doorReach)
        {
            continue;
        }

        float hitDistance = 0.0f;
        if (RayIntersectsDoorBlocker(rayOrigin, rayDirection, blocker, kMaxDoorRayDistance, hitDistance)
            && hitDistance < nearestHitDistance)
        {
            nearestHitDistance = hitDistance;
            targetedDoor = &door;
        }
    }

    if (targetedDoor == nullptr)
    {
        DebugLog::Info("BaseScene", "Interact requested but no door is targeted by the camera ray");
        return false;
    }

    targetedDoor->open = !targetedDoor->open;
    DebugLog::Info(
        "BaseScene",
        "Door interaction ", targetedDoor->name,
        " open=", targetedDoor->open,
        " rayHit=", nearestHitDistance);
    return true;
}

const std::string& BaseScene::GetActiveModelLabel() const noexcept
{
    return activeModelLabel_;
}

glm::vec3 BaseScene::GetSceneBoundsMin() const noexcept
{
    return sceneBoundsMin_;
}

glm::vec3 BaseScene::GetSceneBoundsMax() const noexcept
{
    return sceneBoundsMax_;
}

glm::vec3 BaseScene::GetSuggestedPlayerSpawnPosition() const noexcept
{
    return playerSpawnPosition_;
}

float BaseScene::GetSuggestedPlayerSpawnYawDegrees() const noexcept
{
    return playerSpawnYawDegrees_;
}

const std::vector<SceneCollisionSource>& BaseScene::GetStaticCollisionSources() const noexcept
{
    return staticCollisionSources_;
}

StaticRegionDesc BaseScene::BuildFloorCollisionRegion() const
{
    StaticRegionDesc region;
    region.name = "scene-floor-and-boundary";
    region.regionId = "scene-boundary";
    region.categoryBits = CollisionLayers::WorldStatic;
    region.maskBits = CollisionLayers::Actor | CollisionLayers::Dynamic | CollisionLayers::Query;
    region.vertices = {
        glm::vec3(sceneBoundsMin_.x, 0.0f, sceneBoundsMin_.z),
        glm::vec3(sceneBoundsMax_.x, 0.0f, sceneBoundsMin_.z),
        glm::vec3(sceneBoundsMax_.x, 0.0f, sceneBoundsMax_.z),
        glm::vec3(sceneBoundsMin_.x, 0.0f, sceneBoundsMax_.z)
    };
    region.indices = { 0, 2, 1, 0, 3, 2 };

    const float wallHalfThickness = boundaryWallThickness_ * 0.5f;
    const float wallHalfHeight = boundaryWallHeight_ * 0.5f;
    const float centerZ = (sceneBoundsMin_.z + sceneBoundsMax_.z) * 0.5f;
    const float centerX = (sceneBoundsMin_.x + sceneBoundsMax_.x) * 0.5f;
    const float wallLengthZ = (sceneBoundsMax_.z - sceneBoundsMin_.z) + (boundaryWallThickness_ * 2.0f);
    const float wallLengthX = (sceneBoundsMax_.x - sceneBoundsMin_.x) + (boundaryWallThickness_ * 2.0f);
    AppendBoxGeometry(
        region,
        glm::vec3(sceneBoundsMin_.x - wallHalfThickness, wallHalfHeight, centerZ),
        glm::vec3(wallHalfThickness, wallHalfHeight, wallLengthZ * 0.5f));
    AppendBoxGeometry(
        region,
        glm::vec3(sceneBoundsMax_.x + wallHalfThickness, wallHalfHeight, centerZ),
        glm::vec3(wallHalfThickness, wallHalfHeight, wallLengthZ * 0.5f));
    AppendBoxGeometry(
        region,
        glm::vec3(centerX, wallHalfHeight, sceneBoundsMin_.z - wallHalfThickness),
        glm::vec3(wallLengthX * 0.5f, wallHalfHeight, wallHalfThickness));
    AppendBoxGeometry(
        region,
        glm::vec3(centerX, wallHalfHeight, sceneBoundsMax_.z + wallHalfThickness),
        glm::vec3(wallLengthX * 0.5f, wallHalfHeight, wallHalfThickness));

    region.bounds = ComputeBounds(region.vertices);
    region.bounds.min.y = std::min(region.bounds.min.y, -0.02f);
    region.contributesToCharacterQueries = false;
    return region;
}

std::vector<WalkableBlocker> BaseScene::BuildWalkableBlockers() const
{
    std::vector<WalkableBlocker> blockers;
    blockers.reserve(doors_.size());
    for (const InteractiveDoor& door : doors_)
    {
        if (door.placement.model == nullptr || door.openProgress >= 0.82f)
        {
            continue;
        }

        WalkableBlocker blocker = BuildDoorBlocker(door);
        blocker.enabled = true;
        blockers.push_back(std::move(blocker));
    }
    return blockers;
}

void BaseScene::LoadEntities()
{
    DebugLog::Info("BaseScene", "Loading scene entities");
    entities_.clear();
    doors_.clear();
    staticCollisionSources_.clear();
    LoadHouseDemo();
    LoadExteriorDecorations();
}

void BaseScene::LoadHouseDemo()
{
    DebugLog::ScopedTrace trace("BaseScene", "LoadHouseDemo");
    SceneEntity houseEntity;
    houseEntity.name = "house/source/example16_var1.fbx";
    houseEntity.worldPosition = glm::vec3(0.0f, 0.0f, sceneBoundsMin_.z + 14.0f);
    houseEntity.worldYawDegrees = -90.0f;

    SetupPlacement(
        houseEntity,
        assetsRoot_ / "models" / "house" / "source" / "example16_var1.fbx",
        10.0f,
        0.0f,
        true,
        true);

    if (houseEntity.placement.model == nullptr)
    {
        throw std::runtime_error("No se pudo cargar la casa example16_var1.fbx.");
    }

    const glm::vec3 scaledSize = houseEntity.placement.model->GetSize() * houseEntity.placement.scale;
    DebugLog::Info(
        "BaseScene",
        "House placement ready scale=", houseEntity.placement.scale,
        " size=(",
        scaledSize.x, ", ", scaledSize.y, ", ", scaledSize.z, ")");

    RegisterStaticCollisionSource(houseEntity);

    LoadHouseDoors(houseEntity);
    entities_.push_back(std::move(houseEntity));
    playerSpawnPosition_ = glm::vec3(0.0f, 0.0f, sceneBoundsMax_.z - 10.0f);
    playerSpawnYawDegrees_ = -90.0f;
    activeModelLabel_ = "Outdoor path | Recast nav + interactive doors";
    DebugLog::Info(
        "BaseScene",
        "House demo loaded collisionSources=", staticCollisionSources_.size(),
        " spawn=(",
        playerSpawnPosition_.x, ", ", playerSpawnPosition_.y, ", ", playerSpawnPosition_.z, ")");
}

void BaseScene::LoadExteriorDecorations()
{
    DebugLog::ScopedTrace trace("BaseScene", "LoadExteriorDecorations");

    struct DecorationDesc
    {
        const char* name;
        fs::path path;
        glm::vec3 position;
        float yawDegrees;
        float targetSize;
        bool normalizeToHeight;
    };

    const std::array<DecorationDesc, 30> decorations {
        DecorationDesc { "tree-left-start", assetsRoot_ / "models" / "tree.fbx", glm::vec3(-13.8f, 0.0f, 26.5f), 18.0f, 7.0f, true },
        DecorationDesc { "tree-right-start", assetsRoot_ / "models" / "tree.fbx", glm::vec3(11.9f, 0.0f, 28.0f), -14.0f, 7.3f, true },
        DecorationDesc { "tree-left-upper", assetsRoot_ / "models" / "tree.fbx", glm::vec3(-12.2f, 0.0f, 18.0f), 47.0f, 6.8f, true },
        DecorationDesc { "tree-right-upper", assetsRoot_ / "models" / "tree.fbx", glm::vec3(14.1f, 0.0f, 12.5f), -32.0f, 7.5f, true },
        DecorationDesc { "tree-left-curve", assetsRoot_ / "models" / "tree.fbx", glm::vec3(-14.0f, 0.0f, 8.4f), -61.0f, 6.5f, true },
        DecorationDesc { "tree-right-curve", assetsRoot_ / "models" / "tree.fbx", glm::vec3(12.6f, 0.0f, 3.0f), 75.0f, 7.0f, true },
        DecorationDesc { "tree-left-mid", assetsRoot_ / "models" / "tree.fbx", glm::vec3(-12.7f, 0.0f, -3.0f), 64.0f, 6.8f, true },
        DecorationDesc { "tree-right-mid", assetsRoot_ / "models" / "tree.fbx", glm::vec3(14.2f, 0.0f, -8.5f), 8.0f, 7.2f, true },
        DecorationDesc { "tree-left-late", assetsRoot_ / "models" / "tree.fbx", glm::vec3(-14.1f, 0.0f, -12.7f), 115.0f, 6.7f, true },
        DecorationDesc { "tree-right-late", assetsRoot_ / "models" / "tree.fbx", glm::vec3(11.8f, 0.0f, -15.5f), -88.0f, 7.4f, true },
        DecorationDesc { "tree-left-house", assetsRoot_ / "models" / "tree.fbx", glm::vec3(-12.5f, 0.0f, -22.0f), -26.0f, 6.9f, true },
        DecorationDesc { "tree-right-house", assetsRoot_ / "models" / "tree.fbx", glm::vec3(13.7f, 0.0f, -24.8f), 32.0f, 7.1f, true },
        DecorationDesc { "tree-left-back", assetsRoot_ / "models" / "tree.fbx", glm::vec3(-14.3f, 0.0f, -29.0f), 41.0f, 6.4f, true },
        DecorationDesc { "tree-right-back", assetsRoot_ / "models" / "tree.fbx", glm::vec3(12.4f, 0.0f, -29.5f), -53.0f, 6.6f, true },
        DecorationDesc { "bush-berries-start", assetsRoot_ / "models" / "bushes" / "BushWithBerrys03.fbx", glm::vec3(-9.8f, 0.0f, 23.0f), -12.0f, 2.0f, true },
        DecorationDesc { "bush-right-start", assetsRoot_ / "models" / "bushes" / "Bush02.fbx", glm::vec3(10.4f, 0.0f, 21.0f), 41.0f, 1.8f, true },
        DecorationDesc { "bush-left-center", assetsRoot_ / "models" / "bushes" / "BushWithBerrys01.fbx", glm::vec3(-10.6f, 0.0f, 7.0f), 23.0f, 1.8f, true },
        DecorationDesc { "bush-right-center", assetsRoot_ / "models" / "bushes" / "Bush03.fbx", glm::vec3(9.8f, 0.0f, -1.0f), -28.0f, 1.9f, true },
        DecorationDesc { "bush-left-house", assetsRoot_ / "models" / "bushes" / "BushWithBerrys02.fbx", glm::vec3(-10.8f, 0.0f, -13.0f), 16.0f, 1.9f, true },
        DecorationDesc { "bush-right-house", assetsRoot_ / "models" / "bushes" / "Bush01.fbx", glm::vec3(10.6f, 0.0f, -19.0f), -46.0f, 1.8f, true },
        DecorationDesc { "rock-left-start", assetsRoot_ / "models" / "rocks" / "SM_Rocks_01.fbx", glm::vec3(-7.4f, 0.0f, 26.0f), 18.0f, 1.6f, false },
        DecorationDesc { "rock-right-path", assetsRoot_ / "models" / "rocks" / "SM_Rocks_03.fbx", glm::vec3(7.2f, 0.0f, 13.0f), -36.0f, 1.8f, false },
        DecorationDesc { "rock-left-mid", assetsRoot_ / "models" / "rocks" / "SM_Rocks_02.fbx", glm::vec3(-7.0f, 0.0f, -7.0f), 54.0f, 1.5f, false },
        DecorationDesc { "rock-right-house", assetsRoot_ / "models" / "rocks" / "SM_Rocks_05.fbx", glm::vec3(7.0f, 0.0f, -25.0f), 72.0f, 1.7f, false },
        DecorationDesc { "street-light-start", assetsRoot_ / "models" / "street-light" / "source" / "Street Light" / "SM Street Light.fbx", glm::vec3(11.0f, 0.0f, 25.0f), -90.0f, 5.8f, true },
        DecorationDesc { "street-light-mid", assetsRoot_ / "models" / "street-light" / "source" / "Street Light" / "SM Street Light.fbx", glm::vec3(-11.0f, 0.0f, -4.0f), 90.0f, 5.5f, true },
        DecorationDesc { "dumpster-left", assetsRoot_ / "models" / "dumpster.fbx", glm::vec3(-11.5f, 0.0f, 12.0f), 88.0f, 2.8f, false },
        DecorationDesc { "flower-left-house", assetsRoot_ / "models" / "flower" / "source" / "x3.fbx", glm::vec3(-8.4f, 0.0f, -5.0f), 12.0f, 1.2f, true },
        DecorationDesc { "flower-right-house", assetsRoot_ / "models" / "flower" / "source" / "x3.fbx", glm::vec3(8.4f, 0.0f, -5.5f), -18.0f, 1.2f, true },
        DecorationDesc { "bush-back-right", assetsRoot_ / "models" / "bushes" / "BushWithBerrys03.fbx", glm::vec3(10.8f, 0.0f, -28.0f), 31.0f, 1.7f, true }
    };

    for (const DecorationDesc& decoration : decorations)
    {
        AddStaticSceneEntity(
            decoration.name,
            decoration.path,
            decoration.position,
            decoration.yawDegrees,
            decoration.targetSize,
            0.0f,
            decoration.normalizeToHeight,
            true);
    }

    DebugLog::Info(
        "BaseScene",
        "Exterior decorations loaded, visualEntities=", entities_.size(),
        " collisionSources=", staticCollisionSources_.size());
}

void BaseScene::LoadHouseDoors(const SceneEntity& houseEntity)
{
    const std::array<fs::path, 3> doorPaths {
        assetsRoot_ / "models" / "house" / "source" / "garage_door.fbx",
        assetsRoot_ / "models" / "house" / "source" / "living-room_door.fbx",
        assetsRoot_ / "models" / "house" / "source" / "kitchen_door.fbx"
    };

    for (const fs::path& path : doorPaths)
    {
        if (!fs::exists(path))
        {
            DebugLog::Info("BaseScene", "Door asset not found, skipping ", path.string());
            continue;
        }

        InteractiveDoor door;
        door.name = path.filename().string();
        door.worldPosition = houseEntity.worldPosition;
        door.worldYawDegrees = houseEntity.worldYawDegrees;
        door.placement.sourcePath = path;
        door.placement.scale = houseEntity.placement.scale;
        door.placement.rawOffset = houseEntity.placement.rawOffset;
        door.placement.yawOffsetDegrees = houseEntity.placement.yawOffsetDegrees;
        door.placement.model = std::make_unique<Model>(path, true);
        if (!door.placement.model->IsLoaded())
        {
            DebugLog::Error("BaseScene", "Failed to load door ", path.string());
            continue;
        }

        door.localMin = door.placement.model->GetMinBounds();
        door.localMax = door.placement.model->GetMaxBounds();
        const glm::vec3 localCenter = (door.localMin + door.localMax) * 0.5f;
        const glm::vec3 localSize = door.localMax - door.localMin;
        door.localHinge = localCenter;
        if (localSize.x >= localSize.z)
        {
            door.localHinge.x = door.localMin.x;
        }
        else
        {
            door.localHinge.z = door.localMin.z;
        }

        const std::string lowerName = ToLowerAscii(door.name);
        if (lowerName.find("kitchen") == std::string::npos
            && lowerName.find("garage") == std::string::npos)
        {
            door.motionType = DoorMotionType::Slide;
            door.openAngleDegrees = 0.0f;
            door.animationSpeed = 2.2f;
            constexpr float kSlidingDoorVisibleFraction = 0.10f;
            door.slideDistance = std::max(localSize.x, localSize.z) * door.placement.scale * (1.0f - kSlidingDoorVisibleFraction);
            door.localSlideDirection = localSize.x >= localSize.z
                ? glm::vec3(1.0f, 0.0f, 0.0f)
                : glm::vec3(0.0f, 0.0f, 1.0f);
        }
        else
        {
            door.openAngleDegrees *= -1.0f;
        }

        DebugLog::Info(
            "BaseScene",
            "Door loaded ", door.name,
            " localSize=(",
            localSize.x, ", ", localSize.y, ", ", localSize.z,
            ") motion=", door.motionType == DoorMotionType::Slide ? "slide" : "swing");
        doors_.push_back(std::move(door));
    }
}

void BaseScene::AddStaticSceneEntity(
    const std::string& name,
    const fs::path& path,
    const glm::vec3& worldPosition,
    float worldYawDegrees,
    float targetSize,
    float yawOffsetDegrees,
    bool normalizeToHeight,
    bool loadTextures)
{
    if (!fs::exists(path))
    {
        DebugLog::Info("BaseScene", "Decoration asset not found, skipping ", path.string());
        return;
    }

    SceneEntity entity;
    entity.name = name;
    entity.worldPosition = worldPosition;
    entity.worldYawDegrees = worldYawDegrees;
    SetupPlacement(entity, path, targetSize, yawOffsetDegrees, normalizeToHeight, loadTextures);
    if (entity.placement.model == nullptr)
    {
        DebugLog::Error("BaseScene", "Skipping decoration with failed model load ", path.string());
        return;
    }

    RegisterStaticCollisionSource(entity);
    DebugLog::Info(
        "BaseScene",
        "Static decoration loaded ", name,
        " position=(",
        worldPosition.x, ", ", worldPosition.y, ", ", worldPosition.z,
        ") scale=", entity.placement.scale);
    entities_.push_back(std::move(entity));
}

void BaseScene::RegisterStaticCollisionSource(const SceneEntity& entity)
{
    if (entity.placement.model == nullptr)
    {
        return;
    }

    SceneCollisionSource collisionSource;
    collisionSource.name = entity.name;
    collisionSource.sourcePath = entity.placement.sourcePath;
    collisionSource.collisionProfilePath = entity.placement.sourcePath;
    collisionSource.collisionProfilePath.replace_extension(".collision.json");
    collisionSource.transform = BuildStaticModelMatrix(entity);
    staticCollisionSources_.push_back(std::move(collisionSource));
}

void BaseScene::SetupPlacement(
    SceneEntity& entity,
    const fs::path& path,
    float targetSize,
    float yawOffsetDegrees,
    bool normalizeToHeight,
    bool loadTextures)
{
    DebugLog::Info(
        "BaseScene",
        "SetupPlacement path=", path.string(),
        " targetSize=", targetSize,
        " normalizeToHeight=", normalizeToHeight,
        " loadTextures=", loadTextures);
    entity.placement.sourcePath = path;
    entity.placement.model = std::make_unique<Model>(path, loadTextures);
    if (!entity.placement.model->IsLoaded())
    {
        DebugLog::Error("BaseScene", "Failed placement load for ", path.string());
        entity.placement.model.reset();
        return;
    }

    const glm::vec3 size = entity.placement.model->GetSize();
    const float referenceSize = normalizeToHeight
        ? std::max(size.y, 0.001f)
        : std::max({ size.x, size.y, size.z, 0.001f });
    entity.placement.scale = targetSize / referenceSize;

    const glm::vec3 center = entity.placement.model->GetCenter();
    const glm::vec3 minBounds = entity.placement.model->GetMinBounds();
    entity.placement.rawOffset = glm::vec3(-center.x, -minBounds.y, -center.z);
    entity.placement.yawOffsetDegrees = yawOffsetDegrees;
    DebugLog::Info(
        "BaseScene",
        "Placement loaded path=", path.string(),
        " scale=", entity.placement.scale,
        " center=(",
        center.x, ", ", center.y, ", ", center.z, ")");
}

glm::mat4 BaseScene::BuildStaticModelMatrix(const SceneEntity& entity) const
{
    glm::mat4 model = glm::translate(glm::mat4(1.0f), entity.worldPosition);
    model = glm::rotate(model, glm::radians(entity.worldYawDegrees + entity.placement.yawOffsetDegrees), glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::scale(model, glm::vec3(entity.placement.scale));
    model = glm::translate(model, entity.placement.rawOffset);
    return model;
}

glm::mat4 BaseScene::BuildDoorModelMatrix(const InteractiveDoor& door) const
{
    const float easedProgress = DoorEasedProgress(door.openProgress);

    glm::mat4 model = glm::translate(glm::mat4(1.0f), door.worldPosition);
    model = glm::rotate(model, glm::radians(door.worldYawDegrees + door.placement.yawOffsetDegrees), glm::vec3(0.0f, 1.0f, 0.0f));
    if (door.motionType == DoorMotionType::Slide)
    {
        const glm::vec3 slideDirection = glm::dot(door.localSlideDirection, door.localSlideDirection) > 0.000001f
            ? glm::normalize(door.localSlideDirection)
            : glm::vec3(1.0f, 0.0f, 0.0f);
        model = glm::translate(model, slideDirection * door.slideDistance * easedProgress);
    }
    model = glm::scale(model, glm::vec3(door.placement.scale));
    model = glm::translate(model, door.placement.rawOffset);
    if (door.motionType == DoorMotionType::Swing)
    {
        const float angle = glm::radians(door.openAngleDegrees * easedProgress);
        model = glm::translate(model, door.localHinge);
        model = glm::rotate(model, angle, glm::vec3(0.0f, 1.0f, 0.0f));
        model = glm::translate(model, -door.localHinge);
    }
    return model;
}

WalkableBlocker BaseScene::BuildDoorBlocker(const InteractiveDoor& door) const
{
    WalkableBlocker blocker;
    blocker.name = door.name;
    blocker.enabled = true;

    const glm::mat4 model = BuildDoorModelMatrix(door);
    const glm::vec3 localCenter = (door.localMin + door.localMax) * 0.5f;
    const glm::vec3 localHalf = (door.localMax - door.localMin) * 0.5f;
    blocker.center = glm::vec3(model * glm::vec4(localCenter, 1.0f));

    const glm::vec3 axisX = glm::vec3(model * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
    const glm::vec3 axisY = glm::vec3(model * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));
    const glm::vec3 axisZ = glm::vec3(model * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f));
    const float scaleX = std::max(glm::length(axisX), 0.001f);
    const float scaleY = std::max(glm::length(axisY), 0.001f);
    const float scaleZ = std::max(glm::length(axisZ), 0.001f);

    blocker.halfExtents = glm::vec3(
        std::max(localHalf.x * scaleX, 0.05f),
        std::max(localHalf.y * scaleY, 0.40f),
        std::max(localHalf.z * scaleZ, 0.05f));
    const glm::vec3 normalizedAxisX = glm::normalize(axisX);
    blocker.yawDegrees = glm::degrees(std::atan2(normalizedAxisX.z, normalizedAxisX.x));
    return blocker;
}

Mesh BaseScene::CreateFloorMesh(GLuint textureId, const glm::vec2& halfExtents, const glm::vec2& uvTiling)
{
    std::vector<Vertex> vertices = MakeVertices({
        -halfExtents.x, 0.0f, -halfExtents.y, 0.0f, 1.0f, 0.0f,        0.0f,        0.0f,
         halfExtents.x, 0.0f, -halfExtents.y, 0.0f, 1.0f, 0.0f, uvTiling.x,        0.0f,
         halfExtents.x, 0.0f,  halfExtents.y, 0.0f, 1.0f, 0.0f, uvTiling.x, uvTiling.y,
        -halfExtents.x, 0.0f,  halfExtents.y, 0.0f, 1.0f, 0.0f,        0.0f, uvTiling.y
    });

    std::vector<unsigned int> indices { 0, 1, 2, 0, 2, 3 };
    std::vector<Texture> textures {
        Texture { textureId, "texture_diffuse", "Grass006_1K-JPG_Color.jpg" }
    };

    return Mesh(std::move(vertices), std::move(indices), std::move(textures));
}

Mesh BaseScene::CreateTexturedBoxMesh(
    GLuint textureId,
    const std::string& textureName,
    const glm::vec3& size,
    float tileWorldSize)
{
    const glm::vec3 halfExtents = size * 0.5f;
    const float tileSize = std::max(tileWorldSize, 0.01f);
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
    vertices.reserve(24);
    indices.reserve(36);

    const auto repeatsFor = [tileSize](float value)
    {
        return std::max(value / tileSize, 1.0f);
    };

    auto addFace = [&](const std::array<glm::vec3, 4>& positions, const glm::vec3& normal, float uRepeat, float vRepeat)
    {
        const unsigned int base = static_cast<unsigned int>(vertices.size());
        vertices.push_back(Vertex { positions[0], normal, glm::vec2(uRepeat, vRepeat) });
        vertices.push_back(Vertex { positions[1], normal, glm::vec2(0.0f, vRepeat) });
        vertices.push_back(Vertex { positions[2], normal, glm::vec2(0.0f, 0.0f) });
        vertices.push_back(Vertex { positions[3], normal, glm::vec2(uRepeat, 0.0f) });
        indices.insert(indices.end(), { base, base + 1u, base + 2u, base, base + 2u, base + 3u });
    };

    const float x = halfExtents.x;
    const float y = halfExtents.y;
    const float z = halfExtents.z;

    addFace(
        { glm::vec3(-x, -y, z), glm::vec3(x, -y, z), glm::vec3(x, y, z), glm::vec3(-x, y, z) },
        glm::vec3(0.0f, 0.0f, 1.0f),
        repeatsFor(size.x),
        repeatsFor(size.y));
    addFace(
        { glm::vec3(x, -y, -z), glm::vec3(-x, -y, -z), glm::vec3(-x, y, -z), glm::vec3(x, y, -z) },
        glm::vec3(0.0f, 0.0f, -1.0f),
        repeatsFor(size.x),
        repeatsFor(size.y));
    addFace(
        { glm::vec3(x, -y, z), glm::vec3(x, -y, -z), glm::vec3(x, y, -z), glm::vec3(x, y, z) },
        glm::vec3(1.0f, 0.0f, 0.0f),
        repeatsFor(size.z),
        repeatsFor(size.y));
    addFace(
        { glm::vec3(-x, -y, -z), glm::vec3(-x, -y, z), glm::vec3(-x, y, z), glm::vec3(-x, y, -z) },
        glm::vec3(-1.0f, 0.0f, 0.0f),
        repeatsFor(size.z),
        repeatsFor(size.y));
    addFace(
        { glm::vec3(-x, y, z), glm::vec3(x, y, z), glm::vec3(x, y, -z), glm::vec3(-x, y, -z) },
        glm::vec3(0.0f, 1.0f, 0.0f),
        repeatsFor(size.x),
        repeatsFor(size.z));
    addFace(
        { glm::vec3(-x, -y, -z), glm::vec3(x, -y, -z), glm::vec3(x, -y, z), glm::vec3(-x, -y, z) },
        glm::vec3(0.0f, -1.0f, 0.0f),
        repeatsFor(size.x),
        repeatsFor(size.z));

    std::vector<Texture> textures {
        Texture { textureId, "texture_diffuse", textureName }
    };
    return Mesh(std::move(vertices), std::move(indices), std::move(textures));
}

Mesh BaseScene::CreateCubeMesh(GLuint textureId, const std::string& textureName)
{
    std::vector<Vertex> vertices = MakeVertices({
        -0.5f, -0.5f, -0.5f, 0.0f,  0.0f, -1.0f, 0.0f, 0.0f,
         0.5f, -0.5f, -0.5f, 0.0f,  0.0f, -1.0f, 1.0f, 0.0f,
         0.5f,  0.5f, -0.5f, 0.0f,  0.0f, -1.0f, 1.0f, 1.0f,
         0.5f,  0.5f, -0.5f, 0.0f,  0.0f, -1.0f, 1.0f, 1.0f,
        -0.5f,  0.5f, -0.5f, 0.0f,  0.0f, -1.0f, 0.0f, 1.0f,
        -0.5f, -0.5f, -0.5f, 0.0f,  0.0f, -1.0f, 0.0f, 0.0f,

        -0.5f, -0.5f,  0.5f, 0.0f,  0.0f, 1.0f, 0.0f, 0.0f,
         0.5f, -0.5f,  0.5f, 0.0f,  0.0f, 1.0f, 1.0f, 0.0f,
         0.5f,  0.5f,  0.5f, 0.0f,  0.0f, 1.0f, 1.0f, 1.0f,
         0.5f,  0.5f,  0.5f, 0.0f,  0.0f, 1.0f, 1.0f, 1.0f,
        -0.5f,  0.5f,  0.5f, 0.0f,  0.0f, 1.0f, 0.0f, 1.0f,
        -0.5f, -0.5f,  0.5f, 0.0f,  0.0f, 1.0f, 0.0f, 0.0f,

        -0.5f,  0.5f,  0.5f, -1.0f, 0.0f,  0.0f, 1.0f, 0.0f,
        -0.5f,  0.5f, -0.5f, -1.0f, 0.0f,  0.0f, 1.0f, 1.0f,
        -0.5f, -0.5f, -0.5f, -1.0f, 0.0f,  0.0f, 0.0f, 1.0f,
        -0.5f, -0.5f, -0.5f, -1.0f, 0.0f,  0.0f, 0.0f, 1.0f,
        -0.5f, -0.5f,  0.5f, -1.0f, 0.0f,  0.0f, 0.0f, 0.0f,
        -0.5f,  0.5f,  0.5f, -1.0f, 0.0f,  0.0f, 1.0f, 0.0f,

         0.5f,  0.5f,  0.5f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f,
         0.5f,  0.5f, -0.5f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f,
         0.5f, -0.5f, -0.5f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f,
         0.5f, -0.5f, -0.5f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f,
         0.5f, -0.5f,  0.5f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
         0.5f,  0.5f,  0.5f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f,

        -0.5f, -0.5f, -0.5f, 0.0f, -1.0f, 0.0f, 0.0f, 1.0f,
         0.5f, -0.5f, -0.5f, 0.0f, -1.0f, 0.0f, 1.0f, 1.0f,
         0.5f, -0.5f,  0.5f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f,
         0.5f, -0.5f,  0.5f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f,
        -0.5f, -0.5f,  0.5f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f,
        -0.5f, -0.5f, -0.5f, 0.0f, -1.0f, 0.0f, 0.0f, 1.0f,

        -0.5f,  0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
         0.5f,  0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f,
         0.5f,  0.5f,  0.5f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f,
         0.5f,  0.5f,  0.5f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f,
        -0.5f,  0.5f,  0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
        -0.5f,  0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f
    });

    std::vector<unsigned int> indices(vertices.size());
    for (unsigned int index = 0; index < indices.size(); ++index)
    {
        indices[index] = index;
    }

    std::vector<Texture> textures;
    if (textureId != 0)
    {
        textures.push_back(Texture { textureId, "texture_diffuse", textureName });
    }

    return Mesh(std::move(vertices), std::move(indices), std::move(textures));
}
