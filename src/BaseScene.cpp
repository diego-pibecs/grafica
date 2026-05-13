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

constexpr int kMaxPointLights = 12;
constexpr int kPointShadowTextureUnitOffset = 2;
constexpr int kDirtTextureUnit = kPointShadowTextureUnitOffset + kMaxPointLights;
constexpr bool kSignTextEnabled = false;

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
    float maxDistance)
{
    if (glm::dot(rayDirection, rayDirection) < 0.000001f)
    {
        return false;
    }

    const glm::vec3 direction = glm::normalize(rayDirection);
    const glm::vec3 toCenter = center - rayOrigin;
    const float projectedDistance = glm::dot(toCenter, direction);
    if (projectedDistance < 0.0f || projectedDistance > maxDistance)
    {
        return false;
    }

    const glm::vec3 closestPoint = rayOrigin + (direction * projectedDistance);
    return glm::length(closestPoint - center) <= radius;
}

std::string NormalizeNodeLookupKey(const std::string& value)
{
    std::string normalized;
    normalized.reserve(value.size());
    for (const unsigned char character : value)
    {
        if (std::isalnum(character) != 0)
        {
            normalized.push_back(static_cast<char>(std::tolower(character)));
        }
    }
    return normalized;
}

using SignGlyph = std::array<const char*, 7>;

SignGlyph GlyphForSignText(char rawCharacter)
{
    const char character = static_cast<char>(std::toupper(static_cast<unsigned char>(rawCharacter)));
    switch (character)
    {
        case 'A': return { "01110", "10001", "10001", "11111", "10001", "10001", "10001" };
        case 'D': return { "11110", "10001", "10001", "10001", "10001", "10001", "11110" };
        case 'E': return { "11111", "10000", "10000", "11110", "10000", "10000", "11111" };
        case 'I': return { "11111", "00100", "00100", "00100", "00100", "00100", "11111" };
        case 'L': return { "10000", "10000", "10000", "10000", "10000", "10000", "11111" };
        case 'M': return { "10001", "11011", "10101", "10101", "10001", "10001", "10001" };
        case 'O': return { "01110", "10001", "10001", "10001", "10001", "10001", "01110" };
        case 'Q': return { "01110", "10001", "10001", "10001", "10101", "10010", "01101" };
        case 'R': return { "11110", "10001", "10001", "11110", "10100", "10010", "10001" };
        case 'T': return { "11111", "00100", "00100", "00100", "00100", "00100", "00100" };
        case 'U': return { "10001", "10001", "10001", "10001", "10001", "10001", "01110" };
        case 'X': return { "10001", "10001", "01010", "00100", "01010", "10001", "10001" };
        default: return { "00000", "00000", "00000", "00000", "00000", "00000", "00000" };
    }
}

