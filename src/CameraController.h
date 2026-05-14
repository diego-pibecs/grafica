#pragma once

#include <glm/glm.hpp>

#include "InputState.h"

#include <vector>

enum class CameraMode
{
    Fps,
    ThirdPerson
};

struct CameraSolidCollider
{
    glm::vec3 center { 0.0f };
    glm::vec3 halfExtents { 0.5f };
    bool enabled = true;
};

class CameraController
{
public:
    CameraController();

    void SetPlayerAnchor(const glm::vec3& anchor);
    void SetOrbitTarget(const glm::vec3& target);
    void SetOrbitBounds(const glm::vec3& minBounds, const glm::vec3& maxBounds);
    void SetCollisionColliders(std::vector<CameraSolidCollider> colliders);
    void ToggleMode();
    void Update(const InputState& input);

    [[nodiscard]] CameraMode GetMode() const noexcept;
    [[nodiscard]] glm::mat4 GetViewMatrix() const;
    [[nodiscard]] glm::vec3 GetPosition() const;
    [[nodiscard]] glm::vec3 GetForward() const;
    [[nodiscard]] float GetMovementYawDegrees() const noexcept;
    [[nodiscard]] float GetFovDegrees() const noexcept;

private:
    CameraMode mode_ = CameraMode::Fps;
    glm::vec3 playerAnchor_ { 0.0f, 1.45f, 6.0f };
    glm::vec3 orbitTarget_ { 0.0f, 1.0f, 0.0f };
    float fpsYaw_ = -90.0f;
    float fpsPitch_ = -10.0f;
    float thirdPersonYaw_ = 90.0f;
    float thirdPersonPitch_ = 18.0f;
    float thirdPersonRadius_ = 5.5f;
    float thirdPersonZoomSpeed_ = 1.2f;
    float mouseSensitivity_ = 0.12f;
    float keyboardLookSpeedDegrees_ = 110.0f;
    float fovDegrees_ = 45.0f;
    bool hasOrbitBounds_ = false;
    glm::vec3 orbitBoundsMin_ { -1000.0f, -1000.0f, -1000.0f };
    glm::vec3 orbitBoundsMax_ { 1000.0f, 1000.0f, 1000.0f };
    float orbitBoundsMargin_ = 0.35f;
    std::vector<CameraSolidCollider> collisionColliders_;
    mutable glm::vec3 smoothedThirdPersonPosition_ { 0.0f };
    mutable bool hasSmoothedThirdPersonPosition_ = false;

    void UpdateFps(const InputState& input);
    void UpdateThirdPerson(const InputState& input);
    void ClampPitch(float& pitch) const;

    [[nodiscard]] glm::vec3 GetFpsFront() const;
    [[nodiscard]] glm::vec3 GetThirdPersonPosition() const;
    [[nodiscard]] glm::vec3 ResolveThirdPersonCollision(const glm::vec3& desiredPosition) const;
};
