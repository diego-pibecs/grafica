#include "BaseScene.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>

#include "CameraController.h"
#include "DebugLog.h"
#include "TextureUtils.h"
#include "render/debug/PhysicsDebugRenderer.h"

namespace
{
namespace fs = std::filesystem;

constexpr int kMaxPointLights = 32;
constexpr int kPointShadowTextureUnitOffset = 2;
constexpr int kMaxPointShadowSamplers = 12;
constexpr int kDirtTextureUnit = 15;

double MillisecondsSince(const std::chrono::steady_clock::time_point& begin)
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
}

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

bool IsStrictHouseLightNodeName(const std::string& lowerName)
{
    return lowerName.size() == 8u
        && lowerName.rfind("light", 0) == 0
        && std::isdigit(static_cast<unsigned char>(lowerName[5])) != 0
        && std::isdigit(static_cast<unsigned char>(lowerName[6])) != 0
        && std::isdigit(static_cast<unsigned char>(lowerName[7])) != 0;
}

bool IsInteractableHouseLightNodeName(const std::string& lowerName)
{
    return lowerName.find("light_interactable") != std::string::npos
        || lowerName.find("interactable_light") != std::string::npos;
}

bool IsProximityHouseLightNodeName(const std::string& lowerName)
{
    return lowerName.find("light_proximity") != std::string::npos
        || lowerName.find("proximity_light") != std::string::npos
        || lowerName.find("prox_light") != std::string::npos
        || lowerName.find("light_prox") != std::string::npos;
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

bool RayIntersectsSphere(
    const glm::vec3& rayOrigin,
    const glm::vec3& rayDirection,
    const glm::vec3& center,
    float radius,
    float maxDistance,
    float& hitDistance)
{
    if (radius <= 0.0f || glm::dot(rayDirection, rayDirection) < 0.000001f)
    {
        return false;
    }

    const glm::vec3 direction = glm::normalize(rayDirection);
    const glm::vec3 toCenter = center - rayOrigin;
    const float projected = glm::dot(toCenter, direction);
    if (projected < 0.0f || projected > maxDistance)
    {
        return false;
    }

    const glm::vec3 closest = rayOrigin + (direction * projected);
    const float distanceSq = glm::dot(center - closest, center - closest);
    if (distanceSq > radius * radius)
    {
        return false;
    }

    hitDistance = projected;
    return true;
}

bool IsRayTargetingDropZone(
    const glm::vec3& rayOrigin,
    const glm::vec3& rayDirection,
    const glm::vec3& dropZonePosition,
    float maxDistance)
{
    float hitDistance = 0.0f;
    const glm::vec3 target = dropZonePosition + glm::vec3(0.0f, 0.85f, 0.0f);
    return RayIntersectsSphere(rayOrigin, rayDirection, target, 1.35f, maxDistance, hitDistance);
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
    (void)wallThickness;
    const float centerZ = (sceneMin.z + sceneMax.z) * 0.5f;
    const float centerX = (sceneMin.x + sceneMax.x) * 0.5f;
    const float wallY = wallHeight * 0.5f;

    return {
        glm::vec3(sceneMin.x, wallY, centerZ),
        glm::vec3(sceneMax.x, wallY, centerZ),
        glm::vec3(centerX, wallY, sceneMin.z),
        glm::vec3(centerX, wallY, sceneMax.z)
    };
}
}

BaseScene::BaseScene(std::filesystem::path assetsRoot)
    : assetsRoot_(std::move(assetsRoot))
{
}

BaseScene::~BaseScene()
{
    if (shadowMapTexture_ != 0)
    {
        glDeleteTextures(1, &shadowMapTexture_);
        shadowMapTexture_ = 0;
    }

    if (shadowMapFramebuffer_ != 0)
    {
        glDeleteFramebuffers(1, &shadowMapFramebuffer_);
        shadowMapFramebuffer_ = 0;
    }

    ReleasePointLightShadowMaps();
    if (dirtTexture_ != 0)
    {
        glDeleteTextures(1, &dirtTexture_);
        dirtTexture_ = 0;
    }
    if (pointShadowFramebuffer_ != 0)
    {
        glDeleteFramebuffers(1, &pointShadowFramebuffer_);
        pointShadowFramebuffer_ = 0;
    }
}

void BaseScene::Init()
{
    DebugLog::ScopedTrace trace("BaseScene", "Init");
    litShader_ = std::make_unique<ShaderProgram>(
        assetsRoot_ / "shaders" / "lit.vs",
        assetsRoot_ / "shaders" / "lit.frag");
    shadowDepthShader_ = std::make_unique<ShaderProgram>(
        assetsRoot_ / "shaders" / "shadow_depth.vs",
        assetsRoot_ / "shaders" / "shadow_depth.frag");
    pointShadowDepthShader_ = std::make_unique<ShaderProgram>(
        assetsRoot_ / "shaders" / "point_shadow_depth.vs",
        assetsRoot_ / "shaders" / "point_shadow_depth.frag");
    lightMarkerShader_ = std::make_unique<ShaderProgram>(
        assetsRoot_ / "shaders" / "light_marker.vs",
        assetsRoot_ / "shaders" / "light_marker.frag");
    skyboxShader_ = std::make_unique<ShaderProgram>(
        assetsRoot_ / "shaders" / "skybox.vs",
        assetsRoot_ / "shaders" / "skybox.frag");
    physicsDebugRenderer_ = std::make_unique<PhysicsDebugRenderer>(assetsRoot_);

    const GLuint grassTexture = LoadTexture2D(assetsRoot_ / "textures" / "grass" / "Grass006_1K-JPG_Color.jpg");
    dirtTexture_ = LoadTexture2D(assetsRoot_ / "textures" / "dirt" / "dirt.png");
    const GLuint fenceTexture = LoadTexture2D(assetsRoot_ / "textures" / "fence-texture.png");
    const float wallLengthZ = (sceneBoundsMax_.z - sceneBoundsMin_.z) + (boundaryWallThickness_ * 2.0f);
    const float wallLengthX = (sceneBoundsMax_.x - sceneBoundsMin_.x) + (boundaryWallThickness_ * 2.0f);
    floorMesh_ = std::make_unique<Mesh>(CreateFloorMesh(grassTexture, floorHalfExtents_, floorUvTiling_));
    boundarySideWallMesh_ = std::make_unique<Mesh>(CreateTexturedWallPlaneMesh(
        fenceTexture,
        "fence-texture.png",
        glm::vec2(wallLengthZ, boundaryWallHeight_),
        6.0f,
        false));
    boundaryEndWallMesh_ = std::make_unique<Mesh>(CreateTexturedWallPlaneMesh(
        fenceTexture,
        "fence-texture.png",
        glm::vec2(wallLengthX, boundaryWallHeight_),
        6.0f,
        true));
    lightMarkerMesh_ = std::make_unique<Mesh>(CreateSphereMesh(10, 14));
    skyboxMesh_ = std::make_unique<Mesh>(CreateCubeMesh());

    skyCloudTextureA_ = LoadTexture2D(assetsRoot_ / "textures" / "skybox" / "434P-111_town_sky_cloud_A.png");
    skyCloudTextureB_ = LoadTexture2D(assetsRoot_ / "textures" / "skybox" / "434P-111_town_sky_cloud_D.png");

    glGenFramebuffers(1, &shadowMapFramebuffer_);
    glGenTextures(1, &shadowMapTexture_);
    glBindTexture(GL_TEXTURE_2D, shadowMapTexture_);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_DEPTH_COMPONENT24,
        shadowMapResolution_,
        shadowMapResolution_,
        0,
        GL_DEPTH_COMPONENT,
        GL_FLOAT,
        nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    const float borderColor[] { 1.0f, 1.0f, 1.0f, 1.0f };
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);

    glBindFramebuffer(GL_FRAMEBUFFER, shadowMapFramebuffer_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowMapTexture_, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        throw std::runtime_error("No se pudo inicializar el framebuffer de sombras.");
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glGenFramebuffers(1, &pointShadowFramebuffer_);
    DebugLog::Info("BaseScene", "Core shaders and textures loaded");

    LoadEntities();
    ConfigureSceneLights();
    DebugLog::Info("BaseScene", "Init complete with ", entities_.size(), " visual entit(ies)");
}