GLuint CreateSignTextTexture(const std::string& text)
{
    constexpr int kGlyphWidth = 5;
    constexpr int kGlyphHeight = 7;
    constexpr int kGlyphGap = 1;
    constexpr int kLineGap = 2;
    constexpr int kPixelScale = 8;
    constexpr int kPadding = 8;

    std::vector<std::string> lines;
    lines.emplace_back();
    for (char character : text)
    {
        if (character == '\n')
        {
            lines.emplace_back();
            continue;
        }

        lines.back().push_back(character);
    }

    int maxGlyphCount = 1;
    for (const std::string& line : lines)
    {
        maxGlyphCount = std::max(maxGlyphCount, static_cast<int>(line.size()));
    }

    const int lineCount = static_cast<int>(lines.size());
    const int textWidth = ((maxGlyphCount * kGlyphWidth) + ((maxGlyphCount - 1) * kGlyphGap)) * kPixelScale;
    const int textHeight = ((lineCount * kGlyphHeight) + ((lineCount - 1) * kLineGap)) * kPixelScale;
    const int width = textWidth + (kPadding * 2);
    const int height = textHeight + (kPadding * 2);
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width * height * 4), 0u);

    auto setPixel = [&](int x, int y, unsigned char red, unsigned char green, unsigned char blue, unsigned char alpha)
    {
        if (x < 0 || y < 0 || x >= width || y >= height)
        {
            return;
        }

        const std::size_t offset = static_cast<std::size_t>(((y * width) + x) * 4);
        pixels[offset + 0u] = red;
        pixels[offset + 1u] = green;
        pixels[offset + 2u] = blue;
        pixels[offset + 3u] = alpha;
    };

    for (int lineIndex = 0; lineIndex < lineCount; ++lineIndex)
    {
        const std::string& line = lines[static_cast<std::size_t>(lineIndex)];
        const int glyphCount = static_cast<int>(std::max<std::size_t>(1u, line.size()));
        const int linePixelWidth = ((glyphCount * kGlyphWidth) + ((glyphCount - 1) * kGlyphGap)) * kPixelScale;
        const int lineOffsetX = kPadding + ((textWidth - linePixelWidth) / 2);
        const int lineOffsetY = kPadding + (lineIndex * (kGlyphHeight + kLineGap) * kPixelScale);

        for (int charIndex = 0; charIndex < glyphCount; ++charIndex)
        {
            const char character = charIndex < static_cast<int>(line.size()) ? line[static_cast<std::size_t>(charIndex)] : ' ';
            const SignGlyph glyph = GlyphForSignText(character);
            const int glyphX = lineOffsetX + (charIndex * (kGlyphWidth + kGlyphGap) * kPixelScale);

            for (int row = 0; row < kGlyphHeight; ++row)
            {
                for (int column = 0; column < kGlyphWidth; ++column)
                {
                    if (glyph[static_cast<std::size_t>(row)][column] != '1')
                    {
                        continue;
                    }

                    for (int py = 0; py < kPixelScale; ++py)
                    {
                        for (int px = 0; px < kPixelScale; ++px)
                        {
                            setPixel(
                                glyphX + (column * kPixelScale) + px,
                                lineOffsetY + (row * kPixelScale) + py,
                                28u,
                                18u,
                                10u,
                                255u);
                        }
                    }
                }
            }
        }
    }

    return CreateTexture2DFromRgbaPixels(width, height, pixels);
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
    const GLuint fenceTexture = LoadTexture2D(assetsRoot_ / "textures" / "fence-texture.png");
    dirtTexture_ = LoadTexture2D(assetsRoot_ / "textures" / "dirt" / "dirt.png");
    const float wallLengthZ = (zoneOneBoundsMax_.z - zoneOneBoundsMin_.z) + (boundaryWallThickness_ * 2.0f);
    const float wallLengthX = (zoneOneBoundsMax_.x - zoneOneBoundsMin_.x) + (boundaryWallThickness_ * 2.0f);
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
    cubeMesh_ = std::make_unique<Mesh>(CreateCubeMesh());
    skyboxMesh_ = std::make_unique<Mesh>(CreateCubeMesh());
    if (kSignTextEnabled)
    {
        signTextMesh_ = std::make_unique<Mesh>(CreateTexturedWallPlaneMesh(
            CreateSignTextTexture("MIRA\nALREDEDOR"),
            "sign-text-placeholder",
            glm::vec2(1.32f, 0.44f),
            10.0f,
            true));
    }

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
    latestPlayer_ = player;
    absoluteTimeSeconds_ = absoluteTimeSeconds;

    const float dt = std::clamp(deltaTimeSeconds, 0.0f, 0.1f);
    if (kirbyKeyframeActive_ && absoluteTimeSeconds_ - kirbyKeyframeStartTime_ > 2.20f)
    {
        kirbyKeyframeActive_ = false;
    }
    if (portalKeyframeActive_)
    {
        const float elapsed = absoluteTimeSeconds_ - portalKeyframeStartTime_;
        if (elapsed >= 0.95f && !portalTeleportConsumed_)
        {
            pendingTeleportPosition_ = portalTargetPosition_;
            pendingTeleportYawDegrees_ = portalTargetYawDegrees_;
            portalTeleportPending_ = true;
            portalTeleportConsumed_ = true;
        }
        if (elapsed > 1.65f)
        {
            portalKeyframeActive_ = false;
            portalOpen_ = true;
        }
    }
    if (appleKeyframeActive_ && absoluteTimeSeconds_ - appleKeyframeStartTime_ > 1.85f)
    {
        appleKeyframeActive_ = false;
    }
    if (kirbyKeyframeActive_
        || portalKeyframeActive_
        || appleKeyframeActive_
        || glm::length(glm::vec2(player.velocity.x, player.velocity.z)) > 0.01f)
    {
        shadowMapDirty_ = true;
    }

    // Zona 2 es un parkour suspendido: si el jugador sale del volumen seguro,
    // la escena lo trata como una caida al vacio y lo devuelve a la zona inicial.
    const bool playerNearZoneTwo = player.position.x > 48.0f;
    const bool outsideZoneTwoVolume = player.position.x < 57.8f
        || player.position.x > 76.8f
        || player.position.z < -24.0f
        || player.position.z > 17.8f
        || player.position.y < -1.0f;
    if (playerNearZoneTwo && outsideZoneTwoVolume && !portalTeleportPending_)
    {
        pendingTeleportPosition_ = playerSpawnPosition_;
        pendingTeleportYawDegrees_ = playerSpawnYawDegrees_;
        portalTeleportPending_ = true;
        DebugLog::Info("BaseScene", "Void reset triggered from zone two");
    }

    for (PointLight& pointLight : pointLights_)
    {
        if (pointLight.label == "whispy-final-star")
        {
            const float pulse = 0.5f + (0.5f * std::sin(absoluteTimeSeconds_ * 3.0f));
            pointLight.intensity = finalZoneActivated_ ? 5.5f + pulse : 1.6f;
            pointLight.range = finalZoneActivated_ ? 12.0f : 7.0f;
        }
    }

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
    const bool traceRenderCall = renderCallCount <= 12u || (renderCallCount % 600u) == 0u;
    const auto renderBegin = std::chrono::steady_clock::now();
    if (traceRenderCall)
    {
        DebugLog::Info("BaseScene", "Render call ", renderCallCount, " begin");
    }

    const glm::vec3 cameraPosition = camera.GetPosition();
    zoneTwoViewThisFrame_ = cameraPosition.x > 48.0f || latestPlayer_.position.x > 48.0f;
    const glm::mat4 view = camera.GetViewMatrix();
    const glm::mat4 lightSpaceMatrix = BuildSunLightSpaceMatrix();
    renderKirbyThisFrame_ = camera.GetMode() == CameraMode::ThirdPerson;

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

    double skyboxMs = 0.0;
    if (!zoneTwoViewThisFrame_)
    {
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
        skyboxMs = MillisecondsSince(skyboxBegin);
    }

    const auto litBegin = std::chrono::steady_clock::now();
    litShader_->Use();
    litShader_->SetMat4("projection", projection);
    litShader_->SetMat4("view", view);
    litShader_->SetMat4("lightSpaceMatrix", lightSpaceMatrix);
    litShader_->SetVec3("viewPos", cameraPosition);
    litShader_->SetVec3("sunDirection", glm::normalize(sunDirection_));
    litShader_->SetVec3("sunColor", sunColor_);
    litShader_->SetVec3("skyAmbientColor", ambientSkyColor_);
    litShader_->SetVec3("groundAmbientColor", ambientGroundColor_);
    litShader_->SetVec3("sunSuppressionCenter", zoneTwoCenter_);
    litShader_->SetFloat("sunSuppressionRadius", 34.0f);
    litShader_->SetFloat("sunSuppressionStrength", 0.92f);
    litShader_->SetFloat("shininess", 24.0f);
    litShader_->SetInt("texture_diffuse1", 0);
    litShader_->SetInt("shadowMap", 1);
    litShader_->SetInt("dirtTexture", kDirtTextureUnit);
    litShader_->SetFloat("pointLightResponse", 1.0f);
    litShader_->SetBool("shadowsEnabled", shadowMapTexture_ != 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, shadowMapTexture_);

    const std::size_t activeLightCount = std::min(pointLights_.size(), static_cast<std::size_t>(kMaxPointLights));
    if (pointLights_.size() > static_cast<std::size_t>(kMaxPointLights) && traceRenderCall)
    {
        DebugLog::Info("BaseScene", "Point light count exceeds shader cap: ", pointLights_.size(), " > ", kMaxPointLights);
    }

    const int pointLightCount = static_cast<int>(activeLightCount);
    bool pointShadowsAvailable = false;
    for (std::size_t index = 0; index < activeLightCount; ++index)
    {
        const PointLight& pointLight = pointLights_[index];
        pointShadowsAvailable = pointShadowsAvailable || (pointLight.castsShadow && pointLight.shadowCubeMap != 0);
    }
    litShader_->SetInt("pointLightCount", pointLightCount);
    litShader_->SetBool("pointShadowsEnabled", pointShadowsAvailable);
    for (int index = 0; index < kMaxPointLights; ++index)
    {
        const std::string prefix = "pointLights[" + std::to_string(index) + "]";
        const std::string shadowPrefix = "pointShadowMaps[" + std::to_string(index) + "]";
        litShader_->SetInt(shadowPrefix, kPointShadowTextureUnitOffset + index);
        if (index < pointLightCount)
        {
            const PointLight& pointLight = pointLights_[static_cast<std::size_t>(index)];
            litShader_->SetVec3(prefix + ".position", pointLight.position);
            litShader_->SetVec3(prefix + ".color", pointLight.color);
            litShader_->SetFloat(prefix + ".intensity", pointLight.intensity);
            litShader_->SetFloat(prefix + ".range", pointLight.range);
            litShader_->SetFloat(prefix + ".shadowStrength", pointLight.castsShadow ? 0.50f : 0.0f);
            glActiveTexture(GL_TEXTURE0 + kPointShadowTextureUnitOffset + index);
            glBindTexture(GL_TEXTURE_CUBE_MAP, pointLight.shadowCubeMap != 0 ? pointLight.shadowCubeMap : fallbackPointShadowCubeMap_);
        }
        else
        {
            litShader_->SetVec3(prefix + ".position", glm::vec3(0.0f));
            litShader_->SetVec3(prefix + ".color", glm::vec3(0.0f));
            litShader_->SetFloat(prefix + ".intensity", 0.0f);
            litShader_->SetFloat(prefix + ".range", 1.0f);
            litShader_->SetFloat(prefix + ".shadowStrength", 0.0f);
            glActiveTexture(GL_TEXTURE0 + kPointShadowTextureUnitOffset + index);
            glBindTexture(GL_TEXTURE_CUBE_MAP, fallbackPointShadowCubeMap_);
        }
    }

    DrawLitGeometry();
    glActiveTexture(GL_TEXTURE0 + kDirtTextureUnit);
    glBindTexture(GL_TEXTURE_2D, 0);
    for (int index = 0; index < kMaxPointLights; ++index)
    {
        glActiveTexture(GL_TEXTURE0 + kPointShadowTextureUnitOffset + index);
        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    }
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    const double litMs = MillisecondsSince(litBegin);

    const auto markerBegin = std::chrono::steady_clock::now();
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
    physicsDebugFrame_ = std::move(frame);
}

void BaseScene::SetPhysicsDebugEnabled(bool enabled)
{
    physicsDebugEnabled_ = enabled;
}

