#include "PlayerController.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <sstream>

#include <glm/glm.hpp>

namespace
{
constexpr glm::vec3 kWorldUp(0.0f, 1.0f, 0.0f);

glm::vec3 ApproachVector(const glm::vec3& current, const glm::vec3& target, float maxDelta)
{
    const glm::vec3 delta = target - current;
    const float length = glm::length(delta);
    if (length <= maxDelta || length <= 0.0001f)
    {
        return target;
    }
    return current + ((delta / length) * maxDelta);
}
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

void PlayerController::SetZoneTwoWalkableSurfaces(std::vector<PlayerWalkableSurface> surfaces)
{
    zoneTwoWalkableSurfaces_ = std::move(surfaces);
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
    customVerticalVelocity_ = 0.0f;
    zoneTwoJumpSnapLockTimer_ = 0.0f;
    zoneTwoGroundHit_ = true;
    zoneTwoLandingCandidate_ = false;
    zoneTwoVerticalMode_ = ZoneTwoVerticalMode::Grounded;
    currentZoneTwoSurfaceName_.clear();
    snapshot_.supportSurfaceName.clear();
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

    if (snapshot_.position.x > 48.0f && !zoneTwoWalkableSurfaces_.empty())
    {
        UpdateZoneTwoPlatforming(input, movementYawDegrees, sprinting);
        return;
    }

    currentZoneTwoSurfaceName_.clear();
    snapshot_.supportSurfaceName.clear();

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

void PlayerController::UpdateZoneTwoPlatforming(const InputState& input, float movementYawDegrees, bool sprinting)
{
    const float dt = std::clamp(input.deltaTime, 0.0f, 0.1f);
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

    constexpr float kMaxSpeed = 3.2f;
    constexpr float kSprintMultiplier = 1.75f;
    constexpr float kAcceleration = 18.0f;
    constexpr float kAirControl = 0.48f;
    constexpr float kJumpSpeed = 6.8f;
    constexpr float kGravity = 15.5f;
    constexpr float kGroundSnapDistanceWalking = 0.09f;
    constexpr float kJumpSnapLockSeconds = 0.18f;

    const bool jumpStartedThisFrame = input.WasKeyPressed(GLFW_KEY_SPACE) && snapshot_.grounded;
    if (jumpStartedThisFrame)
    {
        customVerticalVelocity_ = kJumpSpeed;
        snapshot_.grounded = false;
        zoneTwoVerticalMode_ = ZoneTwoVerticalMode::Jumping;
        zoneTwoJumpSnapLockTimer_ = kJumpSnapLockSeconds;
        currentZoneTwoSurfaceName_.clear();
        snapshot_.supportSurfaceName.clear();
    }

    const float targetSpeed = kMaxSpeed * (sprinting ? kSprintMultiplier : 1.0f);
    const glm::vec3 targetHorizontalVelocity = movementDirection * targetSpeed;
    glm::vec3 horizontalVelocity(snapshot_.velocity.x, 0.0f, snapshot_.velocity.z);
    horizontalVelocity = ApproachVector(
        horizontalVelocity,
        targetHorizontalVelocity,
        kAcceleration * (snapshot_.grounded ? 1.0f : kAirControl) * dt);

    const glm::vec3 previousPosition = snapshot_.position;
    const float previousY = snapshot_.position.y;
    snapshot_.position.x += horizontalVelocity.x * dt;
    snapshot_.position.z += horizontalVelocity.z * dt;
    zoneTwoGroundHit_ = false;
    zoneTwoLandingCandidate_ = false;

    if (!snapshot_.grounded)
    {
        snapshot_.supportSurfaceName.clear();
        if (zoneTwoJumpSnapLockTimer_ > 0.0f)
        {
            zoneTwoJumpSnapLockTimer_ = std::max(0.0f, zoneTwoJumpSnapLockTimer_ - dt);
        }

        snapshot_.position.y += customVerticalVelocity_ * dt;
        customVerticalVelocity_ -= kGravity * dt;

        // A jump is allowed to finish its upward arc without any support probe or snap.
        if (customVerticalVelocity_ <= 0.0f && zoneTwoJumpSnapLockTimer_ <= 0.0f)
        {
            zoneTwoVerticalMode_ = ZoneTwoVerticalMode::Falling;
        }

        float landingY = 0.0f;
        std::string surfaceName;
        if (customVerticalVelocity_ <= 0.0f
            && zoneTwoJumpSnapLockTimer_ <= 0.0f
            && SweepZoneTwoLanding(previousY, snapshot_.position.y, snapshot_.position, landingY, surfaceName))
        {
            snapshot_.position.y = landingY;
            customVerticalVelocity_ = 0.0f;
            snapshot_.grounded = true;
            zoneTwoVerticalMode_ = ZoneTwoVerticalMode::Grounded;
            zoneTwoLandingCandidate_ = true;
            zoneTwoGroundHit_ = true;
            currentZoneTwoSurfaceName_ = surfaceName;
            snapshot_.supportSurfaceName = surfaceName;
        }
    }
    else
    {
        float landingY = 0.0f;
        std::string surfaceName;
        if (ProbeZoneTwoSupport(snapshot_.position, kGroundSnapDistanceWalking, landingY, surfaceName))
        {
            snapshot_.position.y = landingY;
            customVerticalVelocity_ = 0.0f;
            snapshot_.grounded = true;
            zoneTwoVerticalMode_ = ZoneTwoVerticalMode::Grounded;
            zoneTwoGroundHit_ = true;
            currentZoneTwoSurfaceName_ = surfaceName;
            snapshot_.supportSurfaceName = surfaceName;
        }
        else
        {
            snapshot_.grounded = false;
            customVerticalVelocity_ = 0.0f;
            zoneTwoVerticalMode_ = ZoneTwoVerticalMode::Falling;
            currentZoneTwoSurfaceName_.clear();
            snapshot_.supportSurfaceName.clear();
        }
    }

    snapshot_.velocity = dt > 0.0f
        ? (snapshot_.position - previousPosition) / dt
        : glm::vec3(0.0f);
    snapshot_.horizontalSpeed = glm::length(glm::vec2(snapshot_.velocity.x, snapshot_.velocity.z));
    snapshot_.sprinting = sprinting;
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

bool PlayerController::ProbeZoneTwoSupport(const glm::vec3& position, float snapDistance, float& groundY, std::string& surfaceName) const
{
    constexpr float kProbeRadius = 0.28f;
    const std::array<glm::vec2, 5> probes {
        glm::vec2(0.0f, 0.0f),
        glm::vec2(0.0f, kProbeRadius),
        glm::vec2(0.0f, -kProbeRadius),
        glm::vec2(kProbeRadius, 0.0f),
        glm::vec2(-kProbeRadius, 0.0f)
    };

    bool found = false;
    float bestY = -10000.0f;
    std::string bestName;
    for (const PlayerWalkableSurface& surface : zoneTwoWalkableSurfaces_)
    {
        const float topY = surface.center.y + surface.halfExtents.y;
        const float verticalDistance = std::abs(position.y - topY);
        if (verticalDistance > snapDistance)
        {
            continue;
        }

        int supportedProbeCount = 0;
        for (const glm::vec2& probe : probes)
        {
            const glm::vec3 probePoint(position.x + probe.x, position.y, position.z + probe.y);
            const bool insideX = probePoint.x >= surface.center.x - surface.halfExtents.x
                && probePoint.x <= surface.center.x + surface.halfExtents.x;
            const bool insideZ = probePoint.z >= surface.center.z - surface.halfExtents.z
                && probePoint.z <= surface.center.z + surface.halfExtents.z;
            if (insideX && insideZ)
            {
                supportedProbeCount += 1;
            }
        }

        if (supportedProbeCount >= 1 && topY > bestY)
        {
            bestY = topY;
            bestName = surface.name;
            found = true;
        }
    }

    if (found)
    {
        groundY = bestY;
        surfaceName = bestName;
    }
    return found;
}

bool PlayerController::SweepZoneTwoLanding(float previousY, float currentY, const glm::vec3& position, float& groundY, std::string& surfaceName) const
{
    constexpr float kFootRadius = 0.30f;
    constexpr float kLandingTolerance = 0.06f;
    bool found = false;
    float bestY = -10000.0f;
    std::string bestName;

    for (const PlayerWalkableSurface& surface : zoneTwoWalkableSurfaces_)
    {
        const float topY = surface.center.y + surface.halfExtents.y;
        if (previousY + kLandingTolerance < topY || currentY - kLandingTolerance > topY)
        {
            continue;
        }

        const bool overlapsX = position.x + kFootRadius >= surface.center.x - surface.halfExtents.x
            && position.x - kFootRadius <= surface.center.x + surface.halfExtents.x;
        const bool overlapsZ = position.z + kFootRadius >= surface.center.z - surface.halfExtents.z
            && position.z - kFootRadius <= surface.center.z + surface.halfExtents.z;
        if (overlapsX && overlapsZ && topY > bestY)
        {
            bestY = topY;
            bestName = surface.name;
            found = true;
        }
    }

    if (found)
    {
        groundY = bestY;
        surfaceName = bestName;
    }
    return found;
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

std::vector<std::string> PlayerController::BuildDebugLines() const
{
    const char* verticalMode = "GROUND";
    if (zoneTwoVerticalMode_ == ZoneTwoVerticalMode::Jumping)
    {
        verticalMode = "JUMP";
    }
    else if (zoneTwoVerticalMode_ == ZoneTwoVerticalMode::Falling)
    {
        verticalMode = "FALL";
    }

    auto formatFloat = [](float value)
    {
        std::ostringstream stream;
        stream << std::fixed << std::setprecision(2) << value;
        return stream.str();
    };

    std::vector<std::string> lines;
    lines.push_back(std::string("VERT ") + verticalMode);
    lines.push_back(std::string("GROUNDED ") + (snapshot_.grounded ? "YES" : "NO"));
    lines.push_back(std::string("AIRBORNE ") + (!snapshot_.grounded ? "YES" : "NO"));
    lines.push_back("VY " + formatFloat(customVerticalVelocity_));
    lines.push_back("SNAPLOCK " + formatFloat(zoneTwoJumpSnapLockTimer_));
    lines.push_back(std::string("GROUND HIT ") + (zoneTwoGroundHit_ ? "YES" : "NO"));
    lines.push_back(std::string("LANDING HIT ") + (zoneTwoLandingCandidate_ ? "YES" : "NO"));
    lines.push_back(std::string("NAV CLAMP ") + (snapshot_.position.x > 48.0f ? "NO" : "YES"));
    if (!currentZoneTwoSurfaceName_.empty())
    {
        lines.push_back("SURFACE " + currentZoneTwoSurfaceName_);
    }
    return lines;
}

void PlayerController::RefreshDerivedPoints()
{
    snapshot_.eyePosition = snapshot_.position + glm::vec3(0.0f, eyeHeight_, 0.0f);
    snapshot_.orbitTarget = snapshot_.position + glm::vec3(0.0f, orbitTargetHeight_, 0.0f);
}