void BaseScene::Update(const PlayerSnapshot& player, float absoluteTimeSeconds, float deltaTimeSeconds)
{
    absoluteTimeSeconds_ = absoluteTimeSeconds;

    const float dt = std::clamp(deltaTimeSeconds, 0.0f, 0.1f);
    UpdateStoryState(player, dt);
    UpdateTvFall();
    UpdateActivePointLights(player.position);
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

void BaseScene::UpdateHoldAction(
    bool cancelRequested,
    const glm::vec3& rayOrigin,
    const glm::vec3& rayDirection,
    const glm::vec3& playerPosition,
    float deltaTimeSeconds)
{
    if (activeHoldAction_ == HoldActionType::None)
    {
        return;
    }

    const float playerDistance = glm::length(playerPosition - activeHoldTargetPosition_);
    float hitDistance = 0.0f;
    const bool stillTargeting = RayIntersectsSphere(
        rayOrigin,
        rayDirection,
        activeHoldTargetPosition_,
        0.55f,
        6.0f,
        hitDistance);
    if (cancelRequested || playerDistance > 2.0f || !stillTargeting)
    {
        CancelHoldAction();
        return;
    }

    const float dt = std::clamp(deltaTimeSeconds, 0.0f, 0.1f);
    holdActionProgress_ = std::min(holdActionDuration_, holdActionProgress_ + dt);
    if (holdActionProgress_ < holdActionDuration_)
    {
        return;
    }

    if (activeHoldAction_ == HoldActionType::HandWash)
    {
        for (FaucetInteraction& faucet : faucetInteractions_)
        {
            if (faucet.id == activeHoldTargetId_)
            {
                faucet.used = true;
                break;
            }
        }
        activeHoldAction_ = HoldActionType::None;
        activeHoldTargetId_.clear();
        holdActionProgress_ = 0.0f;
        ReduceAnxiety(22.0f);
        SetStoryPhase(StoryPhase::HandWashedRelief);
        ShowCenterMessage({ "TE LIMPIASTE UN POCO", "PERO LA SENSACION SIGUE AHI" }, 5.0f);
        return;
    }

    if (activeHoldAction_ == HoldActionType::Shower)
    {
        for (FaucetInteraction& faucet : faucetInteractions_)
        {
            if (faucet.id == activeHoldTargetId_)
            {
                faucet.used = true;
                break;
            }
        }
        activeHoldAction_ = HoldActionType::None;
        activeHoldTargetId_.clear();
        holdActionProgress_ = 0.0f;
        ReduceAnxiety(38.0f);
        SetStoryPhase(StoryPhase::ShowerRelief);
        ShowCenterMessage({ "TE LIMPIASTE MAS A FONDO", "LA SENSACION BAJA POR UN MOMENTO" }, 5.0f);
    }
}

void BaseScene::Render(const CameraController& camera, const glm::mat4& projection) const
{
    static std::uint64_t renderCallCount = 0;
    ++renderCallCount;
    const bool traceRenderCall = renderCallCount <= 12u || (renderCallCount % 600u) == 0u;
    const auto renderBegin = std::chrono::steady_clock::now();
    if (traceRenderCall)
    {
        DebugLog::Info("BaseScene", "Render call ", renderCallCount, " begin");
    }

    const glm::mat4 view = camera.GetViewMatrix();
    const glm::mat4 lightSpaceMatrix = BuildSunLightSpaceMatrix();

    double shadowMs = 0.0;
    bool shadowUpdated = false;
    if (shadowMapDirty_ || pointShadowMapsDirty_)
    {
        const auto shadowBegin = std::chrono::steady_clock::now();
        if (shadowMapDirty_)
        {
            RenderShadowMap(lightSpaceMatrix);
            shadowMapDirty_ = false;
        }
        if (pointShadowMapsDirty_)
        {
            RenderPointShadowMaps();
            pointShadowMapsDirty_ = false;
        }
        shadowMs = MillisecondsSince(shadowBegin);
        shadowUpdated = true;
    }

    const auto skyboxBegin = std::chrono::steady_clock::now();
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
    const double skyboxMs = MillisecondsSince(skyboxBegin);

    const auto litBegin = std::chrono::steady_clock::now();
    litShader_->Use();
    litShader_->SetMat4("projection", projection);
    litShader_->SetMat4("view", view);
    litShader_->SetMat4("lightSpaceMatrix", lightSpaceMatrix);
    litShader_->SetVec3("viewPos", camera.GetPosition());
    litShader_->SetVec3("sunDirection", glm::normalize(sunDirection_));
    litShader_->SetVec3("sunColor", sunColor_);
    litShader_->SetVec3("skyAmbientColor", ambientSkyColor_);
    litShader_->SetVec3("groundAmbientColor", ambientGroundColor_);
    litShader_->SetFloat("shininess", 24.0f);
    litShader_->SetInt("texture_diffuse1", 0);
    litShader_->SetInt("shadowMap", 1);
    litShader_->SetInt("dirtTexture", kDirtTextureUnit);
    litShader_->SetBool("shadowsEnabled", shadowMapTexture_ != 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, shadowMapTexture_);

    const glm::vec3 cameraPosition = camera.GetPosition();
    const std::size_t activeLightCount = std::min(pointLights_.size(), static_cast<std::size_t>(kMaxPointLights));
    if (pointLights_.size() > static_cast<std::size_t>(kMaxPointLights) && traceRenderCall)
    {
        DebugLog::Info("BaseScene", "Point light count exceeds shader cap: ", pointLights_.size(), " > ", kMaxPointLights);
    }

    const int pointLightCount = static_cast<int>(activeLightCount);
    litShader_->SetInt("pointLightCount", pointLightCount);
    bool hasActivePointShadow = false;
    const std::size_t shadowSamplerLightCount = std::min(activeLightCount, static_cast<std::size_t>(kMaxPointShadowSamplers));
    for (std::size_t index = 0; index < shadowSamplerLightCount; ++index)
    {
        const PointLight& pointLight = pointLights_[index];
        if (pointLight.castsShadow && pointLight.shadowCubeMap != 0)
        {
            hasActivePointShadow = true;
            break;
        }
    }
    litShader_->SetBool("pointShadowsEnabled", pointShadowFramebuffer_ != 0 && hasActivePointShadow);
    for (int index = 0; index < kMaxPointLights; ++index)
    {
        const std::string prefix = "pointLights[" + std::to_string(index) + "]";
        const bool hasShadowSampler = index < kMaxPointShadowSamplers;
        if (hasShadowSampler)
        {
            const std::string shadowPrefix = "pointShadowMaps[" + std::to_string(index) + "]";
            litShader_->SetInt(shadowPrefix, kPointShadowTextureUnitOffset + index);
        }
        if (index < pointLightCount)
        {
            const PointLight& pointLight = pointLights_[static_cast<std::size_t>(index)];
            litShader_->SetVec3(prefix + ".position", pointLight.position);
            litShader_->SetVec3(prefix + ".color", pointLight.color);
            litShader_->SetFloat(prefix + ".intensity", pointLight.intensity);
            litShader_->SetFloat(prefix + ".range", pointLight.range);
            litShader_->SetFloat(prefix + ".shadowStrength", hasShadowSampler && pointLight.castsShadow ? 0.50f : 0.0f);
            if (hasShadowSampler)
            {
                glActiveTexture(GL_TEXTURE0 + kPointShadowTextureUnitOffset + index);
                glBindTexture(GL_TEXTURE_CUBE_MAP, pointLight.shadowCubeMap);
            }
        }
        else
        {
            litShader_->SetVec3(prefix + ".position", glm::vec3(0.0f));
            litShader_->SetVec3(prefix + ".color", glm::vec3(0.0f));
            litShader_->SetFloat(prefix + ".intensity", 0.0f);
            litShader_->SetFloat(prefix + ".range", 1.0f);
            litShader_->SetFloat(prefix + ".shadowStrength", 0.0f);
            if (hasShadowSampler)
            {
                glActiveTexture(GL_TEXTURE0 + kPointShadowTextureUnitOffset + index);
                glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
            }
        }
    }

    DrawLitGeometry();
    for (int index = 0; index < kMaxPointShadowSamplers; ++index)
    {
        glActiveTexture(GL_TEXTURE0 + kPointShadowTextureUnitOffset + index);
        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    }
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    const double litMs = MillisecondsSince(litBegin);

    const auto markerBegin = std::chrono::steady_clock::now();
    if (physicsDebugEnabled_)
    {
        lightMarkerShader_->Use();
        lightMarkerShader_->SetMat4("projection", projection);
        lightMarkerShader_->SetMat4("view", view);
        lightMarkerShader_->SetVec3("color", glm::vec3(1.0f, 0.90f, 0.62f));
        for (const PointLight& pointLight : pointLights_)
        {
            glm::mat4 lightMarkerModel = glm::translate(glm::mat4(1.0f), pointLight.position);
            lightMarkerModel = glm::scale(lightMarkerModel, glm::vec3(0.12f));
            lightMarkerShader_->SetMat4("model", lightMarkerModel);
            lightMarkerMesh_->Draw();
        }
        if (traceRenderCall)
        {
            DebugLog::Info("BaseScene", "Render call ", renderCallCount, " light markers done");
        }
    }
    const double markerMs = MillisecondsSince(markerBegin);

    const auto debugBegin = std::chrono::steady_clock::now();
    if (physicsDebugEnabled_ && physicsDebugRenderer_ != nullptr)
    {
        physicsDebugRenderer_->Render(physicsDebugFrame_, view, projection);
        if (traceRenderCall)
        {
            DebugLog::Info("BaseScene", "Render call ", renderCallCount, " physics debug done");
        }
    }
    const double debugMs = MillisecondsSince(debugBegin);

    const double totalRenderMs = MillisecondsSince(renderBegin);
    renderPerfStats_.frameCount += 1u;
    renderPerfStats_.totalMs += totalRenderMs;
    renderPerfStats_.shadowMs += shadowMs;
    renderPerfStats_.skyboxMs += skyboxMs;
    renderPerfStats_.litMs += litMs;
    renderPerfStats_.markersMs += markerMs;
    renderPerfStats_.debugMs += debugMs;
    renderPerfStats_.maxFrameMs = std::max(renderPerfStats_.maxFrameMs, totalRenderMs);

    if ((renderCallCount % 120u) == 0u || totalRenderMs > 18.0)
    {
        std::string activeLightLabels;
        for (int index = 0; index < pointLightCount; ++index)
        {
            if (!activeLightLabels.empty())
            {
                activeLightLabels += ", ";
            }
            activeLightLabels += pointLights_[static_cast<std::size_t>(index)].label;
        }

        const double frameCount = std::max<double>(1.0, static_cast<double>(renderPerfStats_.frameCount));
        DebugLog::Info(
            "Perf",
            "Render avgMs=", renderPerfStats_.totalMs / frameCount,
            " maxMs=", renderPerfStats_.maxFrameMs,
            " shadowAvgMs=", renderPerfStats_.shadowMs / frameCount,
            " skyboxAvgMs=", renderPerfStats_.skyboxMs / frameCount,
            " litAvgMs=", renderPerfStats_.litMs / frameCount,
            " markerAvgMs=", renderPerfStats_.markersMs / frameCount,
            " debugAvgMs=", renderPerfStats_.debugMs / frameCount,
            " currentMs=", totalRenderMs,
            " shadowUpdated=", shadowUpdated,
            " pointLights=", pointLightCount,
            " active=[", activeLightLabels, "]",
            " camera=(", cameraPosition.x, ", ", cameraPosition.y, ", ", cameraPosition.z, ")");

        renderPerfStats_ = RenderPerfStats {};
    }

    if (traceRenderCall)
    {
        DebugLog::Info("BaseScene", "Render call ", renderCallCount, " end");
    }
}

void BaseScene::SetPhysicsDebugFrame(PhysicsDebugFrame frame)
{
    for (const InteractiveDoor& door : doors_)
    {
        if (door.placement.model == nullptr)
        {
            continue;
        }

        const bool blockerActive = door.openProgress < 0.82f;
        const WalkableBlocker blocker = BuildDoorBlocker(door);
        const glm::vec3 color = blockerActive ? glm::vec3(1.0f, 0.12f, 0.08f) : glm::vec3(0.20f, 1.0f, 0.25f);
        frame.points.push_back(PhysicsDebugPoint { blocker.center, color, blockerActive ? 8.0f : 6.0f });

        const float yaw = glm::radians(blocker.yawDegrees);
        const glm::vec2 xAxis(std::cos(yaw), std::sin(yaw));
        const glm::vec2 zAxis(-std::sin(yaw), std::cos(yaw));
        const glm::vec2 center(blocker.center.x, blocker.center.z);
        const glm::vec2 x = xAxis * blocker.halfExtents.x;
        const glm::vec2 z = zAxis * blocker.halfExtents.z;
        const std::array<glm::vec2, 4> corners {
            center - x - z,
            center + x - z,
            center + x + z,
            center - x + z
        };
        const float y = std::max(0.08f, blocker.center.y - blocker.halfExtents.y + 0.08f);
        for (std::size_t index = 0; index < corners.size(); ++index)
        {
            const glm::vec2& a = corners[index];
            const glm::vec2& b = corners[(index + 1u) % corners.size()];
            frame.lines.push_back(PhysicsDebugLine {
                glm::vec3(a.x, y, a.y),
                glm::vec3(b.x, y, b.y),
                color,
                color
            });
        }
    }

    for (const CarryableObject& object : carryableObjects_)
    {
        if (object.placement.model == nullptr
            || !object.visible
            || object.pickedUp
            || object.discarded
            || !object.blocksNavigation)
        {
            continue;
        }

        const WalkableBlocker blocker = BuildCarryableBlocker(object);
        const glm::vec3 color(1.0f, 0.64f, 0.10f);
        frame.points.push_back(PhysicsDebugPoint { blocker.center, color, 5.5f });

        const float yaw = glm::radians(blocker.yawDegrees);
        const glm::vec2 xAxis(std::cos(yaw), std::sin(yaw));
        const glm::vec2 zAxis(-std::sin(yaw), std::cos(yaw));
        const glm::vec2 center(blocker.center.x, blocker.center.z);
        const glm::vec2 x = xAxis * blocker.halfExtents.x;
        const glm::vec2 z = zAxis * blocker.halfExtents.z;
        const std::array<glm::vec2, 4> corners {
            center - x - z,
            center + x - z,
            center + x + z,
            center - x + z
        };
        const float y = std::max(0.08f, blocker.center.y - blocker.halfExtents.y + 0.08f);
        for (std::size_t index = 0; index < corners.size(); ++index)
        {
            const glm::vec2& a = corners[index];
            const glm::vec2& b = corners[(index + 1u) % corners.size()];
            frame.lines.push_back(PhysicsDebugLine {
                glm::vec3(a.x, y, a.y),
                glm::vec3(b.x, y, b.y),
                color,
                color
            });
        }
    }

    physicsDebugFrame_ = std::move(frame);
}

void BaseScene::SetPhysicsDebugEnabled(bool enabled)
{
    physicsDebugEnabled_ = enabled;
}

bool BaseScene::TryInteract(const glm::vec3& rayOrigin, const glm::vec3& rayDirection, const glm::vec3& playerPosition)
{
    if (activeHoldAction_ != HoldActionType::None)
    {
        return false;
    }

    if (TryUseFaucet(rayOrigin, rayDirection, playerPosition))
    {
        return true;
    }

    if (TryDiscardCarriedObject(rayOrigin, rayDirection, playerPosition))
    {
        return true;
    }

    if (TryToggleHouseLight(rayOrigin, rayDirection, playerPosition))
    {
        return true;
    }

    if (carriedObjectIndex_ < 0 && CarryablesEnabled())
    {
        const int carryableIndex = FindTargetedCarryable(rayOrigin, rayDirection, playerPosition);
        if (carryableIndex >= 0 && TryPickupCarryable(carryableIndex))
        {
            return true;
        }
    }

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
    const WalkableBlocker doorBlocker = BuildDoorBlocker(*targetedDoor);
    const bool blockerActive = targetedDoor->openProgress < 0.82f;
    DebugLog::Info(
        "DOOR FIX",
        "name=", targetedDoor->name,
        " open=", targetedDoor->open,
        " openProgress=", targetedDoor->openProgress,
        " blockerActive=", blockerActive,
        " passable=", !blockerActive,
        " blocker center=(",
        doorBlocker.center.x, ", ", doorBlocker.center.y, ", ", doorBlocker.center.z,
        ") blocker halfExtents=(",
        doorBlocker.halfExtents.x, ", ", doorBlocker.halfExtents.y, ", ", doorBlocker.halfExtents.z,
        ")");
    DebugLog::Info(
        "BaseScene",
        "Door interaction ", targetedDoor->name,
        " open=", targetedDoor->open,
        " rayHit=", nearestHitDistance);
    return true;
}

std::vector<std::string> BaseScene::BuildHudLines() const
{
    std::vector<std::string> lines;
    if (anxietySystemActive_)
    {
        const float displayedAnxiety = std::clamp(anxietyLevel_ + anxietyPulse_, 0.0f, 100.0f);
        lines.push_back(BuildAnxietyBar(displayedAnxiety));
    }
    else
    {
        lines.push_back("ANSIEDAD [----------------] 0%");
    }

    if (CarryablesEnabled())
    {
        lines.push_back("OBJETOS EN BASURERO: " + std::to_string(discardedCount_) + " / " + std::to_string(requiredDiscardCount_));
        if (carriedObjectIndex_ >= 0 && carriedObjectIndex_ < static_cast<int>(carryableObjects_.size()))
        {
            lines.push_back("LLEVANDO: " + carryableObjects_[static_cast<std::size_t>(carriedObjectIndex_)].displayName);
        }
    }

    if (darkReflectionUnlocked_)
    {
        lines.push_back("ZONA FINAL: DESBLOQUEADA");
    }
    return lines;
}

std::vector<std::string> BaseScene::BuildObjectiveLines() const
{
    if (carriedObjectIndex_ >= 0)
    {
        return { "VE AL BASURERO" };
    }

    if (currentPhase_ == StoryPhase::ExteriorStart)
    {
        return { "AVANZA HACIA LA CASA" };
    }
    if (currentPhase_ == StoryPhase::TriggerWalk || currentPhase_ == StoryPhase::AnxietyActivated)
    {
        return { "SIGUE EL CAMINO", "NO PUEDES QUITAR LA SENSACION" };
    }
    if (currentPhase_ == StoryPhase::NeedHandWash)
    {
        return { "LAVATE LAS MANOS" };
    }
    if (currentPhase_ == StoryPhase::HandWashing)
    {
        return { "NO TE MUEVAS" };
    }
    if (currentPhase_ == StoryPhase::HandWashedRelief)
    {
        return { "LA SENSACION SIGUE AHI" };
    }
    if (currentPhase_ == StoryPhase::NeedShower)
    {
        return { "VE A LA REGADERA" };
    }
    if (currentPhase_ == StoryPhase::Showering)
    {
        return { "NO TE MUEVAS" };
    }
    if (currentPhase_ == StoryPhase::ShowerRelief)
    {
        return { "RESPIRA UN MOMENTO" };
    }
    if (currentPhase_ == StoryPhase::NeedDiscardObjects || currentPhase_ == StoryPhase::DiscardLoop)
    {
        return { "BUSCA OBJETOS PARA SACAR" };
    }
    if (currentPhase_ == StoryPhase::DarkReflection)
    {
        return { "EL SILENCIO SE VUELVE VISIBLE" };
    }
    return {};
}

std::vector<std::string> BaseScene::BuildContextMessageLines() const
{
    return BuildObjectiveLines();
}

std::vector<std::string> BaseScene::BuildRaycastPromptLines(
    const glm::vec3& rayOrigin,
    const glm::vec3& rayDirection,
    const glm::vec3& playerPosition) const
{
    if (activeHoldAction_ != HoldActionType::None)
    {
        return { BuildHoldActionProgressLine() };
    }

    const int faucetIndex = FindTargetedFaucet(rayOrigin, rayDirection, playerPosition);
    if (faucetIndex >= 0)
    {
        const FaucetInteraction& faucet = faucetInteractions_[static_cast<std::size_t>(faucetIndex)];
        if (faucet.type == HoldActionType::HandWash && currentPhase_ == StoryPhase::NeedHandWash)
        {
            return { "PRESIONA E PARA LAVARTE LAS MANOS" };
        }
        if (faucet.type == HoldActionType::Shower && currentPhase_ == StoryPhase::NeedShower)
        {
            return { "PRESIONA E PARA BANARTE" };
        }
    }

    if (carriedObjectIndex_ >= 0
        && IsPlayerNearDropZone(playerPosition)
        && IsRayTargetingDropZone(rayOrigin, rayDirection, dumpsterDropZone_.position, 7.0f))
    {
        return { "PRESIONA E PARA TIRAR EN BASURERO" };
    }

    if (CarryablesEnabled()
        && carriedObjectIndex_ < 0
        && FindTargetedCarryable(rayOrigin, rayDirection, playerPosition) >= 0)
    {
        return { "PRESIONA E PARA RECOGER" };
    }

    if (FindTargetedHouseLight(rayOrigin, rayDirection, playerPosition) >= 0)
    {
        return { "PRESIONA E PARA ENCENDER LUZ" };
    }

    constexpr float kMaxDoorRayDistance = 4.2f;
    for (const InteractiveDoor& door : doors_)
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
        if (RayIntersectsDoorBlocker(rayOrigin, rayDirection, blocker, kMaxDoorRayDistance, hitDistance))
        {
            return { door.open ? "PRESIONA E PARA CERRAR" : "PRESIONA E PARA ABRIR" };
        }
    }

    return {};
}

