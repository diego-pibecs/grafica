#pragma once

#include <cstdint>
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
    ~BaseScene();

    void Init();
    void Update(const PlayerSnapshot& player, float absoluteTimeSeconds, float deltaTimeSeconds);
    void Render(const CameraController& camera, const glm::mat4& projection) const;

    void SetPhysicsDebugFrame(PhysicsDebugFrame frame);
    void SetPhysicsDebugEnabled(bool enabled);
    bool TryInteract(const glm::vec3& rayOrigin, const glm::vec3& rayDirection, const glm::vec3& playerPosition);
    [[nodiscard]] std::vector<std::string> BuildHudLines() const;
    [[nodiscard]] std::vector<std::string> BuildContextMessageLines() const;
    [[nodiscard]] std::vector<std::string> BuildCenterMessageLines() const;
    [[nodiscard]] float GetAnxietyTintAlpha() const noexcept;

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

    enum class StoryPhase
    {
        ExteriorStart,
        TriggerWalk,
        AnxietyActivated,
        HouseEntry,
        CleaningLoop,
        DiscardLoop,
        EmptyHouse,
        DarkReflection,
        Finished
    };

    struct TimedMessage
    {
        std::vector<std::string> lines;
        float remainingSeconds = 0.0f;
    };

    struct NarrativeTrigger
    {
        std::string id;
        glm::vec3 position { 0.0f };
        float radius = 2.0f;
        bool activated = false;
        StoryPhase requiredPhase = StoryPhase::ExteriorStart;
        StoryPhase nextPhase = StoryPhase::TriggerWalk;
        std::vector<std::string> messages;
        float anxietyDelta = 0.0f;
    };

    struct CarryableObject
    {
        std::string id;
        std::string displayName;
        ModelPlacement placement;
        glm::vec3 worldPosition { 0.0f };
        float worldYawDegrees = 0.0f;
        bool visible = true;
        bool pickedUp = false;
        bool carried = false;
        bool discarded = false;
        bool requiredForEmptyHouse = true;
        bool blocksNavigation = true;
        float interactRadius = 2.8f;
        glm::vec3 localMin { 0.0f };
        glm::vec3 localMax { 0.0f };
    };

    struct DropZone
    {
        std::string id;
        glm::vec3 position { 0.0f };
        float radius = 3.0f;
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

    struct PointLight
    {
        std::string label;
        glm::vec3 position { 0.0f };
        glm::vec3 color { 1.0f };
        float intensity = 1.0f;
        float range = 8.0f;
        bool castsShadow = false;
        GLuint shadowCubeMap = 0;
    };

    struct HouseLight
    {
        std::string id;
        glm::vec3 position { 0.0f };
        glm::vec3 color { 1.0f, 0.84f, 0.62f };
        float intensity = 2.0f;
        float range = 5.2f;
        float activationDistance = 32.0f;
        bool enabled = true;
        bool active = false;
        bool interactable = false;
        bool proximity = false;
    };

    struct RenderPerfStats
    {
        std::uint64_t frameCount = 0;
        double totalMs = 0.0;
        double shadowMs = 0.0;
        double skyboxMs = 0.0;
        double litMs = 0.0;
        double markersMs = 0.0;
        double debugMs = 0.0;
        double maxFrameMs = 0.0;
    };

    std::filesystem::path assetsRoot_;
    std::unique_ptr<ShaderProgram> litShader_;
    std::unique_ptr<ShaderProgram> shadowDepthShader_;
    std::unique_ptr<ShaderProgram> pointShadowDepthShader_;
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
    glm::vec3 sunDirection_ { -0.52f, -0.78f, -0.35f };
    glm::vec3 sunColor_ { 1.12f, 1.05f, 0.88f };
    glm::vec3 ambientSkyColor_ { 0.30f, 0.35f, 0.42f };
    glm::vec3 ambientGroundColor_ { 0.14f, 0.15f, 0.13f };
    glm::vec3 sceneBoundsMin_ { -16.0f, 0.0f, -32.0f };
    glm::vec3 sceneBoundsMax_ { 16.0f, 18.0f, 32.0f };
    glm::vec3 playerSpawnPosition_ { 0.0f, 0.0f, 22.0f };
    float playerSpawnYawDegrees_ = -90.0f;
    GLuint skyCloudTextureA_ = 0;
    GLuint skyCloudTextureB_ = 0;
    GLuint dirtTexture_ = 0;
    std::vector<SceneEntity> entities_;
    std::vector<InteractiveDoor> doors_;
    std::vector<NarrativeTrigger> narrativeTriggers_;
    std::vector<CarryableObject> carryableObjects_;
    DropZone dumpsterDropZone_;
    std::vector<PointLight> basePointLights_;
    std::vector<PointLight> pointLights_;
    std::vector<HouseLight> houseLights_;
    std::vector<SceneCollisionSource> staticCollisionSources_;
    PhysicsDebugFrame physicsDebugFrame_;
    float absoluteTimeSeconds_ = 0.0f;
    bool physicsDebugEnabled_ = false;
    glm::vec2 floorHalfExtents_ { 16.0f, 32.0f };
    glm::vec2 floorUvTiling_ { 10.0f, 20.0f };
    float boundaryWallHeight_ = 5.5f;
    float boundaryWallThickness_ = 0.70f;
    GLuint shadowMapFramebuffer_ = 0;
    GLuint shadowMapTexture_ = 0;
    GLuint pointShadowFramebuffer_ = 0;
    int shadowMapResolution_ = 2048;
    int pointShadowResolution_ = 1024;
    mutable bool shadowMapDirty_ = true;
    mutable bool pointShadowMapsDirty_ = true;
    mutable RenderPerfStats renderPerfStats_;
    StoryPhase currentPhase_ = StoryPhase::ExteriorStart;
    float anxietyLevel_ = 0.0f;
    float anxietyPulse_ = 0.0f;
    float contaminationLevel_ = 0.0f;
    int cleanedCount_ = 0;
    int discardedCount_ = 0;
    int totalDiscardableObjects_ = 0;
    int requiredDiscardCount_ = 0;
    bool anxietySystemActive_ = false;
    bool darkReflectionUnlocked_ = false;
    int carriedObjectIndex_ = -1;
    TimedMessage centerMessage_;
    bool tvFallActive_ = false;
    bool tvHasFallen_ = false;
    float tvFallStartTime_ = 0.0f;
    glm::vec3 tvFallStartPosition_ { 0.0f };
    glm::vec3 tvFallTargetPosition_ { 0.0f };

    void LoadEntities();
    void LoadHouseDemo();
    void LoadExteriorDecorations();
    void LoadHouseDoors(const SceneEntity& houseEntity);
    void LoadHouseCarryableObjects(const SceneEntity& houseEntity);
    void ConfigureSceneLights();
    void UpdateActivePointLights(const glm::vec3& playerPosition);
    void AddStaticSceneEntity(
        const std::string& name,
        const std::filesystem::path& path,
        const glm::vec3& worldPosition,
        float worldYawDegrees,
        float targetSize,
        float yawOffsetDegrees,
        bool normalizeToHeight,
        bool loadTextures = true,
        bool registerCollision = true);
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
    [[nodiscard]] glm::mat4 BuildCarryableModelMatrix(const CarryableObject& object) const;
    [[nodiscard]] WalkableBlocker BuildDoorBlocker(const InteractiveDoor& door) const;
    [[nodiscard]] WalkableBlocker BuildCarryableBlocker(const CarryableObject& object) const;
    [[nodiscard]] glm::mat4 BuildSunLightSpaceMatrix() const;
    [[nodiscard]] glm::vec3 ComputeStreetLightAnchor(const SceneEntity& entity) const;
    void RenderShadowMap(const glm::mat4& lightSpaceMatrix) const;
    void RenderPointShadowMaps() const;
    void AllocatePointLightShadowMaps();
    void ReleasePointLightShadowMaps();
    void DrawShadowCasters(const ShaderProgram& shader) const;
    void DrawLitGeometry() const;
    void UpdateStoryState(const PlayerSnapshot& player, float deltaTimeSeconds);
    void UpdateTvFall();
    void StartTvFallIfNeeded(const CarryableObject& pickedObject);
    void AddNarrativeTrigger(
        const std::string& id,
        const glm::vec3& position,
        float radius,
        StoryPhase requiredPhase,
        StoryPhase nextPhase,
        std::vector<std::string> messages,
        float anxietyDelta);
    void ShowCenterMessage(std::vector<std::string> lines, float durationSeconds);
    void ActivateAnxiety(float delta);
    void ReduceAnxiety(float amount);
    [[nodiscard]] std::string BuildAnxietyBar(float value) const;
    [[nodiscard]] bool IsPhaseAtLeast(StoryPhase phase) const noexcept;
    [[nodiscard]] int FindTargetedCarryable(const glm::vec3& rayOrigin, const glm::vec3& rayDirection, const glm::vec3& playerPosition) const;
    [[nodiscard]] int FindTargetedHouseLight(const glm::vec3& rayOrigin, const glm::vec3& rayDirection, const glm::vec3& playerPosition) const;
    [[nodiscard]] bool IsPlayerNearDropZone(const glm::vec3& playerPosition) const;
    bool TryToggleHouseLight(const glm::vec3& rayOrigin, const glm::vec3& rayDirection, const glm::vec3& playerPosition);
    bool TryDiscardCarriedObject(const glm::vec3& playerPosition);
    bool TryPickupCarryable(int objectIndex);

    static Mesh CreateFloorMesh(GLuint textureId, const glm::vec2& halfExtents, const glm::vec2& uvTiling);
    static Mesh CreateTexturedBoxMesh(
        GLuint textureId,
        const std::string& textureName,
        const glm::vec3& size,
        float tileWorldSize);
    static Mesh CreateTexturedWallPlaneMesh(
        GLuint textureId,
        const std::string& textureName,
        const glm::vec2& size,
        float tileWorldSize,
        bool horizontalAxisIsX);
    static Mesh CreateCubeMesh(GLuint textureId = 0, const std::string& textureName = {});
    static Mesh CreateSphereMesh(int latitudeSegments, int longitudeSegments);
};
