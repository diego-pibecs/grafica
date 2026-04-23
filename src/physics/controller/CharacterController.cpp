#include "physics/controller/CharacterController.h"

#include <algorithm>

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

CharacterController::CharacterController()
{
}

void CharacterController::SetWalkableWorld(IWalkableWorld* world)
{
    walkableWorld_ = world;
    if (walkableWorld_ != nullptr && walkableWorld_->IsReady())
    {
        params_.radius = walkableWorld_->GetBuildSettings().agentRadius;
        params_.height = walkableWorld_->GetBuildSettings().agentHeight;
        SnapToWalkable(std::max(params_.radius * 4.0f, 1.25f));
    }
    else
    {
        currentPolyRef_ = 0;
    }
}

void CharacterController::SetSpawn(const glm::vec3& position)
{
    state_.position = position;
    state_.velocity = glm::vec3(0.0f);
    state_.grounded = true;
    state_.groundNormal = kWorldUp;
    state_.slopeBlocked = false;
    state_.lastHits.clear();
    currentPolyRef_ = 0;
    groundHeight_ = position.y;
    verticalVelocity_ = 0.0f;
    SnapToWalkable(std::max(params_.radius * 4.0f, 1.25f));
}

const CharacterState& CharacterController::GetState() const noexcept
{
    return state_;
}

const CharacterParams& CharacterController::GetParams() const noexcept
{
    return params_;
}

const PhysicsDebugFrame& CharacterController::GetDebugFrame() const noexcept
{
    return debugFrame_;
}

void CharacterController::AppendCapsuleDebug(const glm::vec3& basePosition, const glm::vec3& color)
{
    const float centerDistance = std::max(params_.height - (2.0f * params_.radius), 0.0f);
    const float bottomCenterY = basePosition.y + params_.radius;
    const float topCenterY = bottomCenterY + centerDistance;

    auto addLine = [this, &color](const glm::vec3& start, const glm::vec3& end)
    {
        debugFrame_.lines.push_back(PhysicsDebugLine { start, end, color, color });
    };

    addLine(
        glm::vec3(basePosition.x, bottomCenterY, basePosition.z),
        glm::vec3(basePosition.x, topCenterY, basePosition.z));
    addLine(
        glm::vec3(basePosition.x - params_.radius, bottomCenterY, basePosition.z),
        glm::vec3(basePosition.x - params_.radius, topCenterY, basePosition.z));
    addLine(
        glm::vec3(basePosition.x + params_.radius, bottomCenterY, basePosition.z),
        glm::vec3(basePosition.x + params_.radius, topCenterY, basePosition.z));
    addLine(
        glm::vec3(basePosition.x, bottomCenterY, basePosition.z - params_.radius),
        glm::vec3(basePosition.x, topCenterY, basePosition.z - params_.radius));
    addLine(
        glm::vec3(basePosition.x, bottomCenterY, basePosition.z + params_.radius),
        glm::vec3(basePosition.x, topCenterY, basePosition.z + params_.radius));
}

void CharacterController::AppendRayDebug(const glm::vec3& start, const glm::vec3& end, const glm::vec3& color)
{
    debugFrame_.lines.push_back(PhysicsDebugLine { start, end, color, color });
}

bool CharacterController::SnapToWalkable(float searchRadius)
{
    if (walkableWorld_ == nullptr || !walkableWorld_->IsReady())
    {
        return false;
    }

    WalkableSample sample;
    if (!walkableWorld_->SamplePosition(state_.position, searchRadius, sample) || !sample.valid)
    {
        currentPolyRef_ = 0;
        return false;
    }

    state_.position = sample.position;
    state_.grounded = true;
    state_.groundNormal = kWorldUp;
    groundHeight_ = sample.position.y;
    verticalVelocity_ = 0.0f;
    currentPolyRef_ = sample.polyRef;
    return true;
}