std::vector<std::string> BaseScene::BuildCenterMessageLines() const
{
    if (centerMessage_.remainingSeconds > 0.0f)
    {
        return centerMessage_.lines;
    }
    return {};
}

float BaseScene::GetAnxietyTintAlpha() const noexcept
{
    if (!anxietySystemActive_)
    {
        return 0.0f;
    }

    const float base = std::clamp(anxietyLevel_ / 100.0f, 0.0f, 1.0f);
    const float pulse = 0.5f + (0.5f * std::sin(absoluteTimeSeconds_ * 4.1f));
    const float calmFactor = darkReflectionUnlocked_ ? 0.35f : 1.0f;
    return std::clamp(((base * 0.20f) + (pulse * base * 0.16f)) * calmFactor, 0.0f, 0.42f);
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
    blockers.reserve(doors_.size() + carryableObjects_.size());
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
    for (const CarryableObject& object : carryableObjects_)
    {
        if (object.placement.model == nullptr
            || !object.visible
            || object.pickedUp
            || object.discarded
            || !object.blocksNavigation)
        {
            continue;
        }

        WalkableBlocker blocker = BuildCarryableBlocker(object);
        blocker.enabled = true;
        blockers.push_back(std::move(blocker));
    }
    return blockers;
}

bool BaseScene::IsPhaseAtLeast(StoryPhase phase) const noexcept
{
    return static_cast<int>(currentPhase_) >= static_cast<int>(phase);
}

bool BaseScene::CarryablesEnabled() const noexcept
{
    return currentPhase_ == StoryPhase::NeedDiscardObjects
        || currentPhase_ == StoryPhase::DiscardLoop
        || currentPhase_ == StoryPhase::EmptyHouse
        || currentPhase_ == StoryPhase::DarkReflection
        || currentPhase_ == StoryPhase::Finished;
}

void BaseScene::AddNarrativeTrigger(
    const std::string& id,
    const glm::vec3& position,
    float radius,
    StoryPhase requiredPhase,
    StoryPhase nextPhase,
    std::vector<std::string> messages,
    float anxietyDelta)
{
    NarrativeTrigger trigger;
    trigger.id = id;
    trigger.position = position;
    trigger.radius = radius;
    trigger.requiredPhase = requiredPhase;
    trigger.nextPhase = nextPhase;
    trigger.messages = std::move(messages);
    trigger.anxietyDelta = anxietyDelta;
    narrativeTriggers_.push_back(std::move(trigger));
}

void BaseScene::ShowCenterMessage(std::vector<std::string> lines, float durationSeconds)
{
    centerMessage_.lines = std::move(lines);
    centerMessage_.remainingSeconds = std::max(durationSeconds, 0.0f);
}

void BaseScene::ActivateAnxiety(float delta)
{
    anxietySystemActive_ = true;
    anxietyLevel_ = std::clamp(anxietyLevel_ + delta, 0.0f, 100.0f);
    contaminationLevel_ = std::clamp(contaminationLevel_ + (delta * 0.35f), 0.0f, 100.0f);
}

void BaseScene::ReduceAnxiety(float amount)
{
    anxietyLevel_ = std::clamp(anxietyLevel_ - amount, 0.0f, 100.0f);
}

std::string BaseScene::BuildAnxietyBar(float value) const
{
    constexpr int kSegments = 16;
    const int filled = static_cast<int>(std::round(std::clamp(value, 0.0f, 100.0f) / 100.0f * static_cast<float>(kSegments)));
    std::string bar = "ANSIEDAD [";
    for (int index = 0; index < kSegments; ++index)
    {
        bar += index < filled ? '#' : '-';
    }
    bar += "] ";
    bar += std::to_string(static_cast<int>(std::round(std::clamp(value, 0.0f, 100.0f))));
    bar += "%";
    return bar;
}

std::string BaseScene::BuildHoldActionProgressLine() const
{
    const char* label = activeHoldAction_ == HoldActionType::Shower
        ? "BANANDOTE"
        : "LAVANDO MANOS";
    constexpr int kSegments = 10;
    const float normalized = holdActionDuration_ > 0.001f
        ? std::clamp(holdActionProgress_ / holdActionDuration_, 0.0f, 1.0f)
        : 0.0f;
    const int filled = static_cast<int>(std::round(normalized * static_cast<float>(kSegments)));

    std::string line = label;
    line += " [";
    for (int index = 0; index < kSegments; ++index)
    {
        line += index < filled ? '#' : '-';
    }
    line += "]";
    return line;
}

void BaseScene::SetStoryPhase(StoryPhase phase)
{
    currentPhase_ = phase;
    storyPhaseStartTime_ = absoluteTimeSeconds_;
    DebugLog::Info("Story", "phase=", static_cast<int>(currentPhase_), " time=", storyPhaseStartTime_);
}