bool BaseScene::TryInteract(const glm::vec3& rayOrigin, const glm::vec3& rayDirection, const glm::vec3& playerPosition)
{
    if (RayIntersectsSphere(rayOrigin, rayDirection, portalPropPosition_, 1.15f, 5.2f))
    {
        if (!portalKeyframeActive_ && !portalTeleportPending_)
        {
            portalKeyframeActive_ = true;
            portalOpen_ = false;
            portalTeleportPending_ = false;
            portalTeleportConsumed_ = false;
            portalKeyframeStartTime_ = absoluteTimeSeconds_;
            shadowMapDirty_ = true;
            DebugLog::Info("BaseScene", "Portal interaction accepted");
        }
        return true;
    }

    const float distanceToFinalZone = glm::length(glm::vec2(playerPosition.x, playerPosition.z) - glm::vec2(whispyPosition_.x, whispyPosition_.z));
    const bool whispyTargeted = RayIntersectsSphere(rayOrigin, rayDirection, whispyPosition_ + glm::vec3(0.0f, 2.8f, 0.0f), 3.4f, 12.0f);
    if (distanceToFinalZone > 9.0f || !whispyTargeted)
    {
        DebugLog::Info(
            "BaseScene",
            "Interact requested outside target distance=", distanceToFinalZone,
            " whispyTargeted=", whispyTargeted);
        return false;
    }

    finalZoneActivated_ = !finalZoneActivated_;
    finalActivationTime_ = absoluteTimeSeconds_;
    appleKeyframeActive_ = true;
    appleKeyframeStartTime_ = absoluteTimeSeconds_;
    shadowMapDirty_ = true;
    pointShadowMapsDirty_ = true;
    DebugLog::Info(
        "BaseScene",
        "Whispy final-zone interaction activated=", finalZoneActivated_,
        " distance=", distanceToFinalZone);
    return true;
}

bool BaseScene::ConsumePendingTeleport(glm::vec3& position, float& yawDegrees)
{
    if (!portalTeleportPending_)
    {
        return false;
    }

    portalTeleportPending_ = false;
    position = pendingTeleportPosition_;
    yawDegrees = pendingTeleportYawDegrees_;
    DebugLog::Info(
        "BaseScene",
        "Portal teleport consumed target=(",
        position.x, ", ", position.y, ", ", position.z,
        ") yaw=", yawDegrees);
    return true;
}

void BaseScene::TriggerKeyframeAnimation()
{
    appleKeyframeActive_ = true;
    appleKeyframeStartTime_ = absoluteTimeSeconds_;
    shadowMapDirty_ = true;
}

void BaseScene::TriggerKirbyEntranceAnimation()
{
    kirbyKeyframeActive_ = true;
    kirbyKeyframeStartTime_ = absoluteTimeSeconds_;
}

void BaseScene::ToggleWhispyVariant()
{
    if (whispyFbxPreviewEnabled_)
    {
        whispyFbxPreviewEnabled_ = false;
        DebugLog::Info("BaseScene", "Whispy procedural variant enabled");
        return;
    }

    if (!whispyFbxLoadAttempted_)
    {
        whispyFbxLoadAttempted_ = true;
        const fs::path whispyPath = assetsRoot_ / "models" / "whispy-woods" / "Untitled.fbx";
        try
        {
            const std::uintmax_t fileSize = fs::exists(whispyPath) ? fs::file_size(whispyPath) : 0u;
            constexpr std::uintmax_t kMaxInteractivePreviewBytes = 40ull * 1024ull * 1024ull;
            if (fileSize > kMaxInteractivePreviewBytes)
            {
                whispyFbxLoadFailed_ = true;
                DebugLog::Info(
                    "BaseScene",
                    "Whispy FBX preview skipped because asset is too heavy bytes=", fileSize,
                    " limit=", kMaxInteractivePreviewBytes);
            }
            else
            {
                SceneEntity whispyPreview;
                whispyPreview.name = "whispy-fbx-preview";
                whispyPreview.worldPosition = whispyPosition_;
                whispyPreview.worldYawDegrees = 180.0f;
                SetupPlacement(
                    whispyPreview,
                    whispyPath,
                    7.8f,
                    0.0f,
                    true,
                    true);
                whispyFbxPlacement_ = std::move(whispyPreview.placement);
                whispyFbxLoadFailed_ = whispyFbxPlacement_.model == nullptr;
            }
        }
        catch (const std::exception& error)
        {
            whispyFbxLoadFailed_ = true;
            DebugLog::Error("BaseScene", "Whispy FBX preview load failed: ", error.what());
        }
    }

    whispyFbxPreviewEnabled_ = whispyFbxPlacement_.model != nullptr && !whispyFbxLoadFailed_;
    DebugLog::Info(
        "BaseScene",
        "Whispy preview enabled=", whispyFbxPreviewEnabled_,
        " loadFailed=", whispyFbxLoadFailed_);
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
    region.name = "zone-one-floor-zone-two-platforms";
    region.regionId = "p1-walkable-layout";
    region.categoryBits = CollisionLayers::WorldStatic;
    region.maskBits = CollisionLayers::Actor | CollisionLayers::Dynamic | CollisionLayers::Query;
    region.vertices = {
        glm::vec3(zoneOneBoundsMin_.x, 0.0f, zoneOneBoundsMin_.z),
        glm::vec3(zoneOneBoundsMax_.x, 0.0f, zoneOneBoundsMin_.z),
        glm::vec3(zoneOneBoundsMax_.x, 0.0f, zoneOneBoundsMax_.z),
        glm::vec3(zoneOneBoundsMin_.x, 0.0f, zoneOneBoundsMax_.z)
    };
    region.indices = { 0, 2, 1, 0, 3, 2 };

    const float wallHalfThickness = boundaryWallThickness_ * 0.5f;
    const float wallHalfHeight = boundaryWallHeight_ * 0.5f;
    const float centerZ = (zoneOneBoundsMin_.z + zoneOneBoundsMax_.z) * 0.5f;
    const float centerX = (zoneOneBoundsMin_.x + zoneOneBoundsMax_.x) * 0.5f;
    const float wallLengthZ = (zoneOneBoundsMax_.z - zoneOneBoundsMin_.z) + (boundaryWallThickness_ * 2.0f);
    const float wallLengthX = (zoneOneBoundsMax_.x - zoneOneBoundsMin_.x) + (boundaryWallThickness_ * 2.0f);
    AppendBoxGeometry(
        region,
        glm::vec3(zoneOneBoundsMin_.x - wallHalfThickness, wallHalfHeight, centerZ),
        glm::vec3(wallHalfThickness, wallHalfHeight, wallLengthZ * 0.5f));
    AppendBoxGeometry(
        region,
        glm::vec3(zoneOneBoundsMax_.x + wallHalfThickness, wallHalfHeight, centerZ),
        glm::vec3(wallHalfThickness, wallHalfHeight, wallLengthZ * 0.5f));
    AppendBoxGeometry(
        region,
        glm::vec3(centerX, wallHalfHeight, zoneOneBoundsMin_.z - wallHalfThickness),
        glm::vec3(wallLengthX * 0.5f, wallHalfHeight, wallHalfThickness));
    AppendBoxGeometry(
        region,
        glm::vec3(centerX, wallHalfHeight, zoneOneBoundsMax_.z + wallHalfThickness),
        glm::vec3(wallLengthX * 0.5f, wallHalfHeight, wallHalfThickness));

    for (const PrimitivePlatform& platform : platforms_)
    {
        AppendBoxGeometry(region, platform.center, platform.size * 0.5f);
    }

    region.bounds = ComputeBounds(region.vertices);
    region.bounds.min.y = std::min(region.bounds.min.y, -0.02f);
    region.contributesToCharacterQueries = false;
    return region;
}

std::vector<WalkableBlocker> BaseScene::BuildWalkableBlockers() const
{
    std::vector<WalkableBlocker> blockers;
    blockers.reserve(doors_.size() + sceneBlockers_.size());
    blockers.insert(blockers.end(), sceneBlockers_.begin(), sceneBlockers_.end());
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
    sceneBlockers_.clear();
    platforms_.clear();
    staticCollisionSources_.clear();
    kirbyPlacement_ = ModelPlacement {};
    whispyFbxPlacement_ = ModelPlacement {};
    whispyFbxPreviewEnabled_ = false;
    whispyFbxLoadAttempted_ = false;
    whispyFbxLoadFailed_ = false;
    LoadVegetableValleyDemo();
}

