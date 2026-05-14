#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "InputState.h"
#include "MotionState.h"
#include "physics/controller/CharacterController.h"

struct PlayerWalkableSurface
{
    std::string name;
    glm::vec3 center { 0.0f };
    glm::vec3 halfExtents { 0.5f };
};

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
    std::string supportSurfaceName;
};

class PlayerController
{
public:
    PlayerController();

    void SetWalkableWorld(IWalkableWorld* world);
    void SetZoneTwoWalkableSurfaces(std::vector<PlayerWalkableSurface> surfaces);
    void Update(const InputState& input, float movementYawDegrees);
    void SetSpawn(const glm::vec3& position, float facingYawDegrees);

    [[nodiscard]] const PlayerSnapshot& GetSnapshot() const noexcept;
    [[nodiscard]] glm::vec3 GetPosition() const noexcept;
    [[nodiscard]] glm::vec3 GetEyePosition() const noexcept;
    [[nodiscard]] glm::vec3 GetOrbitTarget() const noexcept;
    [[nodiscard]] const PhysicsDebugFrame& GetPhysicsDebugFrame() const noexcept;
    [[nodiscard]] std::vector<std::string> BuildDebugLines() const;

private:
    enum class ZoneTwoVerticalMode
    {
        Grounded,
        Jumping,
        Falling
    };

    CharacterController characterController_;
    std::vector<PlayerWalkableSurface> zoneTwoWalkableSurfaces_;
    PlayerSnapshot snapshot_;
    float eyeHeight_ = 1.45f;
    float orbitTargetHeight_ = 0.95f;
    float customVerticalVelocity_ = 0.0f;
    float zoneTwoJumpSnapLockTimer_ = 0.0f;
    bool zoneTwoGroundHit_ = false;
    bool zoneTwoLandingCandidate_ = false;
    ZoneTwoVerticalMode zoneTwoVerticalMode_ = ZoneTwoVerticalMode::Grounded;
    std::string currentZoneTwoSurfaceName_;

    void RefreshDerivedPoints();
    void UpdateZoneTwoPlatforming(const InputState& input, float movementYawDegrees, bool sprinting);
    [[nodiscard]] bool ProbeZoneTwoSupport(const glm::vec3& position, float snapDistance, float& groundY, std::string& surfaceName) const;
    [[nodiscard]] bool SweepZoneTwoLanding(float previousY, float currentY, const glm::vec3& position, float& groundY, std::string& surfaceName) const;
};