void BaseScene::UpdateStoryState(const PlayerSnapshot& player, float deltaTimeSeconds)
{
    if (centerMessage_.remainingSeconds > 0.0f)
    {
        centerMessage_.remainingSeconds = std::max(0.0f, centerMessage_.remainingSeconds - deltaTimeSeconds);
    }

    anxietyPulse_ = anxietySystemActive_
        ? std::sin(absoluteTimeSeconds_ * 4.1f) * (5.5f + anxietyLevel_ * 0.035f)
        : 0.0f;

    for (NarrativeTrigger& trigger : narrativeTriggers_)
    {
        if (trigger.activated || currentPhase_ != trigger.requiredPhase)
        {
            continue;
        }

        const float distance = glm::length(glm::vec2(player.position.x, player.position.z) - glm::vec2(trigger.position.x, trigger.position.z));
        if (distance > trigger.radius)
        {
            continue;
        }

        trigger.activated = true;
        SetStoryPhase(trigger.nextPhase);
        ActivateAnxiety(trigger.anxietyDelta);
        if (!trigger.messages.empty())
        {
            ShowCenterMessage(trigger.messages, 4.5f);
        }
        DebugLog::Info("Story", "Trigger activated ", trigger.id, " anxiety=", anxietyLevel_);
    }

    if (currentPhase_ == StoryPhase::HandWashedRelief && absoluteTimeSeconds_ - storyPhaseStartTime_ >= 4.5f)
    {
        SetStoryPhase(StoryPhase::NeedShower);
        ActivateAnxiety(18.0f);
        ShowCenterMessage({ "LA ANSIEDAD AUMENTA", "NECESITAS BANARTE" }, 4.2f);
    }

    if (currentPhase_ == StoryPhase::ShowerRelief && absoluteTimeSeconds_ - storyPhaseStartTime_ >= 15.0f)
    {
        SetStoryPhase(StoryPhase::NeedDiscardObjects);
        ActivateAnxiety(24.0f);
        ShowCenterMessage(
            {
                "SIGUES PENSANDO EN ESE CUERPO Y LA SANGRE",
                "TODAVIA SIENTES QUE LA CASA ESTA SUCIA"
            },
            5.5f);
    }

    if (currentPhase_ == StoryPhase::NeedHandWash
        || currentPhase_ == StoryPhase::NeedShower
        || currentPhase_ == StoryPhase::NeedDiscardObjects
        || currentPhase_ == StoryPhase::DiscardLoop)
    {
        const float riseRate = currentPhase_ == StoryPhase::DiscardLoop ? 3.0f : 2.2f;
        anxietyLevel_ = std::clamp(anxietyLevel_ + (riseRate * deltaTimeSeconds), 0.0f, 100.0f);
        if (anxietyLevel_ > 82.0f && centerMessage_.remainingSeconds <= 0.0f)
        {
            ShowCenterMessage({ "LA ANSIEDAD AUMENTA", "NECESITAS DESCONTAMINAR" }, 3.0f);
        }
    }

    if (darkReflectionUnlocked_)
    {
        anxietyLevel_ = std::max(0.0f, anxietyLevel_ - (10.0f * deltaTimeSeconds));
    }
}

bool BaseScene::IsPlayerNearDropZone(const glm::vec3& playerPosition) const
{
    const float distance = glm::length(glm::vec2(playerPosition.x, playerPosition.z) - glm::vec2(dumpsterDropZone_.position.x, dumpsterDropZone_.position.z));
    return distance <= dumpsterDropZone_.radius;
}

int BaseScene::FindTargetedCarryable(const glm::vec3& rayOrigin, const glm::vec3& rayDirection, const glm::vec3& playerPosition) const
{
    int bestIndex = -1;
    float bestHitDistance = std::numeric_limits<float>::max();
    constexpr float kMaxRayDistance = 7.0f;

    for (std::size_t index = 0; index < carryableObjects_.size(); ++index)
    {
        const CarryableObject& object = carryableObjects_[index];
        if (object.placement.model == nullptr || !object.visible || object.pickedUp || object.discarded)
        {
            continue;
        }

        const glm::mat4 model = BuildCarryableModelMatrix(object);
        const glm::vec3 localCenter = object.placement.model->GetCenter();
        const glm::vec3 worldCenter = glm::vec3(model * glm::vec4(localCenter, 1.0f));
        const glm::vec3 scaledSize = object.placement.model->GetSize() * object.placement.scale;
        const float radius = std::clamp(std::max({ scaledSize.x, scaledSize.y, scaledSize.z }) * 0.35f, 0.45f, 1.45f);
        const float playerDistance = glm::length(glm::vec2(playerPosition.x, playerPosition.z) - glm::vec2(worldCenter.x, worldCenter.z));
        if (playerDistance > object.interactRadius + radius)
        {
            continue;
        }

        float hitDistance = 0.0f;
        if (RayIntersectsSphere(rayOrigin, rayDirection, worldCenter, radius, kMaxRayDistance, hitDistance)
            && hitDistance < bestHitDistance)
        {
            bestHitDistance = hitDistance;
            bestIndex = static_cast<int>(index);
        }
    }

    return bestIndex;
}

int BaseScene::FindTargetedHouseLight(const glm::vec3& rayOrigin, const glm::vec3& rayDirection, const glm::vec3& playerPosition) const
{
    int bestIndex = -1;
    float bestHitDistance = std::numeric_limits<float>::max();
    constexpr float kMaxRayDistance = 7.0f;
    constexpr float kLightHitRadius = 0.42f;
    constexpr float kPlayerReach = 3.25f;

    for (std::size_t index = 0; index < houseLights_.size(); ++index)
    {
        const HouseLight& light = houseLights_[index];
        if (!light.interactable)
        {
            continue;
        }

        const float playerDistance = glm::length(playerPosition - light.position);
        if (playerDistance > kPlayerReach)
        {
            continue;
        }

        float hitDistance = 0.0f;
        if (RayIntersectsSphere(rayOrigin, rayDirection, light.position, kLightHitRadius, kMaxRayDistance, hitDistance)
            && hitDistance < bestHitDistance)
        {
            bestHitDistance = hitDistance;
            bestIndex = static_cast<int>(index);
        }
    }

    return bestIndex;
}

int BaseScene::FindTargetedFaucet(const glm::vec3& rayOrigin, const glm::vec3& rayDirection, const glm::vec3& playerPosition) const
{
    int bestIndex = -1;
    float bestHitDistance = std::numeric_limits<float>::max();
    constexpr float kMaxRayDistance = 6.0f;

    for (std::size_t index = 0; index < faucetInteractions_.size(); ++index)
    {
        const FaucetInteraction& faucet = faucetInteractions_[index];
        const bool phaseMatches =
            (faucet.type == HoldActionType::HandWash && currentPhase_ == StoryPhase::NeedHandWash)
            || (faucet.type == HoldActionType::Shower && currentPhase_ == StoryPhase::NeedShower)
            || (activeHoldAction_ != HoldActionType::None && faucet.id == activeHoldTargetId_);
        if (!phaseMatches)
        {
            continue;
        }

        const float playerDistance = glm::length(playerPosition - faucet.position);
        if (playerDistance > faucet.radius + 0.75f)
        {
            continue;
        }

        float hitDistance = 0.0f;
        if (RayIntersectsSphere(rayOrigin, rayDirection, faucet.position, 0.55f, kMaxRayDistance, hitDistance)
            && hitDistance < bestHitDistance)
        {
            bestHitDistance = hitDistance;
            bestIndex = static_cast<int>(index);
        }
    }

    return bestIndex;
}

bool BaseScene::TryUseFaucet(const glm::vec3& rayOrigin, const glm::vec3& rayDirection, const glm::vec3& playerPosition)
{
    const int faucetIndex = FindTargetedFaucet(rayOrigin, rayDirection, playerPosition);
    if (faucetIndex < 0)
    {
        return false;
    }

    FaucetInteraction& faucet = faucetInteractions_[static_cast<std::size_t>(faucetIndex)];
    if (faucet.type == HoldActionType::HandWash && currentPhase_ == StoryPhase::NeedHandWash)
    {
        activeHoldAction_ = HoldActionType::HandWash;
        activeHoldTargetId_ = faucet.id;
        activeHoldTargetPosition_ = faucet.position;
        holdActionProgress_ = 0.0f;
        holdActionDuration_ = 5.0f;
        SetStoryPhase(StoryPhase::HandWashing);
        DebugLog::Info("HOLD ACTION", "started hand wash target=", faucet.id);
        return true;
    }

    if (faucet.type == HoldActionType::Shower && currentPhase_ == StoryPhase::NeedShower)
    {
        activeHoldAction_ = HoldActionType::Shower;
        activeHoldTargetId_ = faucet.id;
        activeHoldTargetPosition_ = faucet.position;
        holdActionProgress_ = 0.0f;
        holdActionDuration_ = 5.0f;
        SetStoryPhase(StoryPhase::Showering);
        DebugLog::Info("HOLD ACTION", "started shower target=", faucet.id);
        return true;
    }

    return false;
}

void BaseScene::CancelHoldAction()
{
    if (activeHoldAction_ == HoldActionType::None)
    {
        return;
    }

    const HoldActionType previousAction = activeHoldAction_;
    activeHoldAction_ = HoldActionType::None;
    activeHoldTargetId_.clear();
    activeHoldTargetPosition_ = glm::vec3(0.0f);
    holdActionProgress_ = 0.0f;
    if (previousAction == HoldActionType::HandWash)
    {
        SetStoryPhase(StoryPhase::NeedHandWash);
    }
    else if (previousAction == HoldActionType::Shower)
    {
        SetStoryPhase(StoryPhase::NeedShower);
    }
    DebugLog::Info("HOLD ACTION", "cancelled");
}

bool BaseScene::TryToggleHouseLight(const glm::vec3& rayOrigin, const glm::vec3& rayDirection, const glm::vec3& playerPosition)
{
    const int lightIndex = FindTargetedHouseLight(rayOrigin, rayDirection, playerPosition);
    if (lightIndex < 0)
    {
        return false;
    }

    HouseLight& light = houseLights_[static_cast<std::size_t>(lightIndex)];
    light.enabled = !light.enabled;
    UpdateActivePointLights(playerPosition);
    ShowCenterMessage({ light.enabled ? "LUZ ENCENDIDA" : "LUZ APAGADA" }, 2.0f);
    DebugLog::Info("HOUSE LIGHT", "interactable toggled id=", light.id, " enabled=", light.enabled);
    return true;
}

void BaseScene::StartTvFallIfNeeded(const CarryableObject& pickedObject)
{
    if (tvFallActive_ || tvHasFallen_ || pickedObject.id != "livingroom_table")
    {
        return;
    }

    auto tvIterator = std::find_if(
        carryableObjects_.begin(),
        carryableObjects_.end(),
        [](const CarryableObject& object)
        {
            return object.id == "livingroom_tv";
        });
    if (tvIterator == carryableObjects_.end()
        || !tvIterator->visible
        || tvIterator->pickedUp
        || tvIterator->discarded
        || tvIterator->placement.model == nullptr)
    {
        return;
    }

    const glm::vec3 tvLocalSize = tvIterator->localMax - tvIterator->localMin;
    const float dropDistance = std::clamp(tvLocalSize.y * tvIterator->placement.scale * 1.35f, 0.45f, 1.10f);
    tvFallActive_ = true;
    tvFallStartTime_ = absoluteTimeSeconds_;
    tvFallStartPosition_ = tvIterator->worldPosition;
    tvFallTargetPosition_ = tvIterator->worldPosition - glm::vec3(0.0f, dropDistance, 0.0f);
    DebugLog::Info(
        "CARRY",
        "tv fall triggered by=", pickedObject.id,
        " dropDistance=", dropDistance,
        " startY=", tvFallStartPosition_.y,
        " targetY=", tvFallTargetPosition_.y);
}

void BaseScene::UpdateTvFall()
{
    if (!tvFallActive_)
    {
        return;
    }

    auto tvIterator = std::find_if(
        carryableObjects_.begin(),
        carryableObjects_.end(),
        [](const CarryableObject& object)
        {
            return object.id == "livingroom_tv";
        });
    if (tvIterator == carryableObjects_.end() || !tvIterator->visible || tvIterator->pickedUp || tvIterator->discarded)
    {
        tvFallActive_ = false;
        return;
    }

    constexpr float kTvFallDurationSeconds = 0.65f;
    const float t = std::clamp((absoluteTimeSeconds_ - tvFallStartTime_) / kTvFallDurationSeconds, 0.0f, 1.0f);
    const float eased = t * t * (3.0f - (2.0f * t));
    tvIterator->worldPosition = glm::mix(tvFallStartPosition_, tvFallTargetPosition_, eased);
    if (t >= 1.0f)
    {
        tvFallActive_ = false;
        tvHasFallen_ = true;
        DebugLog::Info("CARRY", "tv fall completed finalY=", tvIterator->worldPosition.y);
    }
}

