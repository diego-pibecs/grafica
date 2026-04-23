#pragma once

#include <glm/glm.hpp>

#include "InputState.h"
#include "MotionState.h"
#include "physics/controller/CharacterController.h"

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
    PlayerController();

    void SetWalkableWorld(IWalkableWorld* world);
    void Update(const InputState& input, float movementYawDegrees);
    void SetSpawn(const glm::vec3& position, float facingYawDegrees);

    [[nodiscard]] const PlayerSnapshot& GetSnapshot() const noexcept;
    [[nodiscard]] glm::vec3 GetPosition() const noexcept;
    [[nodiscard]] glm::vec3 GetEyePosition() const noexcept;
    [[nodiscard]] glm::vec3 GetOrbitTarget() const noexcept;
    [[nodiscard]] const PhysicsDebugFrame& GetPhysicsDebugFrame() const noexcept;

private:
    CharacterController characterController_;
    PlayerSnapshot snapshot_;
    float eyeHeight_ = 1.45f;
    float orbitTargetHeight_ = 0.95f;

    void RefreshDerivedPoints();
};
