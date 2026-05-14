#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "CameraController.h"
#include "Mesh.h"
#include "Model.h"
#include "PlayerController.h"
#include "ShaderProgram.h"
#include "navigation/IWalkableWorld.h"
#include "physics/core/PhysicsTypes.h"
#include "render/debug/PhysicsDebugRenderer.h"

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
    bool ConsumePendingTeleport(glm::vec3& position, float& yawDegrees);
    void TriggerKeyframeAnimation();
    void TriggerKirbyEntranceAnimation();
    void ToggleWhispyVariant();

    [[nodiscard]] const std::string& GetActiveModelLabel() const noexcept;
    [[nodiscard]] glm::vec3 GetSceneBoundsMin() const noexcept;
    [[nodiscard]] glm::vec3 GetSceneBoundsMax() const noexcept;
    [[nodiscard]] glm::vec3 GetSuggestedPlayerSpawnPosition() const noexcept;
    [[nodiscard]] float GetSuggestedPlayerSpawnYawDegrees() const noexcept;
    [[nodiscard]] const std::vector<SceneCollisionSource>& GetStaticCollisionSources() const noexcept;
    [[nodiscard]] StaticRegionDesc BuildFloorCollisionRegion() const;
    [[nodiscard]] std::vector<WalkableBlocker> BuildWalkableBlockers() const;
    [[nodiscard]] std::vector<PlayerWalkableSurface> BuildZoneTwoWalkableSurfaces() const;
    [[nodiscard]] std::vector<CameraSolidCollider> BuildCameraSolidColliders() const;
    [[nodiscard]] std::vector<std::string> BuildHudLines() const;
    [[nodiscard]] std::vector<std::string> BuildInstructionLines() const;
    [[nodiscard]] std::vector<std::string> BuildContextMessageLines() const;
    [[nodiscard]] std::vector<std::string> BuildCenterMessageLines() const;
    [[nodiscard]] float GetDamageFlashAlpha() const noexcept;