bool BaseScene::TryPickupCarryable(int objectIndex)
{
    if (objectIndex < 0 || objectIndex >= static_cast<int>(carryableObjects_.size()) || carriedObjectIndex_ >= 0)
    {
        return false;
    }

    CarryableObject& object = carryableObjects_[static_cast<std::size_t>(objectIndex)];
    if (!CarryablesEnabled())
    {
        return false;
    }
    if (!object.visible || object.pickedUp || object.discarded)
    {
        return false;
    }

    object.pickedUp = true;
    object.carried = true;
    object.visible = false;
    object.blocksNavigation = false;
    carriedObjectIndex_ = objectIndex;
    StartTvFallIfNeeded(object);
    if (currentPhase_ == StoryPhase::NeedDiscardObjects)
    {
        SetStoryPhase(StoryPhase::DiscardLoop);
    }
    ShowCenterMessage({ "LLEVANDO: " + object.displayName, "LLEVALO AL BASURERO" }, 3.0f);
    ReduceAnxiety(8.0f);
    DebugLog::Info("CARRY", "picked object id=", object.id, " visible=", object.visible, " original house element hidden=true");
    return true;
}

bool BaseScene::TryDiscardCarriedObject(const glm::vec3& rayOrigin, const glm::vec3& rayDirection, const glm::vec3& playerPosition)
{
    if (carriedObjectIndex_ < 0
        || !IsPlayerNearDropZone(playerPosition)
        || !IsRayTargetingDropZone(rayOrigin, rayDirection, dumpsterDropZone_.position, 7.0f))
    {
        return false;
    }

    CarryableObject& object = carryableObjects_[static_cast<std::size_t>(carriedObjectIndex_)];
    object.carried = false;
    object.discarded = true;
    object.visible = false;
    object.blocksNavigation = false;
    carriedObjectIndex_ = -1;
    discardedCount_ += object.requiredForEmptyHouse ? 1 : 0;
    SetStoryPhase(StoryPhase::DiscardLoop);
    ReduceAnxiety(18.0f);
    ShowCenterMessage({ "ALIVIO TEMPORAL", "EL ESPACIO SE VACIA" }, 3.2f);
    DebugLog::Info("CARRY", "discarded id=", object.id, " visible=", object.visible, " discarded=", discardedCount_, "/", requiredDiscardCount_);

    if (discardedCount_ >= requiredDiscardCount_ && !darkReflectionUnlocked_)
    {
        darkReflectionUnlocked_ = true;
        SetStoryPhase(StoryPhase::DarkReflection);
        ShowCenterMessage({ "EL ESPACIO QUEDA VACIO", "LA SENSACION COMIENZA A BAJAR" }, 6.0f);
    }
    return true;
}

void BaseScene::LoadEntities()
{
    DebugLog::Info("BaseScene", "Loading scene entities");
    entities_.clear();
    doors_.clear();
    narrativeTriggers_.clear();
    carryableObjects_.clear();
    faucetInteractions_.clear();
    carriedObjectIndex_ = -1;
    discardedCount_ = 0;
    cleanedCount_ = 0;
    currentPhase_ = StoryPhase::ExteriorStart;
    storyPhaseStartTime_ = 0.0f;
    anxietyLevel_ = 0.0f;
    activeHoldAction_ = HoldActionType::None;
    activeHoldTargetId_.clear();
    activeHoldTargetPosition_ = glm::vec3(0.0f);
    holdActionProgress_ = 0.0f;
    tvFallActive_ = false;
    tvHasFallen_ = false;
    tvFallStartTime_ = 0.0f;
    tvFallStartPosition_ = glm::vec3(0.0f);
    tvFallTargetPosition_ = glm::vec3(0.0f);
    anxietyPulse_ = 0.0f;
    contaminationLevel_ = 0.0f;
    anxietySystemActive_ = false;
    darkReflectionUnlocked_ = false;
    centerMessage_ = TimedMessage {};
    staticCollisionSources_.clear();
    LoadHouseDemo();
    LoadExteriorDecorations();
}

