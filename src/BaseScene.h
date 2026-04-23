#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Mesh.h"
#include "Model.h"
#include "PlayerController.h"
#include "ShaderProgram.h"
#include "navigation/IWalkableWorld.h"
#include "physics/core/PhysicsTypes.h"
#include "render/debug/PhysicsDebugRenderer.h"

class CameraController;

struct SceneCollisionSource
{
    std::string name;
    std::filesystem::path sourcePath;
    std::filesystem::path collisionProfilePath;
    glm::mat4 transform { 1.0f };
};

class BaseScene
{
public:
    explicit BaseScene(std::filesystem::path assetsRoot);

    void Init();
    void Update(const PlayerSnapshot& player, float absoluteTimeSeconds, float deltaTimeSeconds);
    void Render(const CameraController& camera, const glm::mat4& projection) const;

    void SetPhysicsDebugFrame(PhysicsDebugFrame frame);
    void SetPhysicsDebugEnabled(bool enabled);
    bool TryInteract(const glm::vec3& rayOrigin, const glm::vec3& rayDirection, const glm::vec3& playerPosition);

    [[nodiscard]] const std::string& GetActiveModelLabel() const noexcept;
    [[nodiscard]] glm::vec3 GetSceneBoundsMin() const noexcept;
    [[nodiscard]] glm::vec3 GetSceneBoundsMax() const noexcept;
    [[nodiscard]] glm::vec3 GetSuggestedPlayerSpawnPosition() const noexcept;
    [[nodiscard]] float GetSuggestedPlayerSpawnYawDegrees() const noexcept;
    [[nodiscard]] const std::vector<SceneCollisionSource>& GetStaticCollisionSources() const noexcept;
    [[nodiscard]] StaticRegionDesc BuildFloorCollisionRegion() const;
    [[nodiscard]] std::vector<WalkableBlocker> BuildWalkableBlockers() const;

private:
    struct ModelPlacement
    {
        std::filesystem::path sourcePath;
        std::unique_ptr<Model> model;
        glm::vec3 rawOffset { 0.0f };
        float scale = 1.0f;
        float yawOffsetDegrees = 0.0f;
    };

    struct SceneEntity
    {
        std::string name;
        ModelPlacement placement;
        glm::vec3 worldPosition { 0.0f };
        float worldYawDegrees = 0.0f;
    };

    enum class DoorMotionType
    {
        Swing,
        Slide
    };

    struct InteractiveDoor
    {
        std::string name;
        ModelPlacement placement;
        glm::vec3 worldPosition { 0.0f };
        float worldYawDegrees = 0.0f;
        glm::vec3 localMin { 0.0f };
        glm::vec3 localMax { 0.0f };
        glm::vec3 localHinge { 0.0f };
        bool open = false;
        float openProgress = 0.0f;
        DoorMotionType motionType = DoorMotionType::Swing;
        float openAngleDegrees = 92.0f;
        glm::vec3 localSlideDirection { 1.0f, 0.0f, 0.0f };
        float slideDistance = 1.2f;
        float animationSpeed = 2.8f;
        float interactRadius = 1.6f;
    };

    std::filesystem::path assetsRoot_;
    std::unique_ptr<ShaderProgram> litShader_;
    std::unique_ptr<ShaderProgram> lightMarkerShader_;
    std::unique_ptr<ShaderProgram> skyboxShader_;
    std::unique_ptr<Mesh> floorMesh_;
    std::unique_ptr<Mesh> boundarySideWallMesh_;
    std::unique_ptr<Mesh> boundaryEndWallMesh_;
    std::unique_ptr<Mesh> lightMarkerMesh_;
    std::unique_ptr<Mesh> skyboxMesh_;
    std::unique_ptr<PhysicsDebugRenderer> physicsDebugRenderer_;
    std::string activeModelLabel_ = "Physics refactor demo";
    glm::vec3 lightPosition_ { 0.0f, 7.5f, 6.0f };
    glm::vec3 sceneBoundsMin_ { -16.0f, 0.0f, -32.0f };
    glm::vec3 sceneBoundsMax_ { 16.0f, 18.0f, 32.0f };
    glm::vec3 playerSpawnPosition_ { 0.0f, 0.0f, 22.0f };
    float playerSpawnYawDegrees_ = -90.0f;
    GLuint skyCloudTextureA_ = 0;
    GLuint skyCloudTextureB_ = 0;
    std::vector<SceneEntity> entities_;
    std::vector<InteractiveDoor> doors_;
    std::vector<SceneCollisionSource> staticCollisionSources_;
    PhysicsDebugFrame physicsDebugFrame_;
    float absoluteTimeSeconds_ = 0.0f;
    bool physicsDebugEnabled_ = false;
    glm::vec2 floorHalfExtents_ { 16.0f, 32.0f };
    glm::vec2 floorUvTiling_ { 10.0f, 20.0f };
    float boundaryWallHeight_ = 5.5f;
    float boundaryWallThickness_ = 0.70f;

    void LoadEntities();
    void LoadHouseDemo();
    void LoadExteriorDecorations();
    void LoadHouseDoors(const SceneEntity& houseEntity);
    void AddStaticSceneEntity(
        const std::string& name,
        const std::filesystem::path& path,
        const glm::vec3& worldPosition,
        float worldYawDegrees,
        float targetSize,
        float yawOffsetDegrees,
        bool normalizeToHeight,
        bool loadTextures = true);
    void RegisterStaticCollisionSource(const SceneEntity& entity);
    void SetupPlacement(
        SceneEntity& entity,
        const std::filesystem::path& path,
        float targetSize,
        float yawOffsetDegrees,
        bool normalizeToHeight,
        bool loadTextures = true);
    [[nodiscard]] glm::mat4 BuildStaticModelMatrix(const SceneEntity& entity) const;
    [[nodiscard]] glm::mat4 BuildDoorModelMatrix(const InteractiveDoor& door) const;
    [[nodiscard]] WalkableBlocker BuildDoorBlocker(const InteractiveDoor& door) const;

    static Mesh CreateFloorMesh(GLuint textureId, const glm::vec2& halfExtents, const glm::vec2& uvTiling);
    static Mesh CreateTexturedBoxMesh(
        GLuint textureId,
        const std::string& textureName,
        const glm::vec3& size,
        float tileWorldSize);
    static Mesh CreateCubeMesh(GLuint textureId = 0, const std::string& textureName = {});
};