void BaseScene::LoadVegetableValleyDemo()
{
    DebugLog::ScopedTrace trace("BaseScene", "LoadVegetableValleyDemo");
    playerSpawnPosition_ = glm::vec3(0.0f, 0.0f, 16.0f);
    playerSpawnYawDegrees_ = -90.0f;
    zoneTwoCenter_ = glm::vec3(70.0f, 2.0f, -4.0f);
    whispyPosition_ = glm::vec3(70.0f, 3.50f, -20.0f);
    portalPosition_ = glm::vec3(0.0f, 1.25f, -15.5f);
    portalPropPosition_ = glm::vec3(0.0f, 1.0f, -13.4f);
    portalTargetPosition_ = glm::vec3(62.0f, 0.25f, 14.0f);
    portalTargetYawDegrees_ = -90.0f;
    finalStarBasePosition_ = whispyPosition_ + glm::vec3(0.0f, 1.4f, 4.0f);
    activeModelLabel_ = "Kirby Vegetable Valley P1";

    AddPrimitivePlatform("zone-two-start-platform", glm::vec3(62.0f, 0.10f, 14.0f), glm::vec3(7.5f, 0.20f, 6.0f), glm::vec3(0.20f, 0.25f, 0.44f));
    AddPrimitivePlatform("zone-two-step-a", glm::vec3(65.0f, 0.35f, 8.8f), glm::vec3(4.2f, 0.70f, 3.4f), glm::vec3(0.28f, 0.33f, 0.55f));
    AddPrimitivePlatform("zone-two-step-b", glm::vec3(68.5f, 0.65f, 3.8f), glm::vec3(4.0f, 1.30f, 3.2f), glm::vec3(0.26f, 0.30f, 0.52f));
    AddPrimitivePlatform("zone-two-step-c", glm::vec3(65.0f, 1.00f, -1.8f), glm::vec3(4.0f, 2.00f, 3.2f), glm::vec3(0.24f, 0.28f, 0.48f));
    AddPrimitivePlatform("zone-two-step-d", glm::vec3(70.0f, 1.35f, -7.2f), glm::vec3(4.3f, 2.70f, 3.0f), glm::vec3(0.23f, 0.26f, 0.46f));
    AddPrimitivePlatform("zone-two-step-e", glm::vec3(66.8f, 1.55f, -11.8f), glm::vec3(4.2f, 3.10f, 3.0f), glm::vec3(0.24f, 0.27f, 0.47f));
    AddPrimitivePlatform("zone-two-summit", glm::vec3(70.0f, 1.75f, -18.0f), glm::vec3(11.0f, 3.50f, 7.5f), glm::vec3(0.20f, 0.24f, 0.40f));

    SceneEntity kirbyEntity;
    kirbyEntity.name = "kirby-player";
    kirbyEntity.worldPosition = playerSpawnPosition_;
    kirbyEntity.worldYawDegrees = playerSpawnYawDegrees_;
    SetupPlacement(
        kirbyEntity,
        assetsRoot_ / "models" / "kirby" / "source" / "KirbyForSketchfab.fbx",
        1.25f,
        180.0f,
        true,
        true);
    kirbyPlacement_ = std::move(kirbyEntity.placement);

    whispyFbxLoadAttempted_ = true;
    SceneEntity whispyEntity;
    whispyEntity.name = "whispy-woods";
    whispyEntity.worldPosition = whispyPosition_;
    whispyEntity.worldYawDegrees = 180.0f;
    SetupPlacement(
        whispyEntity,
        assetsRoot_ / "models" / "whispy-woods" / "Untitled.fbx",
        7.8f,
        0.0f,
        true,
        true);
    if (whispyEntity.placement.model != nullptr)
    {
        whispyFbxPlacement_ = std::move(whispyEntity.placement);
        whispyFbxPreviewEnabled_ = true;
        whispyFbxLoadFailed_ = false;
    }
    else
    {
        whispyFbxPreviewEnabled_ = false;
        whispyFbxLoadFailed_ = true;
    }

    struct DecorationDesc
    {
        const char* name;
        fs::path path;
        glm::vec3 position;
        float yawDegrees;
        float targetSize;
        bool normalizeToHeight;
        bool contributesToCollision;
    };

    const std::vector<DecorationDesc> decorations {
        { "start-tree-left", assetsRoot_ / "models" / "tree.fbx", glm::vec3(-9.8f, 0.0f, 16.2f), 24.0f, 6.2f, true, true },
        { "start-tree-right", assetsRoot_ / "models" / "tree.fbx", glm::vec3(9.8f, 0.0f, 14.0f), -18.0f, 6.4f, true, true },
        { "section-one-bush-left", assetsRoot_ / "models" / "bushes" / "BushWithBerrys03.fbx", glm::vec3(-7.2f, -0.35f, 8.6f), 12.0f, 1.8f, true, false },
        { "section-one-bush-right", assetsRoot_ / "models" / "bushes" / "Bush02.fbx", glm::vec3(7.3f, -0.35f, 6.0f), -22.0f, 1.7f, true, false },
        { "valley-tree-left", assetsRoot_ / "models" / "tree.fbx", glm::vec3(-10.3f, 0.0f, 1.0f), 61.0f, 6.8f, true, true },
        { "valley-tree-right", assetsRoot_ / "models" / "tree.fbx", glm::vec3(10.2f, 0.0f, -4.0f), -42.0f, 6.8f, true, true },
        { "mid-rock-left", assetsRoot_ / "models" / "rocks" / "SM_Rocks_02.fbx", glm::vec3(-5.6f, 0.0f, -6.0f), 35.0f, 1.5f, false, false },
        { "mid-rock-right", assetsRoot_ / "models" / "rocks" / "SM_Rocks_04.fbx", glm::vec3(5.7f, 0.0f, -9.0f), -30.0f, 1.6f, false, false },
        { "parkour-crate-a", assetsRoot_ / "models" / "nuevos" / "crate" / "scene.gltf", glm::vec3(62.0f, 0.23f, 13.8f), 9.0f, 1.05f, false, false },
        { "parkour-crate-b", assetsRoot_ / "models" / "nuevos" / "crate" / "scene.gltf", glm::vec3(65.1f, 0.78f, 8.6f), -14.0f, 1.10f, false, false },
        { "parkour-crate-c", assetsRoot_ / "models" / "nuevos" / "crate" / "scene.gltf", glm::vec3(68.6f, 1.38f, 3.7f), 22.0f, 1.10f, false, false },
        { "parkour-crate-d", assetsRoot_ / "models" / "nuevos" / "crate" / "scene.gltf", glm::vec3(65.1f, 2.08f, -1.9f), -32.0f, 1.12f, false, false },
        { "parkour-crate-e", assetsRoot_ / "models" / "nuevos" / "crate" / "scene.gltf", glm::vec3(70.2f, 2.78f, -7.2f), 18.0f, 1.15f, false, false },
        { "parkour-crate-f", assetsRoot_ / "models" / "nuevos" / "crate" / "scene.gltf", glm::vec3(66.8f, 3.18f, -11.8f), -18.0f, 1.12f, false, false },
        { "parkour-star", assetsRoot_ / "models" / "nuevos" / "star.obj", glm::vec3(70.0f, 4.10f, -13.8f), 36.0f, 0.8f, false, false },
        { "flower-left", assetsRoot_ / "models" / "flower" / "source" / "x3.fbx", glm::vec3(-4.6f, 0.0f, -12.0f), 10.0f, 1.0f, true, false },
        { "flower-right", assetsRoot_ / "models" / "flower" / "source" / "x3.fbx", glm::vec3(4.4f, 0.0f, -14.0f), -16.0f, 1.0f, true, false },
        { "final-tree-left", assetsRoot_ / "models" / "tree.fbx", glm::vec3(64.2f, 3.50f, -21.6f), 14.0f, 7.2f, true, true },
        { "final-tree-right", assetsRoot_ / "models" / "tree.fbx", glm::vec3(76.0f, 3.50f, -21.8f), -39.0f, 7.0f, true, true },
        { "wooden-sign-controls", assetsRoot_ / "models" / "wooden-sign" / "placa.fbx", glm::vec3(-2.8f, 0.0f, 15.5f), 28.0f, 1.3f, true, false },
        { "portal-dummy-star", assetsRoot_ / "models" / "nuevos" / "star.obj", portalPropPosition_, 0.0f, 0.9f, false, false },
        { "zone-two-guide-star", assetsRoot_ / "models" / "nuevos" / "star.obj", glm::vec3(65.0f, 2.0f, 7.8f), 20.0f, 0.75f, false, false }
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
            true,
            decoration.contributesToCollision);
    }

    AddSceneBlocker("bush-left", glm::vec3(-7.2f, 0.55f, 8.6f), glm::vec3(0.95f, 0.65f, 0.95f), 12.0f);
    AddSceneBlocker("bush-right", glm::vec3(7.3f, 0.55f, 6.0f), glm::vec3(0.85f, 0.65f, 0.85f), -22.0f);
    AddSceneBlocker("rock-left", glm::vec3(-5.6f, 0.45f, -6.0f), glm::vec3(0.80f, 0.55f, 0.70f), 35.0f);
    AddSceneBlocker("rock-right", glm::vec3(5.7f, 0.45f, -9.0f), glm::vec3(0.85f, 0.55f, 0.70f), -30.0f);
    AddSceneBlocker("wooden-sign", glm::vec3(-2.8f, 0.75f, 15.5f), glm::vec3(0.65f, 0.85f, 0.35f), 28.0f);
    AddSceneBlocker("whispy-trunk", whispyPosition_ + glm::vec3(0.0f, 1.55f, 0.0f), glm::vec3(1.95f, 1.95f, 1.85f), 0.0f);

    DebugLog::Info(
        "BaseScene",
        "Vegetable Valley demo loaded visualEntities=", entities_.size(),
        " collisionSources=", staticCollisionSources_.size(),
        " dynamicBlockers=", sceneBlockers_.size(),
        " kirbyLoaded=", kirbyPlacement_.model != nullptr);
}