void BaseScene::ConfigureSceneLights()
{
    ReleasePointLightShadowMaps();
    basePointLights_.clear();
    pointLights_.clear();
    houseLights_.clear();

    for (const SceneEntity& entity : entities_)
    {
        const std::string lowerName = ToLowerAscii(entity.name);
        if (lowerName.find("street-light") == std::string::npos)
        {
            continue;
        }

        PointLight pointLight;
        pointLight.label = lowerName;
        bool foundPostLightNode = false;
        if (entity.placement.model != nullptr)
        {
            const glm::mat4 entityTransform = BuildStaticModelMatrix(entity);
            for (const Model::NamedNode& node : entity.placement.model->GetNamedNodes())
            {
                if (ToLowerAscii(node.name) == "light")
                {
                    pointLight.position = glm::vec3(entityTransform * node.transform * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
                    foundPostLightNode = true;
                    break;
                }
            }
        }
        if (!foundPostLightNode)
        {
            pointLight.position = ComputeStreetLightAnchor(entity);
        }
        pointLight.color = glm::vec3(1.0f, 0.88f, 0.64f);
        pointLight.intensity = 4.6f;
        pointLight.range = 8.4f;
        pointLight.castsShadow = false;
        basePointLights_.push_back(pointLight);
        DebugLog::Info(
            "POST LIGHT",
            "node=", foundPostLightNode ? "light" : "fallback-anchor",
            " position=(",
            pointLight.position.x, ", ", pointLight.position.y, ", ", pointLight.position.z,
            ") point light created=true");
    }

    auto houseIterator = std::find_if(
        entities_.begin(),
        entities_.end(),
        [](const SceneEntity& entity)
        {
            return ToLowerAscii(entity.name).find("house/source/example16_var1.fbx") != std::string::npos;
        });
    if (houseIterator != entities_.end() && houseIterator->placement.model != nullptr)
    {
        const glm::mat4 houseTransform = BuildStaticModelMatrix(*houseIterator);

        std::size_t normalLightCount = 0;
        std::size_t proximityLightCount = 0;
        std::size_t interactableLightCount = 0;

        auto addHouseLight = [&](const std::string& id, const glm::vec3& localPosition, bool interactable, bool proximity)
        {
            HouseLight houseLight;
            houseLight.id = id;
            houseLight.position = glm::vec3(houseTransform * glm::vec4(localPosition, 1.0f));
            houseLight.color = proximity ? glm::vec3(1.0f, 0.78f, 0.56f) : glm::vec3(1.0f, 0.84f, 0.62f);
            houseLight.intensity = interactable ? 2.10f : (proximity ? 1.25f : 1.85f);
            houseLight.range = interactable ? 5.8f : (proximity ? 4.7f : 8.0f);
            houseLight.activationDistance = interactable ? 8.5f : (proximity ? 4.25f : 120.0f);
            houseLight.enabled = !interactable;
            houseLight.interactable = interactable;
            houseLight.proximity = proximity;
            houseLights_.push_back(houseLight);
        };

        for (const Model::NamedNode& node : houseIterator->placement.model->GetNamedNodes())
        {
            const std::string lowerNodeName = ToLowerAscii(node.name);
            const bool strictAutoLight = IsStrictHouseLightNodeName(lowerNodeName);
            const bool interactableLight = IsInteractableHouseLightNodeName(lowerNodeName);
            const bool proximityLight = IsProximityHouseLightNodeName(lowerNodeName);
            if (!strictAutoLight && !interactableLight && !proximityLight)
            {
                continue;
            }

            const glm::vec3 localPosition = glm::vec3(node.transform * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
            normalLightCount += strictAutoLight ? 1u : 0u;
            proximityLightCount += proximityLight ? 1u : 0u;
            interactableLightCount += interactableLight ? 1u : 0u;
            DebugLog::Info(
                "HOUSE LIGHT",
                "node=", node.name,
                " accepted=", strictAutoLight,
                " proximity=", proximityLight,
                " interactable=", interactableLight,
                " local=(",
                localPosition.x, ", ", localPosition.y, ", ", localPosition.z, ")");
            addHouseLight(lowerNodeName, localPosition, interactableLight, proximityLight);
        }
        DebugLog::Info(
            "HOUSE LIGHT",
            "summary normalLight###=", normalLightCount,
            " proximity=", proximityLightCount,
            " interactable=", interactableLightCount);
    }

    UpdateActivePointLights(playerSpawnPosition_);
    const std::size_t activeRegularHouseLights = static_cast<std::size_t>(std::count_if(
        houseLights_.begin(),
        houseLights_.end(),
        [](const HouseLight& light)
        {
            return light.active && !light.proximity && !light.interactable;
        }));
    const std::size_t activeProximityHouseLights = static_cast<std::size_t>(std::count_if(
        houseLights_.begin(),
        houseLights_.end(),
        [](const HouseLight& light)
        {
            return light.active && light.proximity;
        }));
    const std::size_t activeInteractableHouseLights = static_cast<std::size_t>(std::count_if(
        houseLights_.begin(),
        houseLights_.end(),
        [](const HouseLight& light)
        {
            return light.active && light.interactable;
        }));
    DebugLog::Info(
        "LIGHTS",
        "detected light###=", static_cast<int>(std::count_if(
            houseLights_.begin(),
            houseLights_.end(),
            [](const HouseLight& light)
            {
                return !light.proximity && !light.interactable;
            })),
        " active regular=", activeRegularHouseLights,
        " active proximity=", activeProximityHouseLights,
        " active interactable=", activeInteractableHouseLights,
        " submitted to shader=", pointLights_.size(),
        " / ", kMaxPointLights);

    const std::size_t shadowCastingPointLights = static_cast<std::size_t>(std::count_if(
        pointLights_.begin(),
        pointLights_.end(),
        [](const PointLight& light)
        {
            return light.castsShadow;
        }));
    DebugLog::Info(
        "BaseScene",
        "Lighting configured sunDirection=(",
        sunDirection_.x, ", ", sunDirection_.y, ", ", sunDirection_.z,
        ") sunColor=(",
        sunColor_.x, ", ", sunColor_.y, ", ", sunColor_.z,
        ") skyAmbient=(",
        ambientSkyColor_.x, ", ", ambientSkyColor_.y, ", ", ambientSkyColor_.z,
        ") groundAmbient=(",
        ambientGroundColor_.x, ", ", ambientGroundColor_.y, ", ", ambientGroundColor_.z,
        ") pointLights=", pointLights_.size(),
        " houseLightsAvailable=", houseLights_.size(),
        " shadowPointLights=", shadowCastingPointLights,
        " pointShadowResolution=", pointShadowResolution_);
    AllocatePointLightShadowMaps();
    pointShadowMapsDirty_ = true;
}

void BaseScene::UpdateActivePointLights(const glm::vec3& playerPosition)
{
    constexpr std::size_t kMaxTotalActivePointLights = static_cast<std::size_t>(kMaxPointLights);

    pointLights_ = basePointLights_;
    for (HouseLight& light : houseLights_)
    {
        light.active = false;
    }

    std::vector<std::size_t> candidates;
    candidates.reserve(houseLights_.size());
    for (std::size_t index = 0; index < houseLights_.size(); ++index)
    {
        const HouseLight& light = houseLights_[index];
        if (!light.enabled)
        {
            continue;
        }

        const float distance = glm::length(playerPosition - light.position);
        if (distance <= light.activationDistance)
        {
            candidates.push_back(index);
        }
    }

    std::sort(
        candidates.begin(),
        candidates.end(),
        [&](std::size_t lhs, std::size_t rhs)
        {
            return glm::length(playerPosition - houseLights_[lhs].position)
                < glm::length(playerPosition - houseLights_[rhs].position);
        });

    for (std::size_t index : candidates)
    {
        if (pointLights_.size() >= kMaxTotalActivePointLights)
        {
            break;
        }

        HouseLight& houseLight = houseLights_[index];
        PointLight pointLight;
        pointLight.label = houseLight.id;
        pointLight.position = houseLight.position;
        pointLight.color = houseLight.color;
        pointLight.intensity = houseLight.intensity;
        pointLight.range = houseLight.range;
        pointLight.castsShadow = false;
        pointLights_.push_back(pointLight);
        houseLight.active = true;
    }
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
    LoadHouseCarryableObjects(houseEntity);
    faucetInteractions_.clear();
    const glm::mat4 houseTransform = BuildStaticModelMatrix(houseEntity);
    for (const Model::NamedNode& node : houseEntity.placement.model->GetNamedNodes())
    {
        const std::string lowerNodeName = ToLowerAscii(node.name);
        const bool isGrifo = lowerNodeName.rfind("grifo", 0) == 0;
        const bool isRegadera = lowerNodeName == "regadera" || lowerNodeName.find("regadera") != std::string::npos;
        if (!isGrifo && !isRegadera)
        {
            continue;
        }

        FaucetInteraction interaction;
        interaction.id = lowerNodeName;
        interaction.position = glm::vec3(houseTransform * node.transform * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        interaction.radius = isRegadera ? 1.25f : 1.10f;
        interaction.type = isRegadera ? HoldActionType::Shower : HoldActionType::HandWash;
        faucetInteractions_.push_back(interaction);
        DebugLog::Info(
            "WASH NODE",
            "node=", node.name,
            " type=", isRegadera ? "regadera" : "grifo",
            " position=(",
            interaction.position.x, ", ", interaction.position.y, ", ", interaction.position.z,
            ")");
    }
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

    const std::vector<DecorationDesc> decorations {
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
        DecorationDesc { "street-light-start", assetsRoot_ / "models" / "street-light" / "source" / "Street Light" / "SM Street Light.fbx", glm::vec3(4.4f, 0.0f, 22.3f), -90.0f, 5.8f, true },
        DecorationDesc { "dumpster-left", assetsRoot_ / "models" / "dumpster.fbx", glm::vec3(-13.35f, 0.0f, -7.74f), 178.0f, 2.8f, false },
        DecorationDesc { "flower-left-house", assetsRoot_ / "models" / "flower" / "source" / "x3.fbx", glm::vec3(-8.4f, 0.0f, -5.0f), 12.0f, 1.2f, true },
        DecorationDesc { "bush-back-right", assetsRoot_ / "models" / "bushes" / "BushWithBerrys03.fbx", glm::vec3(10.8f, 0.0f, -28.0f), 31.0f, 1.7f, true },
        DecorationDesc { "tree-request-01", assetsRoot_ / "models" / "tree.fbx", glm::vec3(0.2f, 0.0f, -1.0f), 38.0f, 6.2f, true },
        DecorationDesc { "tree-request-02", assetsRoot_ / "models" / "tree.fbx", glm::vec3(5.4f, 0.0f, 1.3f), -42.0f, 6.4f, true },
        DecorationDesc { "tree-request-03", assetsRoot_ / "models" / "tree.fbx", glm::vec3(7.2f, 0.0f, 8.2f), 71.0f, 6.5f, true },
        DecorationDesc { "tree-request-04", assetsRoot_ / "models" / "tree.fbx", glm::vec3(5.1f, 0.0f, 13.98f), -28.0f, 6.4f, true },
        DecorationDesc { "tree-request-05", assetsRoot_ / "models" / "tree.fbx", glm::vec3(8.8f, 0.0f, 23.67f), 16.0f, 6.5f, true },
        DecorationDesc { "tree-request-06", assetsRoot_ / "models" / "tree.fbx", glm::vec3(-5.1f, 0.0f, 29.22f), -64.0f, 6.2f, true },
        DecorationDesc { "tree-request-07", assetsRoot_ / "models" / "tree.fbx", glm::vec3(-6.8f, 0.0f, 16.8f), 97.0f, 6.3f, true },
        DecorationDesc { "tree-request-08", assetsRoot_ / "models" / "tree.fbx", glm::vec3(-8.2f, 0.0f, 2.4f), -18.0f, 6.2f, true },
        DecorationDesc { "nature-bush-01", assetsRoot_ / "models" / "bushes" / "BushWithBerrys02.fbx", glm::vec3(1.8f, 0.0f, -2.2f), 25.0f, 1.45f, true },
        DecorationDesc { "nature-bush-02", assetsRoot_ / "models" / "bushes" / "Bush01.fbx", glm::vec3(6.8f, 0.0f, 2.8f), -34.0f, 1.40f, true },
        DecorationDesc { "nature-bush-03", assetsRoot_ / "models" / "bushes" / "Bush02.fbx", glm::vec3(6.0f, 0.0f, 9.9f), 58.0f, 1.35f, true },
        DecorationDesc { "nature-bush-04", assetsRoot_ / "models" / "bushes" / "BushWithBerrys01.fbx", glm::vec3(-6.0f, 0.0f, 18.5f), -12.0f, 1.45f, true },
        DecorationDesc { "nature-bush-05", assetsRoot_ / "models" / "bushes" / "Bush03.fbx", glm::vec3(-7.4f, 0.0f, 4.8f), 41.0f, 1.35f, true },
        DecorationDesc { "nature-grass-01", assetsRoot_ / "models" / "nature" / "grass_01.fbx", glm::vec3(2.4f, 0.0f, 0.8f), 0.0f, 0.95f, true },
        DecorationDesc { "nature-grass-02", assetsRoot_ / "models" / "nature" / "grass_02.fbx", glm::vec3(6.4f, 0.0f, 7.0f), 18.0f, 0.90f, true },
        DecorationDesc { "nature-grass-03", assetsRoot_ / "models" / "nature" / "grass_01.fbx", glm::vec3(-6.2f, 0.0f, 14.9f), -21.0f, 0.90f, true },
        DecorationDesc { "nature-grass-04", assetsRoot_ / "models" / "nature" / "grass_02.fbx", glm::vec3(7.6f, 0.0f, 22.1f), 44.0f, 0.88f, true },
        DecorationDesc { "nature-mushrooms-01", assetsRoot_ / "models" / "nature" / "mushrooms.fbx", glm::vec3(4.3f, 0.0f, 3.4f), -18.0f, 0.85f, true },
        DecorationDesc { "nature-mushrooms-02", assetsRoot_ / "models" / "nature" / "mushrooms_02.fbx", glm::vec3(-7.8f, 0.0f, 1.0f), 31.0f, 0.82f, true },
        DecorationDesc { "nature-flower-01", assetsRoot_ / "models" / "nature" / "flower.fbx", glm::vec3(5.8f, 0.0f, 14.8f), 12.0f, 0.80f, true },
        DecorationDesc { "nature-flower-02", assetsRoot_ / "models" / "nature" / "flower.fbx", glm::vec3(-4.3f, 0.0f, 27.8f), -27.0f, 0.78f, true }
    };

    const auto blocksNavigation = [](std::string name)
    {
        name = ToLowerAscii(std::move(name));
        if (name.find("flower") != std::string::npos
            || name.find("grass") != std::string::npos
            || name.find("mushroom") != std::string::npos)
        {
            return false;
        }
        return name.find("tree") != std::string::npos
            || name.find("bush") != std::string::npos
            || name.find("rock") != std::string::npos
            || name.find("street-light") != std::string::npos
            || name.find("dumpster") != std::string::npos;
    };

    for (const DecorationDesc& decoration : decorations)
    {
        glm::vec3 position = decoration.position;
        if (ToLowerAscii(decoration.name).find("bush") != std::string::npos)
        {
            position.y -= 0.20f;
        }

        AddStaticSceneEntity(
            decoration.name,
            decoration.path,
            position,
            decoration.yawDegrees,
            decoration.targetSize,
            0.0f,
            decoration.normalizeToHeight,
            true,
            blocksNavigation(decoration.name));
    }

    AddStaticSceneEntity(
        "detonante-sangre-01",
        assetsRoot_ / "models" / "blood-spattered" / "source" / "sangre.fbx",
        glm::vec3(-2.7f, -0.025f, 13.4f),
        8.0f,
        3.0f,
        0.0f,
        false,
        true);
    AddStaticSceneEntity(
        "detonante-sangre-02",
        assetsRoot_ / "models" / "blood-spattered" / "source" / "sangre.fbx",
        glm::vec3(-3.25f, -0.025f, 12.85f),
        -24.0f,
        2.8f,
        0.0f,
        false,
        true);
    AddStaticSceneEntity(
        "detonante-sangre-03",
        assetsRoot_ / "models" / "blood-spattered" / "source" / "sangre.fbx",
        glm::vec3(-1.95f, -0.025f, 13.05f),
        31.0f,
        2.7f,
        0.0f,
        false,
        true);
    AddStaticSceneEntity(
        "detonante-sangre-04",
        assetsRoot_ / "models" / "blood-spattered" / "source" / "sangre.fbx",
        glm::vec3(-2.35f, -0.025f, 14.25f),
        83.0f,
        2.9f,
        0.0f,
        false,
        true);
    AddStaticSceneEntity(
        "detonante-sleeping-bag-mummy",
        assetsRoot_ / "models" / "nuevos" / "sleeping bag" / "FBX" / "sleeping bag Mummy.fbx",
        glm::vec3(1.0f, 0.0f, 8.8f),
        -28.0f,
        2.64f,
        0.0f,
        false,
        true);
    AddStaticSceneEntity(
        "basura-inicial-01",
        assetsRoot_ / "models" / "trash-bag" / "Trash_Bag_Pack_ve2hddjga_High.fbx",
        glm::vec3(-13.2f, -0.04f, -5.5f),
        34.0f,
        1.20f,
        0.0f,
        false,
        true);
    AddStaticSceneEntity(
        "basura-inicial-02",
        assetsRoot_ / "models" / "trash-bag" / "Trash_Bag_Pack_ve2hddjga_High.fbx",
        glm::vec3(-13.25f, -0.045f, -1.67f),
        -18.0f,
        1.27f,
        0.0f,
        false,
        true);
    AddStaticSceneEntity(
        "basura-inicial-03",
        assetsRoot_ / "models" / "trash-bag" / "Trash_Bag_Pack_ve2hddjga_High.fbx",
        glm::vec3(-11.88f, -0.04f, -8.86f),
        61.0f,
        1.16f,
        0.0f,
        false,
        true);

    AddNarrativeTrigger(
        "trigger_blood_spot",
        glm::vec3(-2.7f, 0.0f, 13.4f),
        3.2f,
        StoryPhase::ExteriorStart,
        StoryPhase::TriggerWalk,
        {
            "ALGO EN EL AMBIENTE SE SIENTE CONTAMINADO",
            "LA SENSACION NO DESAPARECE"
        },
        34.0f);
    AddNarrativeTrigger(
        "trigger_body_or_scene",
        glm::vec3(1.0f, 0.0f, 8.8f),
        3.5f,
        StoryPhase::TriggerWalk,
        StoryPhase::AnxietyActivated,
        {
            "LA IMAGEN DE ESE CUERPO SE QUEDA EN TU CABEZA"
        },
        28.0f);
    AddNarrativeTrigger(
        "trigger_house_threshold",
        glm::vec3(0.0f, 0.0f, -12.2f),
        5.0f,
        StoryPhase::AnxietyActivated,
        StoryPhase::NeedHandWash,
        {},
        14.0f);

    dumpsterDropZone_.id = "basurero";
    dumpsterDropZone_.position = glm::vec3(-13.35f, 0.0f, -7.74f);
    dumpsterDropZone_.radius = 3.4f;

    DebugLog::Info(
        "BaseScene",
        "Exterior decorations loaded, visualEntities=", entities_.size(),
        " collisionSources=", staticCollisionSources_.size());
}

void BaseScene::LoadHouseDoors(const SceneEntity& houseEntity)
{
    const std::array<fs::path, 5> doorPaths {
        assetsRoot_ / "models" / "house" / "source" / "garage_door.fbx",
        assetsRoot_ / "models" / "house" / "source" / "living-room_door.fbx",
        assetsRoot_ / "models" / "house" / "source" / "kitchen_door.fbx",
        assetsRoot_ / "models" / "house" / "source" / "bedroom_door.fbx",
        assetsRoot_ / "models" / "house" / "source" / "bathroom_door.fbx"
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
        if (lowerName.find("living-room") != std::string::npos)
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
        else if (lowerName.find("bedroom") != std::string::npos)
        {
            if (localSize.x >= localSize.z)
            {
                door.localHinge.x = door.localMax.x;
            }
            else
            {
                door.localHinge.z = door.localMax.z;
            }
            door.openAngleDegrees = 92.0f;
            door.interactRadius = 1.9f;
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

void BaseScene::LoadHouseCarryableObjects(const SceneEntity& houseEntity)
{
    struct CarryableDesc
    {
        const char* id;
        const char* displayName;
        fs::path path;
        bool required;
    };

    const fs::path elementsRoot = assetsRoot_ / "models" / "house" / "source" / "houseElements";
    const std::array<CarryableDesc, 11> carryables {
        CarryableDesc { "dinning_chair01", "SILLA", elementsRoot / "dinning_chair01.fbx", true },
        CarryableDesc { "dinning_chair02", "SILLA", elementsRoot / "dinning_chair02.fbx", true },
        CarryableDesc { "dinning_chair03", "SILLA", elementsRoot / "dinning_chair03.fbx", true },
        CarryableDesc { "dinning_chair04", "SILLA", elementsRoot / "dinning_chair04.fbx", true },
        CarryableDesc { "dinning_table", "MESA", elementsRoot / "dinning_table.fbx", true },
        CarryableDesc { "livingroom_chair01", "SILLON", elementsRoot / "livingroom_chair01.fbx", true },
        CarryableDesc { "livingroom_chair02", "SILLON", elementsRoot / "livingroom_chair02.fbx", true },
        CarryableDesc { "livingroom_cushions_big", "COJINES", elementsRoot / "livingroom_cushions_big.fbx", true },
        CarryableDesc { "livingroom_cushions_small", "COJINES", elementsRoot / "livingroom_cushions_small.fbx", true },
        CarryableDesc { "livingroom_table", "MESA DE SALA", elementsRoot / "livingroom_table.fbx", true },
        CarryableDesc { "livingroom_tv", "TELEVISION", elementsRoot / "livingroom_tv.fbx", true }
    };

    for (const CarryableDesc& desc : carryables)
    {
        if (!fs::exists(desc.path))
        {
            DebugLog::Info("BaseScene", "House element asset not found, skipping ", desc.path.string());
            continue;
        }

        CarryableObject object;
        object.id = desc.id;
        object.displayName = desc.displayName;
        object.worldPosition = houseEntity.worldPosition;
        object.worldYawDegrees = houseEntity.worldYawDegrees;
        object.requiredForEmptyHouse = desc.required;
        object.placement.sourcePath = desc.path;
        object.placement.scale = houseEntity.placement.scale;
        object.placement.rawOffset = houseEntity.placement.rawOffset;
        object.placement.yawOffsetDegrees = houseEntity.placement.yawOffsetDegrees;
        object.placement.model = std::make_unique<Model>(desc.path, true);
        if (!object.placement.model->IsLoaded())
        {
            DebugLog::Error("BaseScene", "Failed to load house carryable ", desc.path.string());
            continue;
        }
        object.localMin = object.placement.model->GetMinBounds();
        object.localMax = object.placement.model->GetMaxBounds();
        object.blocksNavigation = true;

        DebugLog::Info("BaseScene", "Carryable loaded ", object.id, " display=", object.displayName);
        carryableObjects_.push_back(std::move(object));
    }

    totalDiscardableObjects_ = static_cast<int>(std::count_if(
        carryableObjects_.begin(),
        carryableObjects_.end(),
        [](const CarryableObject& object)
        {
            return object.requiredForEmptyHouse;
        }));
    requiredDiscardCount_ = std::max(1, static_cast<int>(std::ceil(static_cast<float>(totalDiscardableObjects_) * 0.70f)));
    DebugLog::Info(
        "BaseScene",
        "House carryables loaded total=", carryableObjects_.size(),
        " requiredForEmptyHouse=", totalDiscardableObjects_,
        " requiredDiscardCount=", requiredDiscardCount_);
}

void BaseScene::AddStaticSceneEntity(
    const std::string& name,
    const fs::path& path,
    const glm::vec3& worldPosition,
    float worldYawDegrees,
    float targetSize,
    float yawOffsetDegrees,
    bool normalizeToHeight,
    bool loadTextures,
    bool registerCollision)
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

    if (registerCollision)
    {
        RegisterStaticCollisionSource(entity);
    }
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

glm::mat4 BaseScene::BuildCarryableModelMatrix(const CarryableObject& object) const
{
    glm::mat4 model = glm::translate(glm::mat4(1.0f), object.worldPosition);
    model = glm::rotate(model, glm::radians(object.worldYawDegrees + object.placement.yawOffsetDegrees), glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::scale(model, glm::vec3(object.placement.scale));
    model = glm::translate(model, object.placement.rawOffset);
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

WalkableBlocker BaseScene::BuildCarryableBlocker(const CarryableObject& object) const
{
    WalkableBlocker blocker;
    blocker.name = object.id;
    blocker.enabled = object.visible && !object.pickedUp && !object.discarded && object.blocksNavigation;

    const glm::mat4 model = BuildCarryableModelMatrix(object);
    const glm::vec3 localCenter = (object.localMin + object.localMax) * 0.5f;
    const glm::vec3 localHalf = (object.localMax - object.localMin) * 0.5f;
    blocker.center = glm::vec3(model * glm::vec4(localCenter, 1.0f));

    const glm::vec3 axisX = glm::vec3(model * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
    const glm::vec3 axisY = glm::vec3(model * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));
    const glm::vec3 axisZ = glm::vec3(model * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f));
    const float scaleX = std::max(glm::length(axisX), 0.001f);
    const float scaleY = std::max(glm::length(axisY), 0.001f);
    const float scaleZ = std::max(glm::length(axisZ), 0.001f);

    blocker.halfExtents = glm::vec3(
        std::clamp(localHalf.x * scaleX, 0.20f, 1.35f),
        std::clamp(localHalf.y * scaleY, 0.35f, 1.80f),
        std::clamp(localHalf.z * scaleZ, 0.20f, 1.35f));
    const glm::vec3 normalizedAxisX = glm::normalize(axisX);
    blocker.yawDegrees = glm::degrees(std::atan2(normalizedAxisX.z, normalizedAxisX.x));
    return blocker;
}

glm::mat4 BaseScene::BuildSunLightSpaceMatrix() const
{
    const glm::vec3 sceneCenter(
        (sceneBoundsMin_.x + sceneBoundsMax_.x) * 0.5f,
        5.0f,
        (sceneBoundsMin_.z + sceneBoundsMax_.z) * 0.5f);
    const glm::vec3 sunDirection = glm::normalize(sunDirection_);
    const glm::vec3 lightPosition = sceneCenter - (sunDirection * 36.0f);
    const glm::mat4 lightView = glm::lookAt(lightPosition, sceneCenter, glm::vec3(0.0f, 1.0f, 0.0f));

    std::array<glm::vec3, 8> sceneCorners {
        glm::vec3(sceneBoundsMin_.x, 0.0f, sceneBoundsMin_.z),
        glm::vec3(sceneBoundsMax_.x, 0.0f, sceneBoundsMin_.z),
        glm::vec3(sceneBoundsMin_.x, 0.0f, sceneBoundsMax_.z),
        glm::vec3(sceneBoundsMax_.x, 0.0f, sceneBoundsMax_.z),
        glm::vec3(sceneBoundsMin_.x, sceneBoundsMax_.y, sceneBoundsMin_.z),
        glm::vec3(sceneBoundsMax_.x, sceneBoundsMax_.y, sceneBoundsMin_.z),
        glm::vec3(sceneBoundsMin_.x, sceneBoundsMax_.y, sceneBoundsMax_.z),
        glm::vec3(sceneBoundsMax_.x, sceneBoundsMax_.y, sceneBoundsMax_.z)
    };

    glm::vec3 lightSpaceMin(std::numeric_limits<float>::max());
    glm::vec3 lightSpaceMax(std::numeric_limits<float>::lowest());
    for (const glm::vec3& corner : sceneCorners)
    {
        const glm::vec3 lightSpacePoint = glm::vec3(lightView * glm::vec4(corner, 1.0f));
        lightSpaceMin = glm::min(lightSpaceMin, lightSpacePoint);
        lightSpaceMax = glm::max(lightSpaceMax, lightSpacePoint);
    }

    constexpr float kShadowPadding = 4.0f;
    lightSpaceMin -= glm::vec3(kShadowPadding);
    lightSpaceMax += glm::vec3(kShadowPadding);
    const glm::mat4 lightProjection = glm::ortho(
        lightSpaceMin.x,
        lightSpaceMax.x,
        lightSpaceMin.y,
        lightSpaceMax.y,
        -lightSpaceMax.z - kShadowPadding,
        -lightSpaceMin.z + kShadowPadding);
    return lightProjection * lightView;
}

glm::vec3 BaseScene::ComputeStreetLightAnchor(const SceneEntity& entity) const
{
    if (entity.placement.model == nullptr)
    {
        return entity.worldPosition + glm::vec3(0.0f, 4.0f, 0.0f);
    }

    const glm::vec3 localMin = entity.placement.model->GetMinBounds();
    const glm::vec3 localMax = entity.placement.model->GetMaxBounds();
    const glm::vec3 localCenter = (localMin + localMax) * 0.5f;
    const glm::vec3 localSize = localMax - localMin;
    glm::vec3 localLampPoint(localCenter.x, glm::mix(localMin.y, localMax.y, 0.88f), localCenter.z);
    localLampPoint += glm::vec3(0.0f, 5.0f - (localSize.y * 0.04f), localSize.z * 0.325f);
    const glm::vec3 worldLampPoint = glm::vec3(BuildStaticModelMatrix(entity) * glm::vec4(localLampPoint, 1.0f));
    return worldLampPoint;
}

void BaseScene::RenderPointShadowMaps() const
{
    if (pointShadowFramebuffer_ == 0 || pointShadowDepthShader_ == nullptr || pointLights_.empty())
    {
        return;
    }

    GLint previousViewport[4] { 0, 0, 0, 0 };
    glGetIntegerv(GL_VIEWPORT, previousViewport);

    glViewport(0, 0, pointShadowResolution_, pointShadowResolution_);
    glBindFramebuffer(GL_FRAMEBUFFER, pointShadowFramebuffer_);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(2.0f, 4.0f);

    pointShadowDepthShader_->Use();
    constexpr float kNearPlane = 0.08f;

    const std::size_t shadowLightCount = std::min(pointLights_.size(), static_cast<std::size_t>(kMaxPointLights));
    for (std::size_t lightIndex = 0; lightIndex < shadowLightCount; ++lightIndex)
    {
        const PointLight& pointLight = pointLights_[lightIndex];
        if (!pointLight.castsShadow || pointLight.shadowCubeMap == 0)
        {
            continue;
        }

        const float farPlane = std::max(pointLight.range, 0.5f);
        const glm::mat4 projection = glm::perspective(glm::radians(90.0f), 1.0f, kNearPlane, farPlane);
        const std::array<glm::mat4, 6> shadowTransforms {
            projection * glm::lookAt(pointLight.position, pointLight.position + glm::vec3( 1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
            projection * glm::lookAt(pointLight.position, pointLight.position + glm::vec3(-1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
            projection * glm::lookAt(pointLight.position, pointLight.position + glm::vec3( 0.0f,  1.0f,  0.0f), glm::vec3(0.0f,  0.0f,  1.0f)),
            projection * glm::lookAt(pointLight.position, pointLight.position + glm::vec3( 0.0f, -1.0f,  0.0f), glm::vec3(0.0f,  0.0f, -1.0f)),
            projection * glm::lookAt(pointLight.position, pointLight.position + glm::vec3( 0.0f,  0.0f,  1.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
            projection * glm::lookAt(pointLight.position, pointLight.position + glm::vec3( 0.0f,  0.0f, -1.0f), glm::vec3(0.0f, -1.0f,  0.0f))
        };

        pointShadowDepthShader_->SetVec3("lightPosition", pointLight.position);
        pointShadowDepthShader_->SetFloat("farPlane", farPlane);
        for (int face = 0; face < 6; ++face)
        {
            glFramebufferTexture2D(
                GL_FRAMEBUFFER,
                GL_DEPTH_ATTACHMENT,
                GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
                pointLight.shadowCubeMap,
                0);
            glClear(GL_DEPTH_BUFFER_BIT);
            pointShadowDepthShader_->SetMat4("shadowMatrix", shadowTransforms[static_cast<std::size_t>(face)]);
            DrawShadowCasters(*pointShadowDepthShader_);
        }
    }

    glDisable(GL_POLYGON_OFFSET_FILL);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
}

void BaseScene::AllocatePointLightShadowMaps()
{
    const std::size_t shadowLightCount = std::min(pointLights_.size(), static_cast<std::size_t>(kMaxPointLights));
    std::size_t allocatedCount = 0;
    for (std::size_t index = 0; index < shadowLightCount; ++index)
    {
        PointLight& pointLight = pointLights_[index];
        if (!pointLight.castsShadow)
        {
            continue;
        }

        glGenTextures(1, &pointLight.shadowCubeMap);
        glBindTexture(GL_TEXTURE_CUBE_MAP, pointLight.shadowCubeMap);
        for (int face = 0; face < 6; ++face)
        {
            glTexImage2D(
                GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
                0,
                GL_DEPTH_COMPONENT,
                pointShadowResolution_,
                pointShadowResolution_,
                0,
                GL_DEPTH_COMPONENT,
                GL_FLOAT,
                nullptr);
        }
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        allocatedCount += 1u;
    }
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    DebugLog::Info(
        "BaseScene",
        "Point shadow cube maps allocated=", allocatedCount,
        " resolution=", pointShadowResolution_,
        " textureUnitOffset=", kPointShadowTextureUnitOffset);
}

void BaseScene::ReleasePointLightShadowMaps()
{
    for (PointLight& pointLight : pointLights_)
    {
        if (pointLight.shadowCubeMap != 0)
        {
            glDeleteTextures(1, &pointLight.shadowCubeMap);
            pointLight.shadowCubeMap = 0;
        }
    }
}

void BaseScene::RenderShadowMap(const glm::mat4& lightSpaceMatrix) const
{
    if (shadowMapFramebuffer_ == 0 || shadowMapTexture_ == 0 || shadowDepthShader_ == nullptr)
    {
        return;
    }

    GLint previousViewport[4] { 0, 0, 0, 0 };
    glGetIntegerv(GL_VIEWPORT, previousViewport);

    glViewport(0, 0, shadowMapResolution_, shadowMapResolution_);
    glBindFramebuffer(GL_FRAMEBUFFER, shadowMapFramebuffer_);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(2.0f, 4.0f);

    shadowDepthShader_->Use();
    shadowDepthShader_->SetMat4("lightSpaceMatrix", lightSpaceMatrix);
    DrawShadowCasters(*shadowDepthShader_);

    glDisable(GL_POLYGON_OFFSET_FILL);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
}

void BaseScene::DrawShadowCasters(const ShaderProgram& shader) const
{
    for (const SceneEntity& entity : entities_)
    {
        if (entity.placement.model == nullptr)
        {
            continue;
        }

        shader.SetBool("useTexture", false);
        shader.SetMat4("model", BuildStaticModelMatrix(entity));
        entity.placement.model->DrawWithoutTextures();
    }

    for (const InteractiveDoor& door : doors_)
    {
        if (door.placement.model == nullptr)
        {
            continue;
        }

        shader.SetBool("useTexture", false);
        shader.SetMat4("model", BuildDoorModelMatrix(door));
        door.placement.model->DrawWithoutTextures();
    }

    for (const CarryableObject& object : carryableObjects_)
    {
        if (object.placement.model == nullptr || !object.visible || object.pickedUp || object.discarded)
        {
            continue;
        }

        shader.SetBool("useTexture", false);
        shader.SetMat4("model", BuildCarryableModelMatrix(object));
        object.placement.model->DrawWithoutTextures();
    }
}

void BaseScene::DrawLitGeometry() const
{
    litShader_->SetBool("useTexture", true);
    litShader_->SetFloat("specularStrength", 0.0f);
    litShader_->SetFloat("shininess", 7.0f);
    litShader_->SetFloat("unlitFactor", 0.0f);
    litShader_->SetBool("proceduralDumpsterMaterial", false);
    litShader_->SetVec3("baseColor", glm::vec3(1.0f));
    litShader_->SetMat4("model", glm::mat4(1.0f));
    litShader_->SetBool("floorDirtEnabled", dirtTexture_ != 0);
    litShader_->SetVec3("dirtPathStart", glm::vec3(0.0f, 0.0f, 22.0f));
    litShader_->SetVec3("dirtPathEnd", glm::vec3(-3.26f, 0.0f, -6.14f));
    litShader_->SetFloat("dirtPathWidth", 1.55f);
    litShader_->SetFloat("dirtPathBlend", 0.55f);
    litShader_->SetFloat("dirtStartRadius", 2.7f);
    litShader_->SetFloat("dirtSineAmplitude", 1.15f);
    litShader_->SetFloat("dirtTextureScale", 0.48f);
    glActiveTexture(GL_TEXTURE0 + kDirtTextureUnit);
    glBindTexture(GL_TEXTURE_2D, dirtTexture_);
    glActiveTexture(GL_TEXTURE0);
    floorMesh_->Draw();
    litShader_->SetBool("floorDirtEnabled", false);
    litShader_->SetFloat("shininess", 24.0f);

    if (boundarySideWallMesh_ != nullptr && boundaryEndWallMesh_ != nullptr)
    {
        litShader_->SetBool("useTexture", true);
        litShader_->SetFloat("specularStrength", 0.0f);
        litShader_->SetFloat("unlitFactor", 1.0f);
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
        litShader_->SetFloat("unlitFactor", 0.0f);
    }

    for (const SceneEntity& entity : entities_)
    {
        if (entity.placement.model == nullptr)
        {
            continue;
        }

        litShader_->SetBool("useTexture", entity.placement.model->HasTextures());
        litShader_->SetFloat("specularStrength", SpecularStrengthForEntity(entity.name));
        litShader_->SetFloat("unlitFactor", 0.0f);
        litShader_->SetVec3("baseColor", glm::vec3(0.92f, 0.86f, 0.72f));
        const std::string lowerEntityName = ToLowerAscii(entity.name);
        litShader_->SetBool(
            "proceduralDumpsterMaterial",
            lowerEntityName.find("dumpster") != std::string::npos
                || lowerEntityName.find("basura") != std::string::npos
                || lowerEntityName.find("trash") != std::string::npos);
        litShader_->SetMat4("model", BuildStaticModelMatrix(entity));
        entity.placement.model->Draw();
        litShader_->SetBool("proceduralDumpsterMaterial", false);
    }

    for (const InteractiveDoor& door : doors_)
    {
        if (door.placement.model == nullptr)
        {
            continue;
        }

        litShader_->SetBool("useTexture", door.placement.model->HasTextures());
        litShader_->SetFloat("specularStrength", 0.03f);
        litShader_->SetFloat("unlitFactor", 0.0f);
        litShader_->SetVec3("baseColor", glm::vec3(0.95f, 0.82f, 0.58f));
        litShader_->SetMat4("model", BuildDoorModelMatrix(door));
        door.placement.model->Draw();
    }

    for (const HouseLight& houseLight : houseLights_)
    {
        if (!houseLight.enabled || !houseLight.active || lightMarkerMesh_ == nullptr)
        {
            continue;
        }

        litShader_->SetBool("useTexture", false);
        litShader_->SetFloat("specularStrength", 0.0f);
        litShader_->SetFloat("unlitFactor", 1.0f);
        litShader_->SetVec3("baseColor", houseLight.color);
        glm::mat4 model = glm::translate(glm::mat4(1.0f), houseLight.position);
        model = glm::scale(model, glm::vec3(houseLight.interactable ? 0.16f : 0.11f));
        litShader_->SetMat4("model", model);
        lightMarkerMesh_->DrawWithoutTextures();
    }
    litShader_->SetFloat("unlitFactor", 0.0f);

    for (const CarryableObject& object : carryableObjects_)
    {
        if (object.placement.model == nullptr || !object.visible || object.pickedUp || object.discarded)
        {
            continue;
        }

        litShader_->SetBool("useTexture", object.placement.model->HasTextures());
        litShader_->SetFloat("specularStrength", 0.05f);
        litShader_->SetFloat("unlitFactor", 0.0f);
        litShader_->SetVec3("baseColor", glm::vec3(0.92f, 0.86f, 0.76f));
        litShader_->SetMat4("model", BuildCarryableModelMatrix(object));
        object.placement.model->Draw();
    }
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

Mesh BaseScene::CreateTexturedWallPlaneMesh(
    GLuint textureId,
    const std::string& textureName,
    const glm::vec2& size,
    float tileWorldSize,
    bool horizontalAxisIsX)
{
    const float halfHorizontal = size.x * 0.5f;
    const float halfHeight = size.y * 0.5f;
    const float uRepeat = std::max(size.x / std::max(tileWorldSize, 0.001f), 1.0f);
    const float vRepeat = std::max(size.y / std::max(tileWorldSize, 0.001f), 1.0f);
    const glm::vec3 normal = horizontalAxisIsX
        ? glm::vec3(0.0f, 0.0f, 1.0f)
        : glm::vec3(1.0f, 0.0f, 0.0f);

    auto makePosition = [&](float horizontal, float vertical)
    {
        return horizontalAxisIsX
            ? glm::vec3(horizontal, vertical, 0.0f)
            : glm::vec3(0.0f, vertical, horizontal);
    };

    std::vector<Vertex> vertices {
        Vertex { makePosition(-halfHorizontal, -halfHeight), normal, glm::vec2(uRepeat, vRepeat) },
        Vertex { makePosition( halfHorizontal, -halfHeight), normal, glm::vec2(0.0f, vRepeat) },
        Vertex { makePosition( halfHorizontal,  halfHeight), normal, glm::vec2(0.0f, 0.0f) },
        Vertex { makePosition(-halfHorizontal,  halfHeight), normal, glm::vec2(uRepeat, 0.0f) }
    };
    std::vector<unsigned int> indices {
        0u, 1u, 2u,
        0u, 2u, 3u
    };
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

Mesh BaseScene::CreateSphereMesh(int latitudeSegments, int longitudeSegments)
{
    const int latSegments = std::max(latitudeSegments, 3);
    const int lonSegments = std::max(longitudeSegments, 4);

    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
    vertices.reserve(static_cast<std::size_t>((latSegments + 1) * (lonSegments + 1)));
    indices.reserve(static_cast<std::size_t>(latSegments * lonSegments * 6));

    for (int lat = 0; lat <= latSegments; ++lat)
    {
        const float v = static_cast<float>(lat) / static_cast<float>(latSegments);
        const float phi = v * glm::pi<float>();
        const float y = std::cos(phi);
        const float radius = std::sin(phi);

        for (int lon = 0; lon <= lonSegments; ++lon)
        {
            const float u = static_cast<float>(lon) / static_cast<float>(lonSegments);
            const float theta = u * glm::two_pi<float>();
            const glm::vec3 normal(
                radius * std::cos(theta),
                y,
                radius * std::sin(theta));

            Vertex vertex {};
            vertex.position = normal * 0.5f;
            vertex.normal = glm::normalize(normal);
            vertex.texCoords = glm::vec2(u, v);
            vertices.push_back(vertex);
        }
    }

    const int stride = lonSegments + 1;
    for (int lat = 0; lat < latSegments; ++lat)
    {
        for (int lon = 0; lon < lonSegments; ++lon)
        {
            const unsigned int topLeft = static_cast<unsigned int>((lat * stride) + lon);
            const unsigned int bottomLeft = topLeft + static_cast<unsigned int>(stride);
            const unsigned int topRight = topLeft + 1u;
            const unsigned int bottomRight = bottomLeft + 1u;

            if (lat > 0)
            {
                indices.insert(indices.end(), { topLeft, bottomLeft, topRight });
            }
            if (lat < latSegments - 1)
            {
                indices.insert(indices.end(), { topRight, bottomLeft, bottomRight });
            }
        }
    }

    return Mesh(std::move(vertices), std::move(indices), {});
}
