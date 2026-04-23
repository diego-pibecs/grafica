#pragma once

#include <memory>
#include <vector>

#include <DetourNavMesh.h>
#include <DetourNavMeshQuery.h>

#include "import/ImportedGeometry.h"
#include "navigation/IWalkableWorld.h"

class RecastNavigationWorld final : public IWalkableWorld
{
public:
    RecastNavigationWorld();
    ~RecastNavigationWorld() override;

    RecastNavigationWorld(const RecastNavigationWorld&) = delete;
    RecastNavigationWorld& operator=(const RecastNavigationWorld&) = delete;

    bool Build(
        const std::vector<ImportedModelAsset>& importedAssets,
        const std::vector<StaticRegionDesc>& staticRegions,
        const WalkableBuildSettings& buildSettings);

    [[nodiscard]] bool IsReady() const override;
    [[nodiscard]] const WalkableBuildSettings& GetBuildSettings() const override;
    bool SamplePosition(const glm::vec3& desiredPosition, float searchRadius, WalkableSample& sample) const override;
    bool MoveAlongSurface(
        std::uint64_t startPolyRef,
        const glm::vec3& startPosition,
        const glm::vec3& desiredPosition,
        WalkableMoveResult& result) const override;
    bool ResolveDynamicBlockers(
        const glm::vec3& currentPosition,
        float maxPushDistance,
        WalkablePushResult& result) const override;
    void SetDynamicBlockers(std::vector<WalkableBlocker> blockers) override;
    [[nodiscard]] PhysicsDebugFrame BuildDebugFrame() const override;

private:
    WalkableBuildSettings buildSettings_ {};
    dtNavMesh* navMesh_ = nullptr;
    dtNavMeshQuery* navQuery_ = nullptr;
    dtQueryFilter queryFilter_ {};
    PhysicsDebugFrame debugFrame_;
    std::vector<WalkableBlocker> dynamicBlockers_;

    void Clear();
};
