#include "CameraController.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

namespace
{
constexpr glm::vec3 kWorldUp(0.0f, 1.0f, 0.0f);
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

void CameraController::ToggleMode()
{
    if (mode_ == CameraMode::Fps)
    {
        orbitYaw_ = fpsYaw_ + 180.0f;
        orbitPitch_ = std::clamp(18.0f - (fpsPitch_ * 0.15f), 8.0f, 45.0f);
        mode_ = CameraMode::Orbit;
        return;
    }

    fpsYaw_ = orbitYaw_ + 180.0f;
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
        UpdateOrbit(input);
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
    return mode_ == CameraMode::Fps ? playerAnchor_ : GetOrbitPosition();
}

glm::vec3 CameraController::GetForward() const
{
    if (mode_ == CameraMode::Fps)
    {
        return GetFpsFront();
    }

    return glm::normalize(orbitTarget_ - GetOrbitPosition());
}

float CameraController::GetFovDegrees() const noexcept
{
    return fovDegrees_;
}

float CameraController::GetMovementYawDegrees() const noexcept
{
    return mode_ == CameraMode::Fps ? fpsYaw_ : orbitYaw_ + 180.0f;
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
    if (input.IsKeyDown(GLFW_KEY_J))
    {
        yawInput -= 1.0f;
    }
    if (input.IsKeyDown(GLFW_KEY_L))
    {
        yawInput += 1.0f;
    }
    if (input.IsKeyDown(GLFW_KEY_I))
    {
        pitchInput += 1.0f;
    }
    if (input.IsKeyDown(GLFW_KEY_K))
    {
        pitchInput -= 1.0f;
    }

    const float lookDelta = keyboardLookSpeedDegrees_ * std::max(input.deltaTime, 1.0f / 60.0f);
    fpsYaw_ += yawInput * lookDelta;
    fpsPitch_ += pitchInput * lookDelta;
    ClampPitch(fpsPitch_);
}

void CameraController::UpdateOrbit(const InputState& input)
{
    if (input.mouseCaptured)
    {
        orbitYaw_ += static_cast<float>(input.mouseDeltaX) * mouseSensitivity_;
        orbitPitch_ -= static_cast<float>(input.mouseDeltaY) * mouseSensitivity_;
    }

    float yawInput = 0.0f;
    float pitchInput = 0.0f;
    if (input.IsKeyDown(GLFW_KEY_J))
    {
        yawInput -= 1.0f;
    }
    if (input.IsKeyDown(GLFW_KEY_L))
    {
        yawInput += 1.0f;
    }
    if (input.IsKeyDown(GLFW_KEY_I))
    {
        pitchInput += 1.0f;
    }
    if (input.IsKeyDown(GLFW_KEY_K))
    {
        pitchInput -= 1.0f;
    }

    const float lookDelta = keyboardLookSpeedDegrees_ * std::max(input.deltaTime, 1.0f / 60.0f);
    orbitYaw_ += yawInput * lookDelta;
    orbitPitch_ += pitchInput * lookDelta;
    ClampPitch(orbitPitch_);

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
        keyboardZoomDirection * orbitZoomSpeed_ * std::max(input.deltaTime, 1.0f / 60.0f) * 6.0f;

    orbitRadius_ -= static_cast<float>(input.scrollDeltaY) * orbitZoomSpeed_;
    orbitRadius_ -= keyboardZoomAmount;
    orbitRadius_ = std::clamp(orbitRadius_, 2.5f, 20.0f);
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

glm::vec3 CameraController::GetOrbitPosition() const
{
    const float yawRadians = glm::radians(orbitYaw_);
    const float pitchRadians = glm::radians(orbitPitch_);
    const float horizontalRadius = orbitRadius_ * std::cos(pitchRadians);

    glm::vec3 position = orbitTarget_ + glm::vec3(
        horizontalRadius * std::cos(yawRadians),
        orbitRadius_ * std::sin(pitchRadians),
        horizontalRadius * std::sin(yawRadians));

    if (hasOrbitBounds_)
    {
        position.x = std::clamp(position.x, orbitBoundsMin_.x + orbitBoundsMargin_, orbitBoundsMax_.x - orbitBoundsMargin_);
        position.y = std::clamp(position.y, orbitBoundsMin_.y + orbitBoundsMargin_, orbitBoundsMax_.y - orbitBoundsMargin_);
        position.z = std::clamp(position.z, orbitBoundsMin_.z + orbitBoundsMargin_, orbitBoundsMax_.z - orbitBoundsMargin_);
    }

    return position;
}