void BaseScene::ConfigureSceneLights()
{
    ReleasePointLightShadowMaps();
    pointLights_.clear();

    auto addPointLight = [&](const std::string& label, const glm::vec3& position, const glm::vec3& color, float intensity, float range)
    {
        PointLight pointLight;
        pointLight.label = label;
        pointLight.position = position;
        pointLight.color = color;
        pointLight.intensity = intensity;
        pointLight.range = range;
        pointLights_.push_back(pointLight);
    };

    sunDirection_ = glm::normalize(glm::vec3(-0.34f, -0.72f, -0.60f));
    sunColor_ = glm::vec3(0.88f, 0.82f, 0.66f);
    ambientSkyColor_ = glm::vec3(0.22f, 0.29f, 0.38f);
    ambientGroundColor_ = glm::vec3(0.08f, 0.12f, 0.08f);

    addPointLight("portal-star", portalPropPosition_ + glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(1.0f, 0.86f, 0.38f), 2.4f, 7.0f);
    addPointLight("zone-two-entry", glm::vec3(62.0f, 1.8f, 14.0f), glm::vec3(0.42f, 0.72f, 1.0f), 2.8f, 8.0f);
    addPointLight("zone-two-step-a", glm::vec3(65.0f, 2.2f, 8.8f), glm::vec3(0.60f, 0.88f, 1.0f), 2.3f, 6.5f);
    addPointLight("zone-two-step-b", glm::vec3(68.5f, 2.8f, 3.8f), glm::vec3(0.80f, 0.74f, 1.0f), 2.3f, 6.5f);
    addPointLight("zone-two-summit", glm::vec3(70.0f, 5.1f, -15.5f), glm::vec3(1.0f, 0.76f, 0.35f), 3.0f, 9.0f);
    addPointLight("whispy-final-star", finalStarBasePosition_ + glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(1.0f, 0.72f, 0.26f), 1.8f, 8.0f);

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
        " shadowPointLights=", shadowCastingPointLights,
        " pointShadowResolution=", pointShadowResolution_);
    AllocatePointLightShadowMaps();
    pointShadowMapsDirty_ = true;
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
    bool contributesToCollision)
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
    entity.contributesToCollision = contributesToCollision;
    SetupPlacement(entity, path, targetSize, yawOffsetDegrees, normalizeToHeight, loadTextures);
    if (entity.placement.model == nullptr)
    {
        DebugLog::Error("BaseScene", "Skipping decoration with failed model load ", path.string());
        return;
    }

    if (entity.contributesToCollision)
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

void BaseScene::AddSceneBlocker(
    const std::string& name,
    const glm::vec3& center,
    const glm::vec3& halfExtents,
    float yawDegrees)
{
    WalkableBlocker blocker;
    blocker.name = name;
    blocker.center = center;
    blocker.halfExtents = halfExtents;
    blocker.yawDegrees = yawDegrees;
    blocker.enabled = true;
    sceneBlockers_.push_back(std::move(blocker));
}

