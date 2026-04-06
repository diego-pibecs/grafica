#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Mesh.h"
#include "Model.h"
#include "MotionState.h"
#include "PlayerController.h"
#include "ShaderProgram.h"

class CameraController;

enum class EntityRole
{
    Player,
    StaticProp
};

struct MotionProfile
{
    float modelHeight = 1.0f;
    float idleBobFactor = 0.015f;
    float idlePitchDegrees = 1.5f;
    float walkBobFactor = 0.035f;
    float walkPitchDegrees = 4.0f;
    float walkRollDegrees = 2.0f;
    float runBobFactor = 0.055f;
    float runPitchDegrees = 7.0f;
    float runRollDegrees = 3.5f;
    float walkCycleSpeed = 7.0f;
    float runCycleSpeed = 10.5f;
    float landingDuration = 0.12f;
};

struct ProceduralMotionState
{
    MotionState state = MotionState::Idle;
    float cycle = 0.0f;
    float timeInAir = 0.0f;
    float landingTimer = 0.0f;
    float verticalVelocity = 0.0f;
    bool wasGrounded = true;
};

struct ColliderComponent
{
    glm::vec3 localMin { 0.0f };
    glm::vec3 localMax { 0.0f };
    glm::vec3 padding { 0.15f, 0.0f, 0.15f };
    bool enabled = false;
    bool blocksPlayer = false;
};

class BaseScene
{
public:
    explicit BaseScene(std::filesystem::path assetsRoot);

    void Init();
    void Update(const PlayerSnapshot& player, float deltaTime, float absoluteTimeSeconds);
    void Render(const CameraController& camera, const glm::mat4& projection) const;
    [[nodiscard]] glm::vec3 ResolvePlayerMovement(
        const glm::vec3& currentPosition,
        const glm::vec3& desiredPosition,
        float playerRadius,
        float playerHeight) const;

    [[nodiscard]] const std::string& GetActiveModelLabel() const noexcept;

private:
    struct ModelPlacement
    {
        std::unique_ptr<Model> model;
        glm::vec3 rawOffset { 0.0f };
        float scale = 1.0f;
        float yawOffsetDegrees = 180.0f;
    };

    struct SceneEntity
    {
        std::string name;
        ModelPlacement placement;
        glm::vec3 worldPosition { 0.0f };
        float worldYawDegrees = 0.0f;
        EntityRole role = EntityRole::StaticProp;
        bool enableProceduralMotion = false;
        bool enableCollision = false;
        bool blocksPlayer = false;
        MotionProfile motionProfile;
        ProceduralMotionState proceduralMotion;
        ColliderComponent collider;
    };

    struct WorldAabb
    {
        glm::vec3 min { 0.0f };
        glm::vec3 max { 0.0f };
    };

    std::filesystem::path assetsRoot_;
    std::unique_ptr<ShaderProgram> litShader_;
    std::unique_ptr<ShaderProgram> lightMarkerShader_;
    std::unique_ptr<Mesh> floorMesh_;
    std::unique_ptr<Mesh> lightMarkerMesh_;
    std::string activeModelLabel_ = "RedDog player + miGato center";
    glm::vec3 lightPosition_ { 3.2f, 4.4f, 3.0f };
    std::vector<SceneEntity> entities_;
    std::size_t playerEntityIndex_ = 0;
    float absoluteTimeSeconds_ = 0.0f;

    void LoadEntities();
    void SetupPlacement(
        SceneEntity& entity,
        const std::filesystem::path& path,
        float targetHeight,
        float yawOffsetDegrees);
    void UpdateProceduralMotion(SceneEntity& entity, const PlayerSnapshot& player, float deltaTime);
    [[nodiscard]] glm::mat4 BuildModelMatrix(const SceneEntity& entity, float absoluteTimeSeconds) const;
    [[nodiscard]] WorldAabb GetWorldAabb(const SceneEntity& entity) const;
    [[nodiscard]] bool IntersectsPlayer(
        const WorldAabb& aabb,
        const glm::vec3& playerPosition,
        float playerRadius,
        float playerHeight) const;

    static Mesh CreateFloorMesh(GLuint textureId);
    static Mesh CreateCubeMesh();
};
