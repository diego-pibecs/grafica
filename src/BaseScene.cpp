#include "BaseScene.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <stdexcept>
#include <utility>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "CameraController.h"
#include "TextureUtils.h"

namespace
{
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

float SmoothDecay(float normalizedTimeRemaining)
{
    const float clamped = std::clamp(normalizedTimeRemaining, 0.0f, 1.0f);
    return clamped * clamped * (3.0f - (2.0f * clamped));
}
}

BaseScene::BaseScene(std::filesystem::path assetsRoot)
    : assetsRoot_(std::move(assetsRoot))
{
}

void BaseScene::Init()
{
    litShader_ = std::make_unique<ShaderProgram>(
        assetsRoot_ / "shaders" / "lit.vs",
        assetsRoot_ / "shaders" / "lit.frag");
    lightMarkerShader_ = std::make_unique<ShaderProgram>(
        assetsRoot_ / "shaders" / "light_marker.vs",
        assetsRoot_ / "shaders" / "light_marker.frag");

    const GLuint checkerTexture = LoadTexture2D(assetsRoot_ / "textures" / "checker_Tex.png");
    floorMesh_ = std::make_unique<Mesh>(CreateFloorMesh(checkerTexture));
    lightMarkerMesh_ = std::make_unique<Mesh>(CreateCubeMesh());

    LoadEntities();
}

void BaseScene::Update(const PlayerSnapshot& player, float deltaTime, float absoluteTimeSeconds)
{
    absoluteTimeSeconds_ = absoluteTimeSeconds;

    if (playerEntityIndex_ < entities_.size())
    {
        SceneEntity& playerEntity = entities_[playerEntityIndex_];
        playerEntity.worldPosition = player.position;
        playerEntity.worldYawDegrees = player.facingYawDegrees;
    }

    for (SceneEntity& entity : entities_)
    {
        UpdateProceduralMotion(entity, player, deltaTime);
    }
}

void BaseScene::Render(const CameraController& camera, const glm::mat4& projection) const
{
    const glm::mat4 view = camera.GetViewMatrix();

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
    litShader_->SetVec3("baseColor", glm::vec3(1.0f, 1.0f, 1.0f));
    litShader_->SetMat4("model", glm::mat4(1.0f));
    floorMesh_->Draw();

    for (const SceneEntity& entity : entities_)
    {
        if (entity.placement.model == nullptr)
        {
            continue;
        }

        litShader_->SetBool("useTexture", entity.placement.model->HasTextures());
        litShader_->SetVec3(
            "baseColor",
            entity.role == EntityRole::Player ? glm::vec3(0.86f, 0.32f, 0.28f) : glm::vec3(0.92f, 0.86f, 0.72f));
        litShader_->SetMat4("model", BuildModelMatrix(entity, absoluteTimeSeconds_));
        entity.placement.model->Draw();
    }

    lightMarkerShader_->Use();
    lightMarkerShader_->SetMat4("projection", projection);
    lightMarkerShader_->SetMat4("view", view);
    lightMarkerShader_->SetVec3("color", glm::vec3(1.0f, 0.93f, 0.70f));
    glm::mat4 lightMarkerModel = glm::translate(glm::mat4(1.0f), lightPosition_);
    lightMarkerModel = glm::scale(lightMarkerModel, glm::vec3(0.18f));
    lightMarkerShader_->SetMat4("model", lightMarkerModel);
    lightMarkerMesh_->Draw();
}

glm::vec3 BaseScene::ResolvePlayerMovement(
    const glm::vec3& currentPosition,
    const glm::vec3& desiredPosition,
    float playerRadius,
    float playerHeight) const
{
    glm::vec3 resolvedPosition = currentPosition;

    glm::vec3 candidate = resolvedPosition;
    candidate.x = desiredPosition.x;
    bool blockedOnX = false;
    for (const SceneEntity& entity : entities_)
    {
        if (!entity.enableCollision || !entity.blocksPlayer || entity.role == EntityRole::Player)
        {
            continue;
        }

        if (IntersectsPlayer(GetWorldAabb(entity), candidate, playerRadius, playerHeight))
        {
            blockedOnX = true;
            break;
        }
    }
    if (!blockedOnX)
    {
        resolvedPosition.x = desiredPosition.x;
    }

    candidate = resolvedPosition;
    candidate.z = desiredPosition.z;
    bool blockedOnZ = false;
    for (const SceneEntity& entity : entities_)
    {
        if (!entity.enableCollision || !entity.blocksPlayer || entity.role == EntityRole::Player)
        {
            continue;
        }

        if (IntersectsPlayer(GetWorldAabb(entity), candidate, playerRadius, playerHeight))
        {
            blockedOnZ = true;
            break;
        }
    }
    if (!blockedOnZ)
    {
        resolvedPosition.z = desiredPosition.z;
    }

    return resolvedPosition;
}

const std::string& BaseScene::GetActiveModelLabel() const noexcept
{
    return activeModelLabel_;
}