void BaseScene::AddPrimitivePlatform(
    const std::string& name,
    const glm::vec3& center,
    const glm::vec3& size,
    const glm::vec3& color)
{
    PrimitivePlatform platform;
    platform.name = name;
    platform.center = center;
    platform.size = size;
    platform.color = color;
    platforms_.push_back(std::move(platform));
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

glm::mat4 BaseScene::BuildKirbyModelMatrix() const
{
    glm::vec3 position = latestPlayer_.position;
    float scaleMultiplier = 1.0f;
    float extraYawDegrees = 0.0f;

    if (kirbyKeyframeActive_)
    {
        const KeyframeSample sample = SampleKirbyEntranceKeyframes(absoluteTimeSeconds_ - kirbyKeyframeStartTime_);
        position += sample.offset;
        scaleMultiplier = sample.scale;
        extraYawDegrees = sample.extraYawDegrees;
    }

    glm::mat4 model = glm::translate(glm::mat4(1.0f), position);
    model = glm::rotate(model, glm::radians(latestPlayer_.facingYawDegrees + kirbyPlacement_.yawOffsetDegrees + extraYawDegrees), glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::scale(model, glm::vec3(kirbyPlacement_.scale * scaleMultiplier));
    model = glm::translate(model, kirbyPlacement_.rawOffset);
    return model;
}

glm::mat4 BaseScene::BuildWhispyFbxModelMatrix() const
{
    glm::mat4 model = glm::translate(glm::mat4(1.0f), whispyPosition_);
    model = glm::rotate(model, glm::radians(360.0f + whispyFbxPlacement_.yawOffsetDegrees), glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::scale(model, glm::vec3(whispyFbxPlacement_.scale));
    model = glm::translate(model, whispyFbxPlacement_.rawOffset);
    return model;
}

BaseScene::KeyframeSample BaseScene::SampleKirbyEntranceKeyframes(float elapsedSeconds) const
{
    // Simple authored keyframe curve: pop in, overshoot, settle.
    const std::array<KeyframeSample, 5> keyframes {
        KeyframeSample { 0.00f, glm::vec3(0.0f, 2.20f, 0.0f), 0.10f, 0.0f },
        KeyframeSample { 0.35f, glm::vec3(0.0f, 1.20f, 0.0f), 1.18f, 160.0f },
        KeyframeSample { 0.75f, glm::vec3(0.0f, 0.18f, 0.0f), 0.88f, 285.0f },
        KeyframeSample { 1.25f, glm::vec3(0.0f, 0.42f, 0.0f), 1.05f, 340.0f },
        KeyframeSample { 1.90f, glm::vec3(0.0f, 0.0f, 0.0f), 1.00f, 360.0f }
    };

    if (elapsedSeconds <= keyframes.front().time)
    {
        return keyframes.front();
    }
    if (elapsedSeconds >= keyframes.back().time)
    {
        return keyframes.back();
    }

    for (std::size_t index = 1; index < keyframes.size(); ++index)
    {
        const KeyframeSample& previous = keyframes[index - 1u];
        const KeyframeSample& next = keyframes[index];
        if (elapsedSeconds > next.time)
        {
            continue;
        }

        float t = (elapsedSeconds - previous.time) / std::max(next.time - previous.time, 0.001f);
        t = t * t * (3.0f - (2.0f * t));
        return KeyframeSample {
            elapsedSeconds,
            glm::mix(previous.offset, next.offset, t),
            glm::mix(previous.scale, next.scale, t),
            glm::mix(previous.extraYawDegrees, next.extraYawDegrees, t)
        };
    }

    return keyframes.back();
}

BaseScene::KeyframeSample BaseScene::SamplePortalKeyframes(float elapsedSeconds) const
{
    const std::array<KeyframeSample, 5> keyframes {
        KeyframeSample { 0.00f, glm::vec3(0.0f), 0.18f, 0.0f },
        KeyframeSample { 0.25f, glm::vec3(0.0f, 0.10f, 0.0f), 0.78f, 90.0f },
        KeyframeSample { 0.65f, glm::vec3(0.0f, 0.18f, 0.0f), 1.18f, 210.0f },
        KeyframeSample { 1.05f, glm::vec3(0.0f), 0.94f, 310.0f },
        KeyframeSample { 1.55f, glm::vec3(0.0f), 1.00f, 360.0f }
    };

    if (elapsedSeconds <= keyframes.front().time)
    {
        return keyframes.front();
    }
    if (elapsedSeconds >= keyframes.back().time)
    {
        return keyframes.back();
    }

    for (std::size_t index = 1; index < keyframes.size(); ++index)
    {
        const KeyframeSample& previous = keyframes[index - 1u];
        const KeyframeSample& next = keyframes[index];
        if (elapsedSeconds > next.time)
        {
            continue;
        }

        float t = (elapsedSeconds - previous.time) / std::max(next.time - previous.time, 0.001f);
        t = t * t * (3.0f - (2.0f * t));
        return KeyframeSample {
            elapsedSeconds,
            glm::mix(previous.offset, next.offset, t),
            glm::mix(previous.scale, next.scale, t),
            glm::mix(previous.extraYawDegrees, next.extraYawDegrees, t)
        };
    }

    return keyframes.back();
}

BaseScene::KeyframeSample BaseScene::SampleAppleFallKeyframes(float elapsedSeconds) const
{
    const std::array<KeyframeSample, 5> keyframes {
        KeyframeSample { 0.00f, glm::vec3(0.65f, 5.60f, 0.42f), 0.42f, 0.0f },
        KeyframeSample { 0.35f, glm::vec3(0.85f, 4.15f, 0.50f), 0.46f, 80.0f },
        KeyframeSample { 0.85f, glm::vec3(0.40f, 2.05f, 0.60f), 0.44f, 210.0f },
        KeyframeSample { 1.25f, glm::vec3(0.75f, 0.55f, 0.70f), 0.48f, 330.0f },
        KeyframeSample { 1.70f, glm::vec3(0.95f, 0.38f, 0.75f), 0.43f, 420.0f }
    };

    if (elapsedSeconds <= keyframes.front().time)
    {
        return keyframes.front();
    }
    if (elapsedSeconds >= keyframes.back().time)
    {
        return keyframes.back();
    }

    for (std::size_t index = 1; index < keyframes.size(); ++index)
    {
        const KeyframeSample& previous = keyframes[index - 1u];
        const KeyframeSample& next = keyframes[index];
        if (elapsedSeconds > next.time)
        {
            continue;
        }

        float t = (elapsedSeconds - previous.time) / std::max(next.time - previous.time, 0.001f);
        t = t * t * (3.0f - (2.0f * t));
        return KeyframeSample {
            elapsedSeconds,
            glm::mix(previous.offset, next.offset, t),
            glm::mix(previous.scale, next.scale, t),
            glm::mix(previous.extraYawDegrees, next.extraYawDegrees, t)
        };
    }

    return keyframes.back();
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

    const glm::mat4 entityTransform = BuildStaticModelMatrix(entity);
    for (const Model::NodeMarker& marker : entity.placement.model->GetNodeMarkers())
    {
        const std::string markerName = NormalizeNodeLookupKey(marker.name);
        const std::string markerPath = NormalizeNodeLookupKey(marker.path);
        if (markerName.find("lightstreetlight") == std::string::npos
            && markerPath.find("lightstreetlight") == std::string::npos)
        {
            continue;
        }

        const glm::vec3 markerPosition = glm::vec3(entityTransform * glm::vec4(marker.position, 1.0f));
        DebugLog::Info(
            "BaseScene",
            "Street light marker found node=", marker.path,
            " hasMesh=", marker.hasMesh,
            " world=(",
            markerPosition.x, ", ", markerPosition.y, ", ", markerPosition.z, ")");
        return markerPosition;
    }

    const glm::vec3 localMin = entity.placement.model->GetMinBounds();
    const glm::vec3 localMax = entity.placement.model->GetMaxBounds();
    const glm::vec3 localCenter = (localMin + localMax) * 0.5f;
    const glm::vec3 localSize = localMax - localMin;
    glm::vec3 localLampPoint(localCenter.x, glm::mix(localMin.y, localMax.y, 0.88f), localCenter.z);
    localLampPoint += glm::vec3(0.0f, 5.0f - (localSize.y * 0.04f), localSize.z * 0.325f);
    const glm::vec3 worldLampPoint = glm::vec3(entityTransform * glm::vec4(localLampPoint, 1.0f));
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

    if (fallbackPointShadowCubeMap_ == 0)
    {
        const float farDepth = 1.0f;
        glGenTextures(1, &fallbackPointShadowCubeMap_);
        glBindTexture(GL_TEXTURE_CUBE_MAP, fallbackPointShadowCubeMap_);
        for (int face = 0; face < 6; ++face)
        {
            glTexImage2D(
                GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
                0,
                GL_DEPTH_COMPONENT,
                1,
                1,
                0,
                GL_DEPTH_COMPONENT,
                GL_FLOAT,
                &farDepth);
        }
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    }

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
    if (fallbackPointShadowCubeMap_ != 0)
    {
        glDeleteTextures(1, &fallbackPointShadowCubeMap_);
        fallbackPointShadowCubeMap_ = 0;
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
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, dirtTexture_);

    for (const SceneEntity& entity : entities_)
    {
        if (entity.placement.model == nullptr)
        {
            continue;
        }
        if (zoneTwoViewThisFrame_ && entity.worldPosition.x < 48.0f)
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

    if (whispyFbxPreviewEnabled_ && whispyFbxPlacement_.model != nullptr)
    {
        shader.SetBool("useTexture", false);
        shader.SetMat4("model", BuildWhispyFbxModelMatrix());
        whispyFbxPlacement_.model->DrawWithoutTextures();
    }

    if (kirbyPlacement_.model != nullptr)
    {
        shader.SetBool("useTexture", false);
        shader.SetMat4("model", BuildKirbyModelMatrix());
        kirbyPlacement_.model->DrawWithoutTextures();
    }

    if (cubeMesh_ != nullptr)
    {
        shader.SetBool("useTexture", false);
        for (const PrimitivePlatform& platform : platforms_)
        {
            const glm::mat4 model = glm::translate(glm::mat4(1.0f), platform.center)
                * glm::scale(glm::mat4(1.0f), platform.size);
            shader.SetMat4("model", model);
            cubeMesh_->DrawWithoutTextures();
        }
    }
}

void BaseScene::DrawColoredMesh(
    const Mesh& mesh,
    const glm::mat4& model,
    const glm::vec3& color,
    float specularStrength,
    float unlitFactor,
    float pointLightResponse) const
{
    litShader_->SetBool("useTexture", false);
    litShader_->SetFloat("specularStrength", specularStrength);
    litShader_->SetFloat("unlitFactor", unlitFactor);
    litShader_->SetFloat("pointLightResponse", pointLightResponse);
    litShader_->SetVec3("baseColor", color);
    litShader_->SetMat4("model", model);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, dirtTexture_);
    mesh.DrawWithoutTextures();
}

void BaseScene::DrawVegetableValleyPrimitives() const
{
    if (lightMarkerMesh_ == nullptr)
    {
        return;
    }

    const float starBob = std::sin(absoluteTimeSeconds_ * 2.4f) * 0.18f;
    const float finalLift = finalZoneActivated_
        ? std::clamp((absoluteTimeSeconds_ - finalActivationTime_) * 1.2f, 0.0f, 1.0f) * 2.0f
        : 0.0f;
    glm::mat4 finalStar = glm::translate(glm::mat4(1.0f), finalStarBasePosition_ + glm::vec3(0.0f, starBob + finalLift, 0.0f));
    finalStar = glm::rotate(finalStar, absoluteTimeSeconds_ * 1.8f, glm::vec3(0.0f, 1.0f, 0.0f));
    finalStar = glm::scale(finalStar, glm::vec3(finalZoneActivated_ ? 1.35f : 0.95f));
    DrawColoredMesh(*lightMarkerMesh_, finalStar, finalZoneActivated_ ? glm::vec3(1.0f, 0.76f, 0.20f) : glm::vec3(1.0f, 0.92f, 0.38f), 0.25f, 0.18f, 1.0f);
}

void BaseScene::DrawWhispyProcedural() const
{
    if (cubeMesh_ == nullptr || lightMarkerMesh_ == nullptr)
    {
        return;
    }

    const float sway = std::sin(absoluteTimeSeconds_ * 1.4f) * 3.0f;
    const glm::mat4 root = glm::translate(glm::mat4(1.0f), whispyPosition_)
        * glm::rotate(glm::mat4(1.0f), glm::radians(sway), glm::vec3(0.0f, 1.0f, 0.0f));

    auto drawPart = [&](const Mesh& mesh, const glm::mat4& local, const glm::vec3& color, float specular = 0.0f)
    {
        DrawColoredMesh(mesh, root * local, color, specular, 0.0f, 0.8f);
    };

    drawPart(*cubeMesh_, glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 2.25f, 0.0f)) * glm::scale(glm::mat4(1.0f), glm::vec3(2.15f, 4.50f, 1.55f)), glm::vec3(0.54f, 0.31f, 0.15f), 0.02f);
    drawPart(*cubeMesh_, glm::translate(glm::mat4(1.0f), glm::vec3(-1.35f, 3.25f, 0.0f)) * glm::rotate(glm::mat4(1.0f), glm::radians(-26.0f), glm::vec3(0.0f, 0.0f, 1.0f)) * glm::scale(glm::mat4(1.0f), glm::vec3(2.3f, 0.34f, 0.34f)), glm::vec3(0.42f, 0.24f, 0.12f), 0.0f);
    drawPart(*cubeMesh_, glm::translate(glm::mat4(1.0f), glm::vec3(1.35f, 3.20f, 0.0f)) * glm::rotate(glm::mat4(1.0f), glm::radians(24.0f), glm::vec3(0.0f, 0.0f, 1.0f)) * glm::scale(glm::mat4(1.0f), glm::vec3(2.3f, 0.34f, 0.34f)), glm::vec3(0.42f, 0.24f, 0.12f), 0.0f);

    const std::array<glm::vec3, 5> crownOffsets {
        glm::vec3(0.0f, 5.25f, 0.0f),
        glm::vec3(-1.35f, 4.85f, 0.05f),
        glm::vec3(1.35f, 4.90f, 0.0f),
        glm::vec3(-0.45f, 6.00f, -0.25f),
        glm::vec3(0.85f, 5.85f, -0.10f)
    };
    for (const glm::vec3& offset : crownOffsets)
    {
        drawPart(*lightMarkerMesh_, glm::translate(glm::mat4(1.0f), offset) * glm::scale(glm::mat4(1.0f), glm::vec3(3.0f)), glm::vec3(0.22f, 0.56f, 0.22f), 0.0f);
    }

    drawPart(*lightMarkerMesh_, glm::translate(glm::mat4(1.0f), glm::vec3(-0.46f, 3.10f, 0.82f)) * glm::scale(glm::mat4(1.0f), glm::vec3(0.23f, 0.32f, 0.10f)), glm::vec3(0.06f, 0.04f, 0.02f), 0.0f);
    drawPart(*lightMarkerMesh_, glm::translate(glm::mat4(1.0f), glm::vec3(0.46f, 3.10f, 0.82f)) * glm::scale(glm::mat4(1.0f), glm::vec3(0.23f, 0.32f, 0.10f)), glm::vec3(0.06f, 0.04f, 0.02f), 0.0f);
    drawPart(*cubeMesh_, glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 2.47f, 0.83f)) * glm::scale(glm::mat4(1.0f), glm::vec3(0.72f, 0.10f, 0.08f)), glm::vec3(0.08f, 0.04f, 0.02f), 0.0f);

    for (int index = 0; index < 4; ++index)
    {
        const float angle = absoluteTimeSeconds_ * 1.8f + static_cast<float>(index) * 1.6f;
        const float bob = std::sin(angle) * (finalZoneActivated_ ? 0.35f : 0.12f);
        const glm::vec3 offset(
            -1.4f + (static_cast<float>(index) * 0.9f),
            4.2f + bob,
            0.85f + (static_cast<float>(index % 2) * 0.18f));
        drawPart(*lightMarkerMesh_, glm::translate(glm::mat4(1.0f), offset) * glm::scale(glm::mat4(1.0f), glm::vec3(0.36f)), glm::vec3(0.88f, 0.14f, 0.08f), 0.05f);
    }
}

