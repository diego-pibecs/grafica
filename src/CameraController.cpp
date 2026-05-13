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

    return position;
}