void BaseScene::LoadEntities()
{
    entities_.clear();

    SceneEntity playerEntity;
    playerEntity.name = "RedDog";
    playerEntity.role = EntityRole::Player;
    playerEntity.worldPosition = glm::vec3(0.0f, 0.0f, 6.0f);
    playerEntity.worldYawDegrees = -90.0f;
    playerEntity.enableProceduralMotion = true;
    playerEntity.enableCollision = true;
    playerEntity.blocksPlayer = false;
    playerEntity.collider.enabled = true;
    playerEntity.collider.blocksPlayer = false;
    playerEntity.collider.padding = glm::vec3(0.08f, 0.0f, 0.08f);
    SetupPlacement(
        playerEntity,
        assetsRoot_ / "models" / "red_dog" / "RedDog.obj",
        2.3f,
        180.0f);

    SceneEntity catEntity;
    catEntity.name = "miGato";
    catEntity.role = EntityRole::StaticProp;
    catEntity.worldPosition = glm::vec3(0.0f, 0.0f, 0.0f);
    catEntity.worldYawDegrees = 25.0f;
    catEntity.enableProceduralMotion = false;
    catEntity.enableCollision = true;
    catEntity.blocksPlayer = true;
    catEntity.collider.enabled = true;
    catEntity.collider.blocksPlayer = true;
    catEntity.collider.padding = glm::vec3(0.20f, 0.0f, 0.20f);
    SetupPlacement(
        catEntity,
        assetsRoot_ / "models" / "mi_gato" / "miGato.obj",
        1.8f,
        180.0f);

    if (playerEntity.placement.model == nullptr)
    {
        throw std::runtime_error("No se pudo cargar RedDog.obj como personaje.");
    }

    if (catEntity.placement.model == nullptr)
    {
        throw std::runtime_error("No se pudo cargar miGato.obj como modelo fijo.");
    }

    entities_.push_back(std::move(playerEntity));
    playerEntityIndex_ = 0;
    entities_.push_back(std::move(catEntity));
}

void BaseScene::SetupPlacement(
    SceneEntity& entity,
    const std::filesystem::path& path,
    float targetHeight,
    float yawOffsetDegrees)
{
    entity.placement.model = std::make_unique<Model>(path);
    if (!entity.placement.model->IsLoaded())
    {
        entity.placement.model.reset();
        return;
    }

    const glm::vec3 size = entity.placement.model->GetSize();
    const float height = std::max(size.y, 0.001f);
    entity.placement.scale = targetHeight / height;

    const glm::vec3 center = entity.placement.model->GetCenter();
    const glm::vec3 minBounds = entity.placement.model->GetMinBounds();
    const glm::vec3 maxBounds = entity.placement.model->GetMaxBounds();
    entity.placement.rawOffset = glm::vec3(-center.x, -minBounds.y, -center.z);
    entity.placement.yawOffsetDegrees = yawOffsetDegrees;
    entity.motionProfile.modelHeight = size.y * entity.placement.scale;

    const glm::vec3 scaledMin = (minBounds + entity.placement.rawOffset) * entity.placement.scale;
    const glm::vec3 scaledMax = (maxBounds + entity.placement.rawOffset) * entity.placement.scale;
    entity.collider.localMin = glm::min(scaledMin, scaledMax);
    entity.collider.localMax = glm::max(scaledMin, scaledMax);
}

void BaseScene::UpdateProceduralMotion(SceneEntity& entity, const PlayerSnapshot& player, float deltaTime)
{
    ProceduralMotionState& proceduralMotion = entity.proceduralMotion;
    const MotionState previousState = proceduralMotion.state;

    if (entity.role == EntityRole::Player)
    {
        proceduralMotion.state = player.motionState;
        proceduralMotion.verticalVelocity = player.velocity.y;

        if (player.motionState == MotionState::Airborne)
        {
            proceduralMotion.timeInAir += deltaTime;
            proceduralMotion.wasGrounded = false;
        }
        else
        {
            if (!proceduralMotion.wasGrounded && player.grounded)
            {
                proceduralMotion.landingTimer = entity.motionProfile.landingDuration;
            }

            proceduralMotion.timeInAir = 0.0f;
            proceduralMotion.wasGrounded = true;
        }

        if (player.motionState == MotionState::Walk)
        {
            const float cycleBoost = std::clamp(player.horizontalSpeed / 3.2f, 0.65f, 1.35f);
            proceduralMotion.cycle += deltaTime * entity.motionProfile.walkCycleSpeed * cycleBoost;
        }
        else if (player.motionState == MotionState::Run)
        {
            const float cycleBoost = std::clamp(player.horizontalSpeed / 5.6f, 0.75f, 1.40f);
            proceduralMotion.cycle += deltaTime * entity.motionProfile.runCycleSpeed * cycleBoost;
        }
    }
    else
    {
        proceduralMotion.state = entity.enableProceduralMotion ? MotionState::Idle : MotionState::Idle;
        proceduralMotion.verticalVelocity = 0.0f;
        proceduralMotion.timeInAir = 0.0f;
        proceduralMotion.wasGrounded = true;
    }

    proceduralMotion.landingTimer = std::max(proceduralMotion.landingTimer - deltaTime, 0.0f);

    if (previousState != MotionState::Airborne && proceduralMotion.state == MotionState::Airborne)
    {
        proceduralMotion.timeInAir = 0.0f;
    }
}