void BaseScene::DrawKirby() const
{
    if (!renderKirbyThisFrame_)
    {
        return;
    }

    if (kirbyPlacement_.model != nullptr)
    {
        litShader_->SetBool("useTexture", kirbyPlacement_.model->HasTextures());
        litShader_->SetFloat("specularStrength", 0.04f);
        litShader_->SetFloat("unlitFactor", 0.12f);
        litShader_->SetFloat("pointLightResponse", 0.8f);
        litShader_->SetVec3("baseColor", glm::vec3(1.0f, 0.52f, 0.66f));
        litShader_->SetMat4("model", BuildKirbyModelMatrix());
        if (!kirbyPlacement_.model->HasTextures())
        {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, dirtTexture_);
        }
        kirbyPlacement_.model->Draw();
        return;
    }

    if (lightMarkerMesh_ != nullptr)
    {
        glm::mat4 fallback = glm::translate(glm::mat4(1.0f), latestPlayer_.position + glm::vec3(0.0f, 0.75f, 0.0f));
        fallback = glm::scale(fallback, glm::vec3(1.2f));
        DrawColoredMesh(*lightMarkerMesh_, fallback, glm::vec3(1.0f, 0.45f, 0.62f), 0.05f, 0.12f, 0.8f);
    }
}

void BaseScene::DrawPortalHierarchy() const
{
    if (cubeMesh_ == nullptr || lightMarkerMesh_ == nullptr)
    {
        return;
    }

    KeyframeSample sample;
    sample.scale = portalOpen_ ? 1.0f : 0.42f;
    sample.extraYawDegrees = portalOpen_ ? 360.0f : 0.0f;
    if (portalKeyframeActive_)
    {
        sample = SamplePortalKeyframes(absoluteTimeSeconds_ - portalKeyframeStartTime_);
    }

    const glm::mat4 root = glm::translate(glm::mat4(1.0f), portalPosition_ + sample.offset)
        * glm::rotate(glm::mat4(1.0f), glm::radians(sample.extraYawDegrees), glm::vec3(0.0f, 0.0f, 1.0f))
        * glm::scale(glm::mat4(1.0f), glm::vec3(sample.scale));

    auto drawChild = [&](const glm::mat4& parent, const glm::mat4& local, const Mesh& mesh, const glm::vec3& color, float unlit)
    {
        DrawColoredMesh(mesh, parent * local, color, 0.04f, unlit, 0.9f);
    };

    const glm::vec3 ringColor(0.34f, 0.72f, 1.0f);
    const glm::vec3 runeColor(1.0f, 0.92f, 0.38f);
    const glm::vec3 coreColor(0.52f, 0.34f, 0.92f);

    drawChild(root, glm::translate(glm::mat4(1.0f), glm::vec3(-1.15f, 0.0f, 0.0f)) * glm::scale(glm::mat4(1.0f), glm::vec3(0.16f, 1.55f, 0.16f)), *cubeMesh_, ringColor, 0.18f);
    drawChild(root, glm::translate(glm::mat4(1.0f), glm::vec3(1.15f, 0.0f, 0.0f)) * glm::scale(glm::mat4(1.0f), glm::vec3(0.16f, 1.55f, 0.16f)), *cubeMesh_, ringColor, 0.18f);
    drawChild(root, glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.42f, 0.0f)) * glm::scale(glm::mat4(1.0f), glm::vec3(1.20f, 0.16f, 0.16f)), *cubeMesh_, ringColor, 0.18f);
    drawChild(root, glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -1.42f, 0.0f)) * glm::scale(glm::mat4(1.0f), glm::vec3(1.20f, 0.16f, 0.16f)), *cubeMesh_, ringColor, 0.18f);

    const glm::mat4 core = glm::rotate(glm::mat4(1.0f), absoluteTimeSeconds_ * 0.9f, glm::vec3(0.0f, 0.0f, 1.0f))
        * glm::scale(glm::mat4(1.0f), glm::vec3(0.82f, 1.08f, 0.12f));
    drawChild(root, core, *lightMarkerMesh_, coreColor, 0.28f);

    for (int index = 0; index < 6; ++index)
    {
        const float angle = (static_cast<float>(index) / 6.0f) * glm::two_pi<float>();
        const glm::vec3 runeOffset(std::cos(angle) * 1.18f, std::sin(angle) * 1.43f, 0.12f);
        const glm::mat4 runeParent = root
            * glm::translate(glm::mat4(1.0f), runeOffset)
            * glm::rotate(glm::mat4(1.0f), -angle, glm::vec3(0.0f, 0.0f, 1.0f));
        drawChild(runeParent, glm::scale(glm::mat4(1.0f), glm::vec3(0.16f)), *lightMarkerMesh_, runeColor, 0.25f);

        const float orbit = absoluteTimeSeconds_ * 2.2f + static_cast<float>(index);
        const glm::mat4 childSpark = glm::translate(glm::mat4(1.0f), glm::vec3(std::cos(orbit) * 0.22f, std::sin(orbit) * 0.22f, 0.04f))
            * glm::scale(glm::mat4(1.0f), glm::vec3(0.07f));
        drawChild(runeParent, childSpark, *lightMarkerMesh_, glm::vec3(1.0f, 0.56f, 0.86f), 0.35f);
    }
}

