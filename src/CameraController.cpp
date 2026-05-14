#include "CameraController.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

namespace
{
constexpr glm::vec3 kWorldUp(0.0f, 1.0f, 0.0f);

bool RayIntersectsAabb(
    const glm::vec3& origin,
    const glm::vec3& direction,
    float maxDistance,
    const CameraSolidCollider& collider,
    float& hitDistance)
{
    const glm::vec3 minBounds = collider.center - collider.halfExtents;
    const glm::vec3 maxBounds = collider.center + collider.halfExtents;
    if (origin.x >= minBounds.x && origin.x <= maxBounds.x
        && origin.y >= minBounds.y && origin.y <= maxBounds.y
        && origin.z >= minBounds.z && origin.z <= maxBounds.z)
    {
        return false;
    }

    float tMin = 0.0f;
    float tMax = maxDistance;
    for (int axis = 0; axis < 3; ++axis)
    {
        const float rayOrigin = origin[axis];
        const float rayDirection = direction[axis];
        const float minValue = minBounds[axis];
        const float maxValue = maxBounds[axis];
        if (std::abs(rayDirection) < 0.00001f)
        {
            if (rayOrigin < minValue || rayOrigin > maxValue)
            {
                return false;
            }
            continue;
        }

        float t1 = (minValue - rayOrigin) / rayDirection;
        float t2 = (maxValue - rayOrigin) / rayDirection;
        if (t1 > t2)
        {
            std::swap(t1, t2);
        }
        tMin = std::max(tMin, t1);
        tMax = std::min(tMax, t2);
        if (tMin > tMax)
        {
            return false;
        }
    }

    hitDistance = tMin;
    return hitDistance >= 0.0f && hitDistance <= maxDistance;
}
}

CameraController::CameraController()
{
}

void CameraController::SetPlayerAnchor(const glm::vec3& anchor)
{
    playerAnchor_ = anchor;
}

void CameraController::SetOrbitTarget(const glm::vec3& target)
{
    orbitTarget_ = target;
}

void CameraController::SetOrbitBounds(const glm::vec3& minBounds, const glm::vec3& maxBounds)
{
    orbitBoundsMin_ = minBounds;
    orbitBoundsMax_ = maxBounds;
    hasOrbitBounds_ = true;
}

void CameraController::SetCollisionColliders(std::vector<CameraSolidCollider> colliders)
{
    collisionColliders_ = std::move(colliders);
}

void CameraController::ToggleMode()
{
    if (mode_ == CameraMode::Fps)
    {
        thirdPersonYaw_ = fpsYaw_ + 180.0f;
        thirdPersonPitch_ = std::clamp(18.0f - (fpsPitch_ * 0.15f), 8.0f, 45.0f);
        mode_ = CameraMode::ThirdPerson;
        return;
    }

    fpsYaw_ = thirdPersonYaw_ + 180.0f;
    mode_ = CameraMode::Fps;
}

void CameraController::Update(const InputState& input)
{
    if (mode_ == CameraMode::Fps)
    {
        UpdateFps(input);
    }
    else
    {
        UpdateThirdPerson(input);
    }
}

CameraMode CameraController::GetMode() const noexcept
{
    return mode_;
}

glm::mat4 CameraController::GetViewMatrix() const
{
    const glm::vec3 position = GetPosition();
    const glm::vec3 forward = GetForward();
    return glm::lookAt(position, position + forward, kWorldUp);
}

glm::vec3 CameraController::GetPosition() const
{
    return mode_ == CameraMode::Fps ? playerAnchor_ : GetThirdPersonPosition();
}

glm::vec3 CameraController::GetForward() const
{
    if (mode_ == CameraMode::Fps)
    {
        return GetFpsFront();
    }

    return glm::normalize(orbitTarget_ - GetThirdPersonPosition());
}

float CameraController::GetFovDegrees() const noexcept
{
    return fovDegrees_;
}

float CameraController::GetMovementYawDegrees() const noexcept
{
    return mode_ == CameraMode::Fps ? fpsYaw_ : thirdPersonYaw_ + 180.0f;
}

void CameraController::UpdateFps(const InputState& input)
{
    if (input.mouseCaptured)
    {
        fpsYaw_ += static_cast<float>(input.mouseDeltaX) * mouseSensitivity_;
        fpsPitch_ += static_cast<float>(input.mouseDeltaY) * mouseSensitivity_;
    }

    float yawInput = 0.0f;
    float pitchInput = 0.0f;
    if (input.IsKeyDown(GLFW_KEY_LEFT))
    {
        yawInput -= 1.0f;
    }
    if (input.IsKeyDown(GLFW_KEY_RIGHT))
    {
        yawInput += 1.0f;
    }
    if (input.IsKeyDown(GLFW_KEY_UP))
    {
        pitchInput += 1.0f;
    }
    if (input.IsKeyDown(GLFW_KEY_DOWN))
    {
        pitchInput -= 1.0f;
    }

    const float lookDelta = keyboardLookSpeedDegrees_ * std::max(input.deltaTime, 1.0f / 60.0f);
    fpsYaw_ += yawInput * lookDelta;
    fpsPitch_ += pitchInput * lookDelta;
    ClampPitch(fpsPitch_);
}