private:
    enum class SceneZone
    {
        Entrance = 1,
        Parkour = 2,
        Whispy = 3
    };

    enum class WhispyState
    {
        Idle,
        Defeating,
        Defeated
    };

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
        bool contributesToCollision = true;
    };

    struct KeyframeSample
    {
        float time = 0.0f;
        glm::vec3 offset { 0.0f };
        float scale = 1.0f;
        float extraYawDegrees = 0.0f;
    };

    struct PrimitivePlatform
    {
        std::string name;
        glm::vec3 center { 0.0f };
        glm::vec3 size { 1.0f };
        glm::vec3 color { 0.5f };
    };

    struct CollectibleStar
    {
        std::string entityName;
        glm::vec3 position { 0.0f };
        bool collected = false;
    };

    struct TimedMessage
    {
        std::string text;
        float remainingTime = 0.0f;
        int priority = 0;
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
        SceneZone ownerZone = SceneZone::Entrance;
        bool visibleAcrossZones = false;
        bool castsShadow = false;
        GLuint shadowCubeMap = 0;
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
    std::unique_ptr<Mesh> cubeMesh_;
    std::unique_ptr<Mesh> skyboxMesh_;
    std::unique_ptr<Mesh> signTextMesh_;
    std::unique_ptr<PhysicsDebugRenderer> physicsDebugRenderer_;
    std::string activeModelLabel_ = "Physics refactor demo";
    glm::vec3 lightPosition_ { 0.0f, 7.5f, 6.0f };
    glm::vec3 sunDirection_ { -0.52f, -0.78f, -0.35f };
    glm::vec3 sunColor_ { 1.12f, 1.05f, 0.88f };
    glm::vec3 ambientSkyColor_ { 0.30f, 0.35f, 0.42f };
    glm::vec3 ambientGroundColor_ { 0.14f, 0.15f, 0.13f };
    glm::vec3 sceneBoundsMin_ { -20.0f, -8.0f, -36.0f };
    glm::vec3 sceneBoundsMax_ { 90.0f, 24.0f, 34.0f };
    glm::vec3 zoneOneBoundsMin_ { -13.5f, 0.0f, -20.0f };
    glm::vec3 zoneOneBoundsMax_ { 13.5f, 18.0f, 21.0f };
    glm::vec3 zoneTwoCenter_ { 70.0f, 2.0f, -4.0f };
    glm::vec3 playerSpawnPosition_ { 0.0f, 0.0f, 16.0f };
    glm::vec3 whispyPosition_ { 70.0f, 3.5f, -20.0f };
    glm::vec3 portalPosition_ { 0.0f, 1.25f, -15.5f };
    glm::vec3 portalPropPosition_ { 2.0f, 1.0f, -13.0f };
    glm::vec3 portalTargetPosition_ { 62.0f, 0.25f, 14.0f };
    glm::vec3 pendingTeleportPosition_ { 0.0f };
    glm::vec3 finalStarBasePosition_ { 0.0f, 1.4f, -22.5f };
    glm::vec3 signTextPosition_ { -1.8f, 1.05f, 18.5f };
    glm::vec3 hiddenStarPosition_ { 11.2f, 1.0f, 13.4f };
    glm::vec3 parkourStarPosition_ { 70.0f, 4.10f, -13.8f };
    glm::vec3 parkourGuideLightPosition_ { 65.0f, 2.45f, 8.8f };
    glm::vec3 parkourGuideLightTarget_ { 65.0f, 2.45f, 8.8f };
    glm::vec3 chickenPosition_ { -4.8f, 0.0f, 10.8f };
    glm::vec3 batPosition_ { 68.8f, 3.2f, 2.4f };
    float playerSpawnYawDegrees_ = -90.0f;
    float portalTargetYawDegrees_ = -90.0f;
    float pendingTeleportYawDegrees_ = -90.0f;
    float signTextYawDegrees_ = 180.0f;
    GLuint dirtTexture_ = 0;
    GLuint skyCloudTextureA_ = 0;
    GLuint skyCloudTextureB_ = 0;
    ModelPlacement kirbyPlacement_;
    ModelPlacement whispyFbxPlacement_;
    std::vector<SceneEntity> entities_;
    std::vector<InteractiveDoor> doors_;
    std::vector<PointLight> pointLights_;
    std::vector<WalkableBlocker> sceneBlockers_;
    std::vector<PrimitivePlatform> platforms_;
    std::vector<PlayerWalkableSurface> zoneTwoWalkableSurfaces_;
    std::vector<CameraSolidCollider> solidColliders_;
    std::vector<CollectibleStar> collectibleStars_;
    std::vector<SceneCollisionSource> staticCollisionSources_;
    PhysicsDebugFrame physicsDebugFrame_;
    PlayerSnapshot latestPlayer_;
    float absoluteTimeSeconds_ = 0.0f;
    float kirbyKeyframeStartTime_ = -100.0f;
    float portalKeyframeStartTime_ = -100.0f;
    float appleKeyframeStartTime_ = -100.0f;
    float finalActivationTime_ = -100.0f;
    float lastFallRespawnTime_ = -100.0f;
    TimedMessage timedMessage_;
    std::string contextMessage_;
    std::string lastParkourGuideSurface_;
    SceneZone currentZone_ = SceneZone::Entrance;
    SceneZone previousZone_ = SceneZone::Entrance;
    int stars_ = 0;
    int lives_ = 3;
    bool physicsDebugEnabled_ = false;
    bool kirbyKeyframeActive_ = false;
    bool portalKeyframeActive_ = false;
    bool portalOpen_ = false;
    bool portalTeleportPending_ = false;
    bool portalTeleportConsumed_ = false;
    bool appleKeyframeActive_ = false;
    bool finalZoneActivated_ = false;
    bool portalStarCollected_ = false;
    bool portalActivated_ = false;
    bool gameOverPendingReset_ = false;
    bool whispyFbxPreviewEnabled_ = false;
    bool whispyFbxLoadAttempted_ = false;
    bool whispyFbxLoadFailed_ = false;
    glm::vec2 floorHalfExtents_ { 13.5f, 21.0f };
    glm::vec2 floorUvTiling_ { 9.0f, 14.0f };
    float gameOverStartTime_ = -100.0f;
    float damageFlashTimer_ = 0.0f;
    float damageFlashDuration_ = 0.85f;
    float damageCooldownTimer_ = 0.0f;
    float whispyDefeatStartTime_ = -100.0f;
    float whispyDefeatDuration_ = 2.4f;
    bool parkourGuideLightInitialized_ = false;
    float boundaryWallHeight_ = 5.5f;
    float boundaryWallThickness_ = 0.70f;
    GLuint shadowMapFramebuffer_ = 0;
    GLuint shadowMapTexture_ = 0;
    GLuint pointShadowFramebuffer_ = 0;
    GLuint fallbackPointShadowCubeMap_ = 0;
    int shadowMapResolution_ = 2048;
    int pointShadowResolution_ = 1024;
    mutable bool shadowMapDirty_ = true;
    mutable bool pointShadowMapsDirty_ = true;
    mutable bool renderKirbyThisFrame_ = false;
    mutable bool zoneTwoViewThisFrame_ = false;
    mutable RenderPerfStats renderPerfStats_;

    void LoadEntities();
    void LoadVegetableValleyDemo();
    void ConfigureSceneLights();
    void AddStaticSceneEntity(
        const std::string& name,
        const std::filesystem::path& path,
        const glm::vec3& worldPosition,
        float worldYawDegrees,
        float targetSize,
        float yawOffsetDegrees,
        bool normalizeToHeight,
        bool loadTextures = true,
        bool contributesToCollision = true);
    void RegisterStaticCollisionSource(const SceneEntity& entity);
    void AddSceneBlocker(const std::string& name, const glm::vec3& center, const glm::vec3& halfExtents, float yawDegrees = 0.0f);
    void AddPrimitivePlatform(const std::string& name, const glm::vec3& center, const glm::vec3& size, const glm::vec3& color);
    void AddZoneTwoWalkableSurface(const std::string& name, const glm::vec3& center, const glm::vec3& halfExtents);
    void AddSolidCollider(const glm::vec3& center, const glm::vec3& halfExtents);
    void AddCollectibleStar(const std::string& entityName, const glm::vec3& position);
    void SetupPlacement(
        SceneEntity& entity,
        const std::filesystem::path& path,
        float targetSize,
        float yawOffsetDegrees,
        bool normalizeToHeight,
        bool loadTextures = true);
    [[nodiscard]] glm::mat4 BuildStaticModelMatrix(const SceneEntity& entity) const;
    [[nodiscard]] glm::mat4 BuildDoorModelMatrix(const InteractiveDoor& door) const;
    [[nodiscard]] glm::mat4 BuildKirbyModelMatrix() const;
    [[nodiscard]] glm::mat4 BuildWhispyFbxModelMatrix() const;
    [[nodiscard]] WalkableBlocker BuildDoorBlocker(const InteractiveDoor& door) const;
    [[nodiscard]] glm::mat4 BuildSunLightSpaceMatrix() const;
    [[nodiscard]] glm::vec3 ComputeStreetLightAnchor(const SceneEntity& entity) const;
    [[nodiscard]] SceneZone DetermineZone(const glm::vec3& position) const;
    [[nodiscard]] const CollectibleStar* FindCollectibleStar(const std::string& entityName) const;
    [[nodiscard]] CollectibleStar* FindCollectibleStar(const std::string& entityName);
    [[nodiscard]] bool IsCollectibleVisible(const std::string& entityName) const;
    void ShowTimedMessage(const std::string& text, float seconds, int priority);
    void RebuildContextMessage(const PlayerSnapshot& player);
    void HandleZoneTwoFall();
    void TriggerDamageFlash();
    bool ApplyPlayerDamage(const std::string& message, bool preserveProgress);
    void UpdateContactDamage(const PlayerSnapshot& player);
    void UpdateParkourGuideLight(const PlayerSnapshot& player, float deltaTimeSeconds);
    void ResetGameState(bool resetLives);
    [[nodiscard]] bool FindZoneTwoSurfaceGuidePosition(const std::string& surfaceName, glm::vec3& position) const;
    void RenderShadowMap(const glm::mat4& lightSpaceMatrix) const;
    void RenderPointShadowMaps() const;
    void AllocatePointLightShadowMaps();
    void ReleasePointLightShadowMaps();
    void DrawShadowCasters(const ShaderProgram& shader) const;
    void DrawLitGeometry() const;
    void DrawKirby() const;
    void DrawPortalHierarchy() const;
    void DrawAppleKeyframe() const;
    void DrawPrimitivePlatforms() const;
    void DrawVegetableValleyPrimitives() const;
    void DrawWhispyProcedural() const;
    void DrawColoredMesh(const Mesh& mesh, const glm::mat4& model, const glm::vec3& color, float specularStrength = 0.0f, float unlitFactor = 0.0f, float pointLightResponse = 1.0f) const;
    [[nodiscard]] KeyframeSample SampleKirbyEntranceKeyframes(float elapsedSeconds) const;
    [[nodiscard]] KeyframeSample SamplePortalKeyframes(float elapsedSeconds) const;
    [[nodiscard]] KeyframeSample SampleAppleFallKeyframes(float elapsedSeconds) const;
    [[nodiscard]] float WhispyDefeatProgress() const noexcept;

    WhispyState whispyState_ = WhispyState::Idle;

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