void BaseScene::DrawAppleKeyframe() const
{
    if (!appleKeyframeActive_ || lightMarkerMesh_ == nullptr)
    {
        return;
    }

    const KeyframeSample sample = SampleAppleFallKeyframes(absoluteTimeSeconds_ - appleKeyframeStartTime_);
    glm::mat4 model = glm::translate(glm::mat4(1.0f), whispyPosition_ + sample.offset);
    model = glm::rotate(model, glm::radians(sample.extraYawDegrees), glm::vec3(0.35f, 0.0f, 1.0f));
    model = glm::scale(model, glm::vec3(sample.scale));
    DrawColoredMesh(*lightMarkerMesh_, model, glm::vec3(0.90f, 0.10f, 0.06f), 0.08f, 0.08f, 0.9f);
}

void BaseScene::DrawPrimitivePlatforms() const
{
    if (cubeMesh_ == nullptr)
    {
        return;
    }

    for (const PrimitivePlatform& platform : platforms_)
    {
        const glm::mat4 model = glm::translate(glm::mat4(1.0f), platform.center)
            * glm::scale(glm::mat4(1.0f), platform.size);
        DrawColoredMesh(*cubeMesh_, model, platform.color, 0.03f, 0.0f, 1.0f);
    }
}

void BaseScene::DrawLitGeometry() const
{
    litShader_->SetBool("useTexture", true);
    litShader_->SetFloat("specularStrength", 0.0f);
    litShader_->SetFloat("pointLightResponse", 0.2f);
    litShader_->SetFloat("unlitFactor", 0.0f);
    litShader_->SetVec3("baseColor", glm::vec3(1.0f));
    litShader_->SetMat4("model", glm::mat4(1.0f));
    if (!zoneTwoViewThisFrame_)
    {
        litShader_->SetBool("floorDirtEnabled", dirtTexture_ != 0);
        litShader_->SetVec3("dirtPathStart", glm::vec3(0.0f, 0.0f, 16.0f));
        litShader_->SetVec3("dirtPathEnd", glm::vec3(0.0f, 0.0f, -16.5f));
        litShader_->SetFloat("dirtPathWidth", 1.35f);
        litShader_->SetFloat("dirtPathBlend", 0.24f);
        litShader_->SetFloat("dirtStartRadius", 2.95f);
        litShader_->SetFloat("dirtSineAmplitude", 1.25f);
        litShader_->SetFloat("dirtTextureScale", 0.55f);
        glActiveTexture(GL_TEXTURE0 + kDirtTextureUnit);
        glBindTexture(GL_TEXTURE_2D, dirtTexture_);
        glActiveTexture(GL_TEXTURE0);
        floorMesh_->Draw();
        litShader_->SetBool("floorDirtEnabled", false);
        litShader_->SetFloat("pointLightResponse", 1.0f);

        if (boundarySideWallMesh_ != nullptr && boundaryEndWallMesh_ != nullptr)
        {
            litShader_->SetBool("useTexture", true);
            litShader_->SetBool("shadowsEnabled", false);
            litShader_->SetFloat("specularStrength", 0.0f);
            litShader_->SetFloat("unlitFactor", 1.0f);
            litShader_->SetVec3("baseColor", glm::vec3(1.0f));
            const std::array<glm::vec3, 4> wallCenters = BuildBoundaryWallCenters(
                zoneOneBoundsMin_,
                zoneOneBoundsMax_,
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
            litShader_->SetBool("shadowsEnabled", shadowMapTexture_ != 0);
        }
    }
    litShader_->SetBool("floorDirtEnabled", false);
    litShader_->SetFloat("pointLightResponse", 1.0f);

    DrawPrimitivePlatforms();
    if (!zoneTwoViewThisFrame_)
    {
        DrawPortalHierarchy();
    }
    DrawVegetableValleyPrimitives();

    for (const SceneEntity& entity : entities_)
    {
        if (entity.placement.model == nullptr)
        {
            continue;
        }
        if (zoneTwoViewThisFrame_ && entity.worldPosition.x < 48.0f)
        {
            continue;
        }

        litShader_->SetBool("useTexture", entity.placement.model->HasTextures());
        litShader_->SetFloat("specularStrength", SpecularStrengthForEntity(entity.name));
        litShader_->SetFloat("unlitFactor", 0.0f);
        litShader_->SetFloat("pointLightResponse", 1.0f);
        litShader_->SetVec3("baseColor", glm::vec3(0.92f, 0.86f, 0.72f));
        litShader_->SetMat4("model", BuildStaticModelMatrix(entity));
        if (!entity.placement.model->HasTextures())
        {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, dirtTexture_);
        }
        entity.placement.model->Draw();
    }

    if (whispyFbxPreviewEnabled_ && whispyFbxPlacement_.model != nullptr)
    {
        litShader_->SetBool("useTexture", whispyFbxPlacement_.model->HasTextures());
        litShader_->SetFloat("specularStrength", 0.03f);
        litShader_->SetFloat("unlitFactor", 0.0f);
        litShader_->SetFloat("pointLightResponse", 0.8f);
        litShader_->SetVec3("baseColor", glm::vec3(0.60f, 0.42f, 0.24f));
        litShader_->SetMat4("model", BuildWhispyFbxModelMatrix());
        if (!whispyFbxPlacement_.model->HasTextures())
        {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, dirtTexture_);
        }
        whispyFbxPlacement_.model->Draw();
    }
    else
    {
        DrawWhispyProcedural();
    }

    DrawAppleKeyframe();
    DrawKirby();

    if (!zoneTwoViewThisFrame_ && signTextMesh_ != nullptr)
    {
        glm::mat4 signTextModel = glm::translate(glm::mat4(1.0f), signTextPosition_);
        signTextModel = glm::rotate(signTextModel, glm::radians(signTextYawDegrees_), glm::vec3(0.0f, 1.0f, 0.0f));
        signTextModel = glm::translate(signTextModel, glm::vec3(0.0f, 0.0f, 0.18f));

        litShader_->SetBool("useTexture", true);
        litShader_->SetFloat("specularStrength", 0.0f);
        litShader_->SetFloat("unlitFactor", 1.0f);
        litShader_->SetVec3("baseColor", glm::vec3(1.0f));
        litShader_->SetMat4("model", signTextModel);
        signTextMesh_->Draw();
        litShader_->SetFloat("unlitFactor", 0.0f);
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
