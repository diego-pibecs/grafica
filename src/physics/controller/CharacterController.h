#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "navigation/IWalkableWorld.h"

struct CharacterParams
{
    float height = 1.8f;
    float radius = 0.35f;
    float maxSpeed = 3.2f;
    float sprintMultiplier = 1.75f;
    float acceleration = 18.0f;
    float airControl = 0.35f;
    float jumpSpeed = 4.6f;
    float gravity = 16.5f;
    float dynamicBlockerPushSpeed = 2.4f;
    float eyeHeight = 1.45f;
    float orbitTargetHeight = 0.95f;
};

struct CharacterContact
{
    glm::vec3 point { 0.0f };
    glm::vec3 normal { 0.0f, 1.0f, 0.0f };
    float fraction = 1.0f;
    std::string colliderName;
};

struct CharacterMoveRequest
{
    glm::vec3 wishMove { 0.0f };
    float wishMoveScale = 1.0f;
    bool sprintRequested = false;
    bool jumpRequested = false;
    float dt = 0.0f;
};

struct CharacterState
{
    glm::vec3 position { 0.0f };
    glm::vec3 velocity { 0.0f };
    bool grounded = true;
    glm::vec3 groundNormal { 0.0f, 1.0f, 0.0f };
    bool slopeBlocked = false;
    std::vector<CharacterContact> lastHits;
};

class CharacterController
{
public:
    CharacterController();

    void SetWalkableWorld(IWalkableWorld* world);
    void SetSpawn(const glm::vec3& position);
    void Update(const CharacterMoveRequest& request);

    [[nodiscard]] const CharacterState& GetState() const noexcept;
    [[nodiscard]] const CharacterParams& GetParams() const noexcept;
    [[nodiscard]] const PhysicsDebugFrame& GetDebugFrame() const noexcept;

private:
    CharacterParams params_;
    CharacterState state_;
    IWalkableWorld* walkableWorld_ = nullptr;
    std::uint64_t currentPolyRef_ = 0;
    PhysicsDebugFrame debugFrame_;
    float groundHeight_ = 0.0f;
    float verticalVelocity_ = 0.0f;

    void AppendCapsuleDebug(const glm::vec3& basePosition, const glm::vec3& color);
    void AppendRayDebug(const glm::vec3& start, const glm::vec3& end, const glm::vec3& color);
    bool SnapToWalkable(float searchRadius);
};