void CameraController::UpdateThirdPerson(const InputState& input)
{
    if (input.mouseCaptured)
    {
        thirdPersonYaw_ += static_cast<float>(input.mouseDeltaX) * mouseSensitivity_;
        thirdPersonPitch_ -= static_cast<float>(input.mouseDeltaY) * mouseSensitivity_;
    }

    float yawInput = 0.0f;
    float pitchInput = 0.0f;
    if (input.IsKeyDown(GLFW_KEY_LEFT))
    {
        yawInput -= 1.0f;
    }
    if (input.IsKeyDown(GLFW_KEY_RIGHT))
    {
        yawInput += 1.0f;
    }
    if (input.IsKeyDown(GLFW_KEY_UP))
    {
        pitchInput += 1.0f;
    }
    if (input.IsKeyDown(GLFW_KEY_DOWN))
    {
        pitchInput -= 1.0f;
    }

    const float lookDelta = keyboardLookSpeedDegrees_ * std::max(input.deltaTime, 1.0f / 60.0f);
    thirdPersonYaw_ += yawInput * lookDelta;
    thirdPersonPitch_ += pitchInput * lookDelta;
    ClampPitch(thirdPersonPitch_);

    float keyboardZoomDirection = 0.0f;
    if (input.IsKeyDown(GLFW_KEY_EQUAL) || input.IsKeyDown(GLFW_KEY_KP_ADD))
    {
        keyboardZoomDirection += 1.0f;
    }
    if (input.IsKeyDown(GLFW_KEY_MINUS) || input.IsKeyDown(GLFW_KEY_KP_SUBTRACT))
    {
        keyboardZoomDirection -= 1.0f;
    }

    const float keyboardZoomAmount =
        keyboardZoomDirection * thirdPersonZoomSpeed_ * std::max(input.deltaTime, 1.0f / 60.0f) * 6.0f;

    thirdPersonRadius_ -= static_cast<float>(input.scrollDeltaY) * thirdPersonZoomSpeed_;
    thirdPersonRadius_ -= keyboardZoomAmount;
    thirdPersonRadius_ = std::clamp(thirdPersonRadius_, 2.5f, 20.0f);
}

void CameraController::ClampPitch(float& pitch) const
{
    pitch = std::clamp(pitch, -80.0f, 80.0f);
}

glm::vec3 CameraController::GetFpsFront() const
{
    glm::vec3 front;
    front.x = std::cos(glm::radians(fpsYaw_)) * std::cos(glm::radians(fpsPitch_));
    front.y = std::sin(glm::radians(fpsPitch_));
    front.z = std::sin(glm::radians(fpsYaw_)) * std::cos(glm::radians(fpsPitch_));
    return glm::normalize(front);
}

glm::vec3 CameraController::GetThirdPersonPosition() const
{
    const float yawRadians = glm::radians(thirdPersonYaw_);
    const float pitchRadians = glm::radians(thirdPersonPitch_);
    const float horizontalRadius = thirdPersonRadius_ * std::cos(pitchRadians);

    glm::vec3 position = orbitTarget_ + glm::vec3(
        horizontalRadius * std::cos(yawRadians),
        thirdPersonRadius_ * std::sin(pitchRadians),
        horizontalRadius * std::sin(yawRadians));

    if (hasOrbitBounds_)
    {
        position.x = std::clamp(position.x, orbitBoundsMin_.x + orbitBoundsMargin_, orbitBoundsMax_.x - orbitBoundsMargin_);
        position.y = std::clamp(position.y, orbitBoundsMin_.y + orbitBoundsMargin_, orbitBoundsMax_.y - orbitBoundsMargin_);
        position.z = std::clamp(position.z, orbitBoundsMin_.z + orbitBoundsMargin_, orbitBoundsMax_.z - orbitBoundsMargin_);
    }

    position = ResolveThirdPersonCollision(position);
    if (!hasSmoothedThirdPersonPosition_)
    {
        smoothedThirdPersonPosition_ = position;
        hasSmoothedThirdPersonPosition_ = true;
    }
    smoothedThirdPersonPosition_ = glm::mix(smoothedThirdPersonPosition_, position, 0.32f);
    return smoothedThirdPersonPosition_;
}

glm::vec3 CameraController::ResolveThirdPersonCollision(const glm::vec3& desiredPosition) const
{
    const glm::vec3 boom = desiredPosition - orbitTarget_;
    const float boomDistance = glm::length(boom);
    if (boomDistance <= 0.0001f || collisionColliders_.empty())
    {
        return desiredPosition;
    }

    const glm::vec3 direction = boom / boomDistance;
    float nearestHit = boomDistance;
    bool hit = false;
    for (const CameraSolidCollider& collider : collisionColliders_)
    {
        if (!collider.enabled)
        {
            continue;
        }

        float hitDistance = 0.0f;
        if (RayIntersectsAabb(orbitTarget_, direction, boomDistance, collider, hitDistance)
            && hitDistance < nearestHit)
        {
            nearestHit = hitDistance;
            hit = true;
        }
    }

    if (!hit)
    {
        return desiredPosition;
    }

    constexpr float kCollisionMargin = 0.38f;
    constexpr float kMinDistance = 1.35f;
    return orbitTarget_ + (direction * std::max(nearestHit - kCollisionMargin, kMinDistance));
}
