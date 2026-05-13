#include "PlayerController.h"

#include <cmath>

#include <glm/glm.hpp>

namespace
{
constexpr glm::vec3 kWorldUp(0.0f, 1.0f, 0.0f);
}

PlayerController::PlayerController()
{
    eyeHeight_ = characterController_.GetParams().eyeHeight;
    orbitTargetHeight_ = characterController_.GetParams().orbitTargetHeight;
    RefreshDerivedPoints();
}

void PlayerController::SetWalkableWorld(IWalkableWorld* world)
{
    characterController_.SetWalkableWorld(world);
}

void PlayerController::SetSpawn(const glm::vec3& position, float facingYawDegrees)
{
    characterController_.SetSpawn(position);
    snapshot_.position = position;
    snapshot_.facingYawDegrees = facingYawDegrees;
    snapshot_.velocity = glm::vec3(0.0f);
    snapshot_.horizontalSpeed = 0.0f;
    snapshot_.motionState = MotionState::Idle;
    snapshot_.grounded = true;
    snapshot_.sprinting = false;
    RefreshDerivedPoints();
}

void PlayerController::Update(const InputState& input, float movementYawDegrees)
{
    const bool sprinting = input.IsKeyDown(GLFW_KEY_LEFT_SHIFT) || input.IsKeyDown(GLFW_KEY_RIGHT_SHIFT);
    snapshot_.sprinting = sprinting;

    const float yawRadians = glm::radians(movementYawDegrees);
    const glm::vec3 forward(std::cos(yawRadians), 0.0f, std::sin(yawRadians));
    const glm::vec3 right = glm::normalize(glm::cross(forward, kWorldUp));

    glm::vec3 movementDirection(0.0f);
    if (input.IsKeyDown(GLFW_KEY_W))
    {
        movementDirection += forward;
    }

    if (input.IsKeyDown(GLFW_KEY_S))
    {
        movementDirection -= forward;
    }

    if (input.IsKeyDown(GLFW_KEY_D))
    {
        movementDirection += right;
    }

    if (input.IsKeyDown(GLFW_KEY_A))
    {
        movementDirection -= right;
    }

    if (glm::dot(movementDirection, movementDirection) > 0.0f)
    {
        movementDirection = glm::normalize(movementDirection);
        snapshot_.facingYawDegrees = glm::degrees(std::atan2(movementDirection.z, movementDirection.x));
    }

    CharacterMoveRequest request;
    request.wishMove = movementDirection;
    request.wishMoveScale = glm::length(movementDirection) > 0.0f ? 1.0f : 0.0f;
    request.sprintRequested = sprinting;
    request.jumpRequested = input.WasKeyPressed(GLFW_KEY_SPACE);
    request.ledgeFallAllowed = snapshot_.position.x > 48.0f;
    request.dt = input.deltaTime;
    characterController_.Update(request);

    const CharacterState& characterState = characterController_.GetState();
    snapshot_.position = characterState.position;
    snapshot_.velocity = characterState.velocity;
    snapshot_.grounded = characterState.grounded;
    snapshot_.horizontalSpeed = glm::length(glm::vec2(characterState.velocity.x, characterState.velocity.z));

    if (!snapshot_.grounded)
    {
        snapshot_.motionState = MotionState::Airborne;
    }
    else if (snapshot_.horizontalSpeed > 0.1f)
    {
        snapshot_.motionState = sprinting ? MotionState::Run : MotionState::Walk;
    }
    else
    {
        snapshot_.motionState = MotionState::Idle;
    }

    RefreshDerivedPoints();
}

const PlayerSnapshot& PlayerController::GetSnapshot() const noexcept
{
    return snapshot_;
}

glm::vec3 PlayerController::GetPosition() const noexcept
{
    return snapshot_.position;
}

glm::vec3 PlayerController::GetEyePosition() const noexcept
{
    return snapshot_.eyePosition;
}

glm::vec3 PlayerController::GetOrbitTarget() const noexcept
{
    return snapshot_.orbitTarget;
}

const PhysicsDebugFrame& PlayerController::GetPhysicsDebugFrame() const noexcept
{
    return characterController_.GetDebugFrame();
}

void PlayerController::RefreshDerivedPoints()
{
    snapshot_.eyePosition = snapshot_.position + glm::vec3(0.0f, eyeHeight_, 0.0f);
    snapshot_.orbitTarget = snapshot_.position + glm::vec3(0.0f, orbitTargetHeight_, 0.0f);
}
