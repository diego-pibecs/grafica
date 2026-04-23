#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "physics/core/PhysicsTypes.h"

struct WalkableBuildSettings
{
    float cellSize = 0.12f;
    float cellHeight = 0.08f;
    float agentHeight = 1.80f;
    float agentRadius = 0.35f;
    float agentMaxClimb = 0.30f;
    float agentMaxSlopeDegrees = 48.0f;
    float edgeMaxLen = 12.0f;
    float edgeMaxError = 1.3f;
    float regionMinSize = 8.0f;
    float regionMergeSize = 20.0f;
    float detailSampleDist = 6.0f;
    float detailSampleMaxError = 1.0f;
    int vertsPerPoly = 6;
};

struct WalkableSample
{
    bool valid = false;
    glm::vec3 position { 0.0f };
    std::uint64_t polyRef = 0;
};

struct WalkableMoveResult
{
    bool valid = false;
    glm::vec3 position { 0.0f };
    std::uint64_t polyRef = 0;
    bool blocked = false;
    glm::vec3 wallPoint { 0.0f };
    glm::vec3 wallNormal { 0.0f, 1.0f, 0.0f };
};

struct WalkablePushResult
{
    bool valid = false;
    bool adjusted = false;
    glm::vec3 position { 0.0f };
    std::uint64_t polyRef = 0;
    glm::vec3 pushFrom { 0.0f };
    glm::vec3 pushNormal { 0.0f, 1.0f, 0.0f };
    std::string blockerName;
};

struct WalkableBlocker
{
    std::string name;
    glm::vec3 center { 0.0f };
    glm::vec3 halfExtents { 0.5f };
    float yawDegrees = 0.0f;
    bool enabled = true;
};

class IWalkableWorld
{
public:
    virtual ~IWalkableWorld() = default;

    [[nodiscard]] virtual bool IsReady() const = 0;
    [[nodiscard]] virtual const WalkableBuildSettings& GetBuildSettings() const = 0;
    virtual bool SamplePosition(const glm::vec3& desiredPosition, float searchRadius, WalkableSample& sample) const = 0;
    virtual bool MoveAlongSurface(
        std::uint64_t startPolyRef,
        const glm::vec3& startPosition,
        const glm::vec3& desiredPosition,
        WalkableMoveResult& result) const = 0;
    virtual bool ResolveDynamicBlockers(
        const glm::vec3& currentPosition,
        float maxPushDistance,
        WalkablePushResult& result) const = 0;
    virtual void SetDynamicBlockers(std::vector<WalkableBlocker> blockers) = 0;

    [[nodiscard]] virtual PhysicsDebugFrame BuildDebugFrame() const = 0;
};