void CharacterController::Update(const CharacterMoveRequest& request)
{
    debugFrame_.Clear();
    state_.lastHits.clear();
    AppendCapsuleDebug(state_.position, glm::vec3(0.3f, 0.8f, 1.0f));

    if (request.dt <= 0.0f)
    {
        return;
    }

    const glm::vec3 normalizedWish = glm::dot(request.wishMove, request.wishMove) > 0.000001f
        ? glm::normalize(request.wishMove)
        : glm::vec3(0.0f);
    const float targetSpeed = params_.maxSpeed
        * std::clamp(request.wishMoveScale, 0.0f, 1.0f)
        * (request.sprintRequested ? params_.sprintMultiplier : 1.0f);
    const glm::vec3 targetHorizontalVelocity = normalizedWish * targetSpeed;

    glm::vec3 horizontalVelocity(state_.velocity.x, 0.0f, state_.velocity.z);
    const float horizontalAcceleration = params_.acceleration * (state_.grounded ? 1.0f : params_.airControl);
    horizontalVelocity = ApproachVector(horizontalVelocity, targetHorizontalVelocity, horizontalAcceleration * request.dt);

    const glm::vec3 desiredDelta(horizontalVelocity.x * request.dt, 0.0f, horizontalVelocity.z * request.dt);
    const glm::vec3 navStartPosition(state_.position.x, groundHeight_, state_.position.z);
    const glm::vec3 desiredPosition = navStartPosition + desiredDelta;
    AppendRayDebug(
        navStartPosition + glm::vec3(0.0f, 0.08f, 0.0f),
        desiredPosition + glm::vec3(0.0f, 0.08f, 0.0f),
        glm::vec3(0.25f, 0.65f, 1.0f));

    if (request.jumpRequested && state_.grounded)
    {
        verticalVelocity_ = params_.jumpSpeed;
        state_.grounded = false;
    }

    if (walkableWorld_ == nullptr || !walkableWorld_->IsReady())
    {
        state_.position += desiredDelta;
        if (!state_.grounded)
        {
            verticalVelocity_ -= params_.gravity * request.dt;
            state_.position.y += verticalVelocity_ * request.dt;
            if (state_.position.y <= groundHeight_)
            {
                state_.position.y = groundHeight_;
                state_.grounded = true;
                verticalVelocity_ = 0.0f;
            }
        }
        else
        {
            state_.position.y = groundHeight_;
        }
        state_.velocity = glm::vec3(horizontalVelocity.x, verticalVelocity_, horizontalVelocity.z);
        state_.groundNormal = kWorldUp;
        state_.slopeBlocked = false;
        AppendCapsuleDebug(state_.position, glm::vec3(1.0f, 0.85f, 0.25f));
        return;
    }

    if (currentPolyRef_ == 0 && !SnapToWalkable(std::max(params_.radius * 4.0f, 1.25f)))
    {
        state_.velocity = glm::vec3(0.0f);
        state_.grounded = false;
        AppendCapsuleDebug(state_.position, glm::vec3(1.0f, 0.35f, 0.25f));
        return;
    }

    WalkableMoveResult moveResult;
    if (!walkableWorld_->MoveAlongSurface(currentPolyRef_, navStartPosition, desiredPosition, moveResult) || !moveResult.valid)
    {
        SnapToWalkable(std::max(params_.radius * 4.0f, 1.25f));
        state_.velocity = glm::vec3(0.0f);
        state_.grounded = currentPolyRef_ != 0;
        AppendCapsuleDebug(state_.position, glm::vec3(1.0f, 0.35f, 0.25f));
        return;
    }

    const glm::vec3 previousPosition = state_.position;
    groundHeight_ = moveResult.position.y;
    currentPolyRef_ = moveResult.polyRef;
    state_.position.x = moveResult.position.x;
    state_.position.z = moveResult.position.z;

    if (!state_.grounded)
    {
        verticalVelocity_ -= params_.gravity * request.dt;
        state_.position.y += verticalVelocity_ * request.dt;
        if (state_.position.y <= groundHeight_)
        {
            state_.position.y = groundHeight_;
            state_.grounded = true;
            verticalVelocity_ = 0.0f;
        }
    }
    else
    {
        state_.position.y = groundHeight_;
        verticalVelocity_ = 0.0f;
    }

    WalkablePushResult pushResult;
    const glm::vec3 groundPosition(state_.position.x, groundHeight_, state_.position.z);
    const float maxPushDistance = params_.dynamicBlockerPushSpeed * request.dt;
    if (walkableWorld_->ResolveDynamicBlockers(groundPosition, maxPushDistance, pushResult)
        && pushResult.valid
        && pushResult.adjusted)
    {
        state_.position.x = pushResult.position.x;
        state_.position.z = pushResult.position.z;
        groundHeight_ = pushResult.position.y;
        if (pushResult.polyRef != 0)
        {
            currentPolyRef_ = pushResult.polyRef;
        }
        if (state_.grounded)
        {
            state_.position.y = groundHeight_;
        }

        CharacterContact contact;
        contact.point = pushResult.pushFrom;
        contact.normal = pushResult.pushNormal;
        contact.fraction = 0.0f;
        contact.colliderName = pushResult.blockerName;
        state_.lastHits.push_back(contact);
        debugFrame_.points.push_back(PhysicsDebugPoint { pushResult.pushFrom, glm::vec3(1.0f, 0.45f, 0.95f), 8.0f });
        debugFrame_.lines.push_back(PhysicsDebugLine {
            pushResult.pushFrom,
            pushResult.pushFrom + (glm::normalize(pushResult.pushNormal) * 0.8f),
            glm::vec3(1.0f, 0.45f, 0.95f),
            glm::vec3(0.45f, 1.0f, 1.0f)
        });
    }

    state_.velocity = request.dt > 0.0f
        ? glm::vec3(
            (state_.position.x - previousPosition.x) / request.dt,
            (state_.position.y - previousPosition.y) / request.dt,
            (state_.position.z - previousPosition.z) / request.dt)
        : glm::vec3(0.0f);
    state_.groundNormal = kWorldUp;
    state_.slopeBlocked = false;

    if (moveResult.blocked)
    {
        CharacterContact contact;
        contact.point = moveResult.wallPoint;
        contact.normal = moveResult.wallNormal;
        contact.fraction = glm::length(desiredDelta) > 0.000001f
            ? glm::clamp(glm::length(state_.position - previousPosition) / glm::length(desiredDelta), 0.0f, 1.0f)
            : 0.0f;
        contact.colliderName = "navmesh-wall";
        state_.lastHits.push_back(contact);
        debugFrame_.points.push_back(PhysicsDebugPoint { moveResult.wallPoint, glm::vec3(1.0f, 0.2f, 0.2f), 7.0f });
        debugFrame_.lines.push_back(PhysicsDebugLine {
            moveResult.wallPoint,
            moveResult.wallPoint + (glm::normalize(moveResult.wallNormal) * 0.7f),
            glm::vec3(1.0f, 0.2f, 0.2f),
            glm::vec3(1.0f, 0.8f, 0.2f)
        });
    }

    AppendRayDebug(
        previousPosition + glm::vec3(0.0f, 0.12f, 0.0f),
        state_.position + glm::vec3(0.0f, 0.12f, 0.0f),
        glm::vec3(0.15f, 1.0f, 0.35f));
    AppendCapsuleDebug(state_.position, glm::vec3(0.2f, 1.0f, 0.3f));
}