glm::mat4 BaseScene::BuildModelMatrix(const SceneEntity& entity, float absoluteTimeSeconds) const
{
    glm::vec3 proceduralTranslation(0.0f);
    glm::vec3 proceduralScale(1.0f, 1.0f, 1.0f);
    float proceduralPitch = 0.0f;
    float proceduralRoll = 0.0f;

    if (entity.enableProceduralMotion)
    {
        const MotionProfile& profile = entity.motionProfile;
        const ProceduralMotionState& motion = entity.proceduralMotion;

        if (motion.state == MotionState::Idle)
        {
            proceduralTranslation.y = std::sin(absoluteTimeSeconds * 2.2f) * (profile.idleBobFactor * profile.modelHeight);
            proceduralPitch = std::sin(absoluteTimeSeconds * 1.1f) * profile.idlePitchDegrees;
        }
        else if (motion.state == MotionState::Walk)
        {
            proceduralTranslation.y = std::sin(motion.cycle) * (profile.walkBobFactor * profile.modelHeight);
            proceduralPitch = std::sin(motion.cycle) * profile.walkPitchDegrees;
            proceduralRoll = std::sin(motion.cycle) * profile.walkRollDegrees;
        }
        else if (motion.state == MotionState::Run)
        {
            proceduralTranslation.y = std::sin(motion.cycle) * (profile.runBobFactor * profile.modelHeight);
            proceduralPitch = std::sin(motion.cycle) * profile.runPitchDegrees;
            proceduralRoll = std::sin(motion.cycle) * profile.runRollDegrees;
        }

        if (motion.state == MotionState::Airborne && motion.verticalVelocity > 0.0f)
        {
            proceduralScale = glm::vec3(0.97f, 1.06f, 0.97f);
        }

        if (motion.landingTimer > 0.0f)
        {
            const float normalized = motion.landingTimer / std::max(profile.landingDuration, 0.001f);
            const float amount = SmoothDecay(normalized);
            proceduralScale.x *= 1.0f + (0.04f * amount);
            proceduralScale.z *= 1.0f + (0.04f * amount);
            proceduralScale.y *= 1.0f - (0.08f * amount);
        }
    }

    glm::mat4 model = glm::translate(glm::mat4(1.0f), entity.worldPosition);
    model = glm::rotate(model, glm::radians(entity.worldYawDegrees + entity.placement.yawOffsetDegrees), glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::translate(model, proceduralTranslation);
    model = glm::rotate(model, glm::radians(proceduralPitch), glm::vec3(1.0f, 0.0f, 0.0f));
    model = glm::rotate(model, glm::radians(proceduralRoll), glm::vec3(0.0f, 0.0f, 1.0f));
    model = glm::scale(model, proceduralScale);
    model = glm::scale(model, glm::vec3(entity.placement.scale));
    model = glm::translate(model, entity.placement.rawOffset);
    return model;
}

BaseScene::WorldAabb BaseScene::GetWorldAabb(const SceneEntity& entity) const
{
    WorldAabb aabb;
    aabb.min = entity.worldPosition + entity.collider.localMin - entity.collider.padding;
    aabb.max = entity.worldPosition + entity.collider.localMax + entity.collider.padding;
    return aabb;
}

bool BaseScene::IntersectsPlayer(
    const WorldAabb& aabb,
    const glm::vec3& playerPosition,
    float playerRadius,
    float playerHeight) const
{
    const float playerMinY = playerPosition.y;
    const float playerMaxY = playerPosition.y + playerHeight;

    if (playerMaxY < aabb.min.y || playerMinY > aabb.max.y)
    {
        return false;
    }

    const float closestX = std::clamp(playerPosition.x, aabb.min.x, aabb.max.x);
    const float closestZ = std::clamp(playerPosition.z, aabb.min.z, aabb.max.z);
    const float deltaX = playerPosition.x - closestX;
    const float deltaZ = playerPosition.z - closestZ;

    return ((deltaX * deltaX) + (deltaZ * deltaZ)) <= (playerRadius * playerRadius);
}

Mesh BaseScene::CreateFloorMesh(GLuint textureId)
{
    std::vector<Vertex> vertices = MakeVertices({
        -18.0f, 0.0f, -18.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
         18.0f, 0.0f, -18.0f, 0.0f, 1.0f, 0.0f, 12.0f, 0.0f,
         18.0f, 0.0f,  18.0f, 0.0f, 1.0f, 0.0f, 12.0f, 12.0f,
        -18.0f, 0.0f,  18.0f, 0.0f, 1.0f, 0.0f, 0.0f, 12.0f
    });

    std::vector<unsigned int> indices { 0, 1, 2, 0, 2, 3 };
    std::vector<Texture> textures {
        Texture { textureId, "texture_diffuse", "checker_Tex.png" }
    };

    return Mesh(std::move(vertices), std::move(indices), std::move(textures));
}

Mesh BaseScene::CreateCubeMesh()
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

    return Mesh(std::move(vertices), std::move(indices));
}
