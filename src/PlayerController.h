#pragma once

#include <functional>

#include <glm/glm.hpp>

#include "InputState.h"
#include "MotionState.h"

struct PlayerSnapshot
{
    glm::vec3 position { 0.0f, 0.0f, 6.0f };
    glm::vec3 eyePosition { 0.0f, 1.45f, 6.0f };
    glm::vec3 orbitTarget { 0.0f, 0.95f, 6.0f };
    glm::vec3 velocity { 0.0f, 0.0f, 0.0f };
    float facingYawDegrees = -90.0f;
    float horizontalSpeed = 0.0f;
    MotionState motionState = MotionState::Idle;
    bool grounded = true;
    bool sprinting = false;
};

class PlayerController
{
public:
    using CollisionResolver = std::function<glm::vec3(const glm::vec3&, const glm::vec3&, float, float)>;

    PlayerController();

    void Update(const InputState& input, float movementYawDegrees, const CollisionResolver& collisionResolver);

    [[nodiscard]] const PlayerSnapshot& GetSnapshot() const noexcept;
    [[nodiscard]] glm::vec3 GetPosition() const noexcept;
    [[nodiscard]] glm::vec3 GetEyePosition() const noexcept;
    [[nodiscard]] glm::vec3 GetOrbitTarget() const noexcept;

private:
    PlayerSnapshot snapshot_;
    float verticalVelocity_ = 0.0f;
    float walkSpeed_ = 3.2f;
    float sprintMultiplier_ = 1.75f;
    float jumpVelocity_ = 6.6f;
    float gravity_ = -18.0f;
    float groundHeight_ = 0.0f;
    float eyeHeight_ = 1.45f;
    float orbitTargetHeight_ = 0.95f;
    float playerRadius_ = 0.45f;
    float playerHeight_ = 1.80f;

    void RefreshDerivedPoints();
};
