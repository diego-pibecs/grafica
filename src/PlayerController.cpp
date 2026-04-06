#include "PlayerController.h"

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>

namespace
{
constexpr glm::vec3 kWorldUp(0.0f, 1.0f, 0.0f);
}

PlayerController::PlayerController()
{
    RefreshDerivedPoints();
}

void PlayerController::Update(const InputState& input, float movementYawDegrees, const CollisionResolver& collisionResolver)
{
    const bool sprinting = input.IsKeyDown(GLFW_KEY_LEFT_SHIFT) || input.IsKeyDown(GLFW_KEY_RIGHT_SHIFT);
    snapshot_.sprinting = sprinting;
    const glm::vec3 previousPosition = snapshot_.position;

    const float yawRadians = glm::radians(movementYawDegrees);
    const glm::vec3 forward(std::cos(yawRadians), 0.0f, std::sin(yawRadians));
    const glm::vec3 right = glm::normalize(glm::cross(forward, kWorldUp));

    glm::vec3 movementDirection(0.0f);
    if (input.IsKeyDown(GLFW_KEY_W) || input.IsKeyDown(GLFW_KEY_UP))
    {
        movementDirection += forward;
    }

    if (input.IsKeyDown(GLFW_KEY_S) || input.IsKeyDown(GLFW_KEY_DOWN))
    {
        movementDirection -= forward;
    }

    if (input.IsKeyDown(GLFW_KEY_D) || input.IsKeyDown(GLFW_KEY_RIGHT))
    {
        movementDirection += right;
    }

    if (input.IsKeyDown(GLFW_KEY_A) || input.IsKeyDown(GLFW_KEY_LEFT))
    {
        movementDirection -= right;
    }

    if (glm::dot(movementDirection, movementDirection) > 0.0f)
    {
        movementDirection = glm::normalize(movementDirection);
        const float currentSpeed = sprinting ? walkSpeed_ * sprintMultiplier_ : walkSpeed_;
        const glm::vec3 desiredPosition = snapshot_.position + (movementDirection * currentSpeed * input.deltaTime);
        if (collisionResolver)
        {
            snapshot_.position = collisionResolver(snapshot_.position, desiredPosition, playerRadius_, playerHeight_);
        }
        else
        {
            snapshot_.position = desiredPosition;
        }
        snapshot_.facingYawDegrees = glm::degrees(std::atan2(movementDirection.z, movementDirection.x));
    }

    if (snapshot_.grounded && input.WasKeyPressed(GLFW_KEY_SPACE))
    {
        snapshot_.grounded = false;
        verticalVelocity_ = jumpVelocity_;
    }

    if (!snapshot_.grounded || verticalVelocity_ != 0.0f)
    {
        verticalVelocity_ += gravity_ * input.deltaTime;
        snapshot_.position.y += verticalVelocity_ * input.deltaTime;

        if (snapshot_.position.y <= groundHeight_)
        {
            snapshot_.position.y = groundHeight_;
            verticalVelocity_ = 0.0f;
            snapshot_.grounded = true;
        }
    }

    snapshot_.velocity = (input.deltaTime > 0.0f)
        ? (snapshot_.position - previousPosition) / input.deltaTime
        : glm::vec3(0.0f);
    snapshot_.horizontalSpeed = glm::length(glm::vec2(snapshot_.velocity.x, snapshot_.velocity.z));

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

void PlayerController::RefreshDerivedPoints()
{
    snapshot_.eyePosition = snapshot_.position + glm::vec3(0.0f, eyeHeight_, 0.0f);
    snapshot_.orbitTarget = snapshot_.position + glm::vec3(0.0f, orbitTargetHeight_, 0.0f);
}
