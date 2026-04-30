#include "navigation/RecastNavigationWorld.h"

#include "DebugLog.h"

#include <DetourCommon.h>
#include <DetourNavMeshBuilder.h>
#include <Recast.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

namespace
{
constexpr unsigned short kWalkFlag = 0x1u;
constexpr unsigned char kGroundArea = 0x1u;
constexpr int kMaxVisitedPolys = 32;
constexpr int kMaxQueryNodes = 2048;

class RecastBuildContext final : public rcContext
{
public:
    RecastBuildContext()
        : rcContext(true)
    {
    }

protected:
    void doLog(const rcLogCategory category, const char* message, const int /*length*/) override
    {
        switch (category)
        {
        case RC_LOG_ERROR:
            DebugLog::Error("Recast", message);
            break;
        case RC_LOG_WARNING:
            DebugLog::Info("Recast", "warning: ", message);
            break;
        case RC_LOG_PROGRESS:
            DebugLog::Info("Recast", message);
            break;
        }
    }
};

struct RecastBuildScratch
{
    rcHeightfield* heightfield = nullptr;
    rcCompactHeightfield* compactHeightfield = nullptr;
    rcContourSet* contourSet = nullptr;
    rcPolyMesh* polyMesh = nullptr;
    rcPolyMeshDetail* detailMesh = nullptr;

    ~RecastBuildScratch()
    {
        if (detailMesh != nullptr)
        {
            rcFreePolyMeshDetail(detailMesh);
        }
        if (polyMesh != nullptr)
        {
            rcFreePolyMesh(polyMesh);
        }
        if (contourSet != nullptr)
        {
            rcFreeContourSet(contourSet);
        }
        if (compactHeightfield != nullptr)
        {
            rcFreeCompactHeightfield(compactHeightfield);
        }
        if (heightfield != nullptr)
        {
            rcFreeHeightField(heightfield);
        }
    }
};

struct CombinedGeometry
{
    std::vector<float> vertices;
    std::vector<int> indices;
    PhysicsAabb bounds;
};

struct BlockerHit
{
    bool hit = false;
    float fraction = 1.0f;
    glm::vec3 point { 0.0f };
    glm::vec3 normal { 0.0f, 1.0f, 0.0f };
    std::string name;
};

struct BlockerOverlap
{
    bool overlaps = false;
    float penetration = 0.0f;
    glm::vec3 normal { 0.0f, 0.0f, 1.0f };
    glm::vec3 closestPoint { 0.0f };
    std::string name;
};

PhysicsAabb MakeEmptyBounds()
{
    return PhysicsAabb {
        glm::vec3(std::numeric_limits<float>::max()),
        glm::vec3(-std::numeric_limits<float>::max())
    };
}

void ExpandBounds(PhysicsAabb& bounds, const glm::vec3& point)
{
    bounds.min = glm::min(bounds.min, point);
    bounds.max = glm::max(bounds.max, point);
}

glm::vec3 PolyMeshVertexToWorld(const rcPolyMesh& mesh, unsigned short vertexIndex)
{
    const unsigned short* vertex = &mesh.verts[vertexIndex * 3];
    return glm::vec3(
        mesh.bmin[0] + (static_cast<float>(vertex[0]) * mesh.cs),
        mesh.bmin[1] + (static_cast<float>(vertex[1] + 1u) * mesh.ch),
        mesh.bmin[2] + (static_cast<float>(vertex[2]) * mesh.cs));
}

CombinedGeometry BuildCombinedGeometry(
    const std::vector<ImportedModelAsset>& importedAssets,
    const std::vector<StaticRegionDesc>& staticRegions)
{
    CombinedGeometry geometry;
    geometry.bounds = MakeEmptyBounds();

    for (const StaticRegionDesc& region : staticRegions)
    {
        const std::uint32_t baseVertex = static_cast<std::uint32_t>(geometry.vertices.size() / 3u);
        for (const glm::vec3& vertex : region.vertices)
        {
            geometry.vertices.push_back(vertex.x);
            geometry.vertices.push_back(vertex.y);
            geometry.vertices.push_back(vertex.z);
            ExpandBounds(geometry.bounds, vertex);
        }
        for (std::uint32_t index : region.indices)
        {
            geometry.indices.push_back(static_cast<int>(baseVertex + index));
        }
    }

    for (const ImportedModelAsset& asset : importedAssets)
    {
        for (const ImportedSubmesh& submesh : asset.submeshes)
        {
            if (submesh.worldVertices.empty() || submesh.indices.empty())
            {
                continue;
            }

            const std::uint32_t baseVertex = static_cast<std::uint32_t>(geometry.vertices.size() / 3u);
            for (const glm::vec3& vertex : submesh.worldVertices)
            {
                geometry.vertices.push_back(vertex.x);
                geometry.vertices.push_back(vertex.y);
                geometry.vertices.push_back(vertex.z);
                ExpandBounds(geometry.bounds, vertex);
            }
            for (std::uint32_t index : submesh.indices)
            {
                geometry.indices.push_back(static_cast<int>(baseVertex + index));
            }
        }
    }

    return geometry;
}

PhysicsDebugFrame BuildNavMeshDebugFrame(const rcPolyMesh& polyMesh)
{
    PhysicsDebugFrame frame;
    const glm::vec3 walkableColor(0.10f, 0.88f, 0.48f);
    const glm::vec3 nullColor(0.18f, 0.18f, 0.20f);
    const glm::vec3 edgeColor(0.95f, 0.95f, 0.95f);
    const glm::vec3 innerEdgeColor(0.15f, 0.35f, 0.40f);
    const glm::vec3 vertexColor(0.96f, 0.28f, 0.28f);

    for (int polyIndex = 0; polyIndex < polyMesh.npolys; ++polyIndex)
    {
        const unsigned short* polygon = &polyMesh.polys[polyIndex * polyMesh.nvp * 2];
        const glm::vec3 color = polyMesh.areas[polyIndex] == RC_NULL_AREA ? nullColor : walkableColor;

        for (int vertexIndex = 2; vertexIndex < polyMesh.nvp; ++vertexIndex)
        {
            if (polygon[vertexIndex] == RC_MESH_NULL_IDX)
            {
                break;
            }

            const glm::vec3 a = PolyMeshVertexToWorld(polyMesh, polygon[0]);
            const glm::vec3 b = PolyMeshVertexToWorld(polyMesh, polygon[vertexIndex - 1]);
            const glm::vec3 c = PolyMeshVertexToWorld(polyMesh, polygon[vertexIndex]);
            frame.triangles.push_back(PhysicsDebugTriangle { a, b, c, color, color, color });
        }
    }

    for (int polyIndex = 0; polyIndex < polyMesh.npolys; ++polyIndex)
    {
        const unsigned short* polygon = &polyMesh.polys[polyIndex * polyMesh.nvp * 2];
        for (int edgeIndex = 0; edgeIndex < polyMesh.nvp; ++edgeIndex)
        {
            if (polygon[edgeIndex] == RC_MESH_NULL_IDX)
            {
                break;
            }

            if ((polygon[polyMesh.nvp + edgeIndex] & 0x8000) != 0)
            {
                continue;
            }

            const int nextIndex = (edgeIndex + 1 >= polyMesh.nvp || polygon[edgeIndex + 1] == RC_MESH_NULL_IDX) ? 0 : edgeIndex + 1;
            const glm::vec3 start = PolyMeshVertexToWorld(polyMesh, polygon[edgeIndex]) + glm::vec3(0.0f, 0.10f, 0.0f);
            const glm::vec3 end = PolyMeshVertexToWorld(polyMesh, polygon[nextIndex]) + glm::vec3(0.0f, 0.10f, 0.0f);
            frame.lines.push_back(PhysicsDebugLine { start, end, innerEdgeColor, innerEdgeColor });
        }
    }

    for (int polyIndex = 0; polyIndex < polyMesh.npolys; ++polyIndex)
    {
        const unsigned short* polygon = &polyMesh.polys[polyIndex * polyMesh.nvp * 2];
        for (int edgeIndex = 0; edgeIndex < polyMesh.nvp; ++edgeIndex)
        {
            if (polygon[edgeIndex] == RC_MESH_NULL_IDX)
            {
                break;
            }

            if ((polygon[polyMesh.nvp + edgeIndex] & 0x8000) == 0)
            {
                continue;
            }

            const int nextIndex = (edgeIndex + 1 >= polyMesh.nvp || polygon[edgeIndex + 1] == RC_MESH_NULL_IDX) ? 0 : edgeIndex + 1;
            const glm::vec3 start = PolyMeshVertexToWorld(polyMesh, polygon[edgeIndex]) + glm::vec3(0.0f, 0.12f, 0.0f);
            const glm::vec3 end = PolyMeshVertexToWorld(polyMesh, polygon[nextIndex]) + glm::vec3(0.0f, 0.12f, 0.0f);
            frame.lines.push_back(PhysicsDebugLine { start, end, edgeColor, edgeColor });
        }
    }

    for (int vertexIndex = 0; vertexIndex < polyMesh.nverts; ++vertexIndex)
    {
        frame.points.push_back(PhysicsDebugPoint {
            PolyMeshVertexToWorld(polyMesh, static_cast<unsigned short>(vertexIndex)) + glm::vec3(0.0f, 0.14f, 0.0f),
            vertexColor,
            4.0f
        });
    }

    return frame;
}

void SetRecastVector(float target[3], const glm::vec3& source)
{
    target[0] = source.x;
    target[1] = source.y;
    target[2] = source.z;
}

glm::vec3 FromRecastVector(const float source[3])
{
    return glm::vec3(source[0], source[1], source[2]);
}

glm::vec2 Rotate2D(const glm::vec2& value, float radians)
{
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    return glm::vec2((value.x * c) - (value.y * s), (value.x * s) + (value.y * c));
}

bool SegmentHitsExpandedBlocker(
    const glm::vec3& start,
    const glm::vec3& end,
    const WalkableBlocker& blocker,
    float agentRadius,
    float agentHeight,
    BlockerHit& hit)
{
    if (!blocker.enabled)
    {
        return false;
    }

    const float actorMinY = std::min(start.y, end.y);
    const float actorMaxY = std::max(start.y, end.y) + agentHeight;
    const float blockerMinY = blocker.center.y - blocker.halfExtents.y;
    const float blockerMaxY = blocker.center.y + blocker.halfExtents.y;
    if (actorMaxY < blockerMinY || actorMinY > blockerMaxY)
    {
        return false;
    }

    const float inverseYaw = -glm::radians(blocker.yawDegrees);
    const glm::vec2 center(blocker.center.x, blocker.center.z);
    const glm::vec2 localStart = Rotate2D(glm::vec2(start.x, start.z) - center, inverseYaw);
    const glm::vec2 localEnd = Rotate2D(glm::vec2(end.x, end.z) - center, inverseYaw);
    const glm::vec2 delta = localEnd - localStart;
    const glm::vec2 extents(
        std::max(blocker.halfExtents.x + agentRadius, agentRadius),
        std::max(blocker.halfExtents.z + agentRadius, agentRadius));

    float tMin = 0.0f;
    float tMax = 1.0f;
    glm::vec2 localNormal(0.0f);

    auto clipAxis = [&](float startValue, float deltaValue, float extent, const glm::vec2& negativeNormal, const glm::vec2& positiveNormal) -> bool
    {
        if (std::abs(deltaValue) < 0.000001f)
        {
            return startValue >= -extent && startValue <= extent;
        }

        float t1 = (-extent - startValue) / deltaValue;
        float t2 = (extent - startValue) / deltaValue;
        glm::vec2 enterNormal = negativeNormal;
        if (t1 > t2)
        {
            std::swap(t1, t2);
            enterNormal = positiveNormal;
        }

        if (t1 > tMin)
        {
            tMin = t1;
            localNormal = enterNormal;
        }
        tMax = std::min(tMax, t2);
        return tMin <= tMax;
    };

    if (!clipAxis(localStart.x, delta.x, extents.x, glm::vec2(-1.0f, 0.0f), glm::vec2(1.0f, 0.0f)))
    {
        return false;
    }
    if (!clipAxis(localStart.y, delta.y, extents.y, glm::vec2(0.0f, -1.0f), glm::vec2(0.0f, 1.0f)))
    {
        return false;
    }

    if (tMax < 0.0f || tMin > 1.0f)
    {
        return false;
    }

    if (glm::dot(localNormal, localNormal) < 0.000001f)
    {
        localNormal = glm::dot(delta, delta) > 0.000001f ? -glm::normalize(delta) : glm::vec2(0.0f, -1.0f);
    }

    const float safeFraction = std::clamp(tMin, 0.0f, 1.0f);
    const glm::vec2 worldNormal2 = Rotate2D(localNormal, glm::radians(blocker.yawDegrees));
    hit.hit = true;
    hit.fraction = safeFraction;
    hit.point = start + ((end - start) * safeFraction);
    hit.normal = glm::normalize(glm::vec3(worldNormal2.x, 0.0f, worldNormal2.y));
    hit.name = blocker.name;
    return true;
}

bool PointOverlapsExpandedBlocker(
    const glm::vec3& position,
    const WalkableBlocker& blocker,
    float agentRadius,
    float agentHeight,
    BlockerOverlap& overlap)
{
    overlap = BlockerOverlap {};
    if (!blocker.enabled)
    {
        return false;
    }

    const float actorMinY = position.y;
    const float actorMaxY = position.y + agentHeight;
    const float blockerMinY = blocker.center.y - blocker.halfExtents.y;
    const float blockerMaxY = blocker.center.y + blocker.halfExtents.y;
    if (actorMaxY < blockerMinY || actorMinY > blockerMaxY)
    {
        return false;
    }

    const float inverseYaw = -glm::radians(blocker.yawDegrees);
    const glm::vec2 center(blocker.center.x, blocker.center.z);
    const glm::vec2 localPoint = Rotate2D(glm::vec2(position.x, position.z) - center, inverseYaw);
    const glm::vec2 extents(
        std::max(blocker.halfExtents.x + agentRadius + 0.08f, agentRadius),
        std::max(blocker.halfExtents.z + agentRadius + 0.08f, agentRadius));

    const float dx = extents.x - std::abs(localPoint.x);
    const float dz = extents.y - std::abs(localPoint.y);
    if (dx < 0.0f || dz < 0.0f)
    {
        return false;
    }

    glm::vec2 localNormal(0.0f);
    float penetration = 0.0f;
    if (dx < dz)
    {
        localNormal = glm::vec2(localPoint.x < 0.0f ? -1.0f : 1.0f, 0.0f);
        penetration = dx;
    }
    else
    {
        localNormal = glm::vec2(0.0f, localPoint.y < 0.0f ? -1.0f : 1.0f);
        penetration = dz;
    }

    const glm::vec2 clampedLocalPoint(
        std::clamp(localPoint.x, -extents.x, extents.x),
        std::clamp(localPoint.y, -extents.y, extents.y));
    const glm::vec2 worldNormal2 = Rotate2D(localNormal, glm::radians(blocker.yawDegrees));
    const glm::vec2 closestWorld2 = center + Rotate2D(clampedLocalPoint, glm::radians(blocker.yawDegrees));

    overlap.overlaps = true;
    overlap.penetration = penetration + 0.08f;
    overlap.normal = glm::normalize(glm::vec3(worldNormal2.x, 0.0f, worldNormal2.y));
    overlap.closestPoint = glm::vec3(closestWorld2.x, position.y, closestWorld2.y);
    overlap.name = blocker.name;
    return true;
}

bool FindDeepestBlockerOverlap(
    const glm::vec3& position,
    const std::vector<WalkableBlocker>& blockers,
    const WalkableBuildSettings& settings,
    BlockerOverlap& deepestOverlap)
{
    deepestOverlap = BlockerOverlap {};
    for (const WalkableBlocker& blocker : blockers)
    {
        BlockerOverlap overlap;
        if (!PointOverlapsExpandedBlocker(position, blocker, settings.agentRadius, settings.agentHeight, overlap))
        {
            continue;
        }
        if (!deepestOverlap.overlaps || overlap.penetration > deepestOverlap.penetration)
        {
            deepestOverlap = overlap;
        }
    }
    return deepestOverlap.overlaps;
}

bool FindFirstBlockerHit(
    const glm::vec3& start,
    const glm::vec3& end,
    const std::vector<WalkableBlocker>& blockers,
    const WalkableBuildSettings& settings,
    BlockerHit& firstHit)
{
    firstHit = BlockerHit {};
    for (const WalkableBlocker& blocker : blockers)
    {
        BlockerHit hit;
        if (!SegmentHitsExpandedBlocker(start, end, blocker, settings.agentRadius, settings.agentHeight, hit))
        {
            continue;
        }
        if (!firstHit.hit || hit.fraction < firstHit.fraction)
        {
            firstHit = hit;
        }
    }
    return firstHit.hit;
}

void AppendBlockerDebug(PhysicsDebugFrame& frame, const WalkableBlocker& blocker)
{
    if (!blocker.enabled)
    {
        return;
    }

    const glm::vec3 color(1.0f, 0.38f, 0.95f);
    const float yaw = glm::radians(blocker.yawDegrees);
    const glm::vec2 axisX = Rotate2D(glm::vec2(1.0f, 0.0f), yaw);
    const glm::vec2 axisZ = Rotate2D(glm::vec2(0.0f, 1.0f), yaw);
    const glm::vec3 x(axisX.x, 0.0f, axisX.y);
    const glm::vec3 z(axisZ.x, 0.0f, axisZ.y);
    const glm::vec3 y(0.0f, 1.0f, 0.0f);

    std::array<glm::vec3, 8> corners {};
    int index = 0;
    for (float sx : { -1.0f, 1.0f })
    {
        for (float sy : { -1.0f, 1.0f })
        {
            for (float sz : { -1.0f, 1.0f })
            {
                corners[index++] = blocker.center
                    + (x * blocker.halfExtents.x * sx)
                    + (y * blocker.halfExtents.y * sy)
                    + (z * blocker.halfExtents.z * sz);
            }
        }
    }

    auto addLine = [&](int a, int b)
    {
        frame.lines.push_back(PhysicsDebugLine { corners[a], corners[b], color, color });
    };

    addLine(0, 1);
    addLine(0, 2);
    addLine(3, 1);
    addLine(3, 2);
    addLine(4, 5);
    addLine(4, 6);
    addLine(7, 5);
    addLine(7, 6);
    addLine(0, 4);
    addLine(1, 5);
    addLine(2, 6);
    addLine(3, 7);
    frame.points.push_back(PhysicsDebugPoint { blocker.center, color, 7.0f });
}
}

RecastNavigationWorld::RecastNavigationWorld()
{
    queryFilter_.setIncludeFlags(kWalkFlag);
    queryFilter_.setExcludeFlags(0u);
}

RecastNavigationWorld::~RecastNavigationWorld()
{
    Clear();
}

void RecastNavigationWorld::Clear()
{
    debugFrame_.Clear();
    if (navQuery_ != nullptr)
    {
        dtFreeNavMeshQuery(navQuery_);
        navQuery_ = nullptr;
    }
    if (navMesh_ != nullptr)
    {
        dtFreeNavMesh(navMesh_);
        navMesh_ = nullptr;
    }
}

bool RecastNavigationWorld::Build(
    const std::vector<ImportedModelAsset>& importedAssets,
    const std::vector<StaticRegionDesc>& staticRegions,
    const WalkableBuildSettings& buildSettings)
{
    DebugLog::ScopedTrace trace("RecastNav", "Build");
    Clear();
    buildSettings_ = buildSettings;

    const CombinedGeometry geometry = BuildCombinedGeometry(importedAssets, staticRegions);
    const int numVertices = static_cast<int>(geometry.vertices.size() / 3u);
    const int numTriangles = static_cast<int>(geometry.indices.size() / 3u);
    if (numVertices <= 0 || numTriangles <= 0)
    {
        DebugLog::Error("RecastNav", "No navigation geometry available for navmesh build");
        return false;
    }

    DebugLog::Info(
        "RecastNav",
        "Combined navigation geometry vertices=", numVertices,
        " triangles=", numTriangles);

    rcConfig config {};
    config.cs = buildSettings_.cellSize;
    config.ch = buildSettings_.cellHeight;
    config.walkableSlopeAngle = buildSettings_.agentMaxSlopeDegrees;
    config.walkableHeight = static_cast<int>(std::ceil(buildSettings_.agentHeight / config.ch));
    config.walkableClimb = static_cast<int>(std::ceil(buildSettings_.agentMaxClimb / config.ch));
    config.walkableRadius = static_cast<int>(std::ceil(buildSettings_.agentRadius / config.cs));
    config.maxEdgeLen = static_cast<int>(buildSettings_.edgeMaxLen / config.cs);
    config.maxSimplificationError = buildSettings_.edgeMaxError;
    config.minRegionArea = static_cast<int>(rcSqr(buildSettings_.regionMinSize));
    config.mergeRegionArea = static_cast<int>(rcSqr(buildSettings_.regionMergeSize));
    config.maxVertsPerPoly = buildSettings_.vertsPerPoly;
    config.detailSampleDist = buildSettings_.detailSampleDist < 0.9f ? 0.0f : config.cs * buildSettings_.detailSampleDist;
    config.detailSampleMaxError = config.ch * buildSettings_.detailSampleMaxError;
    config.borderSize = 0;
    config.tileSize = 0;
    SetRecastVector(config.bmin, geometry.bounds.min);
    SetRecastVector(config.bmax, geometry.bounds.max);
    rcCalcGridSize(config.bmin, config.bmax, config.cs, &config.width, &config.height);
    DebugLog::Info(
        "RecastNav",
        "Config cs=", config.cs,
        " ch=", config.ch,
        " width=", config.width,
        " height=", config.height,
        " walkableHeightCells=", config.walkableHeight,
        " walkableClimbCells=", config.walkableClimb,
        " walkableClimbWorld=", static_cast<float>(config.walkableClimb) * config.ch,
        " walkableRadiusCells=", config.walkableRadius,
        " walkableRadiusWorld=", static_cast<float>(config.walkableRadius) * config.cs,
        " slope=", config.walkableSlopeAngle,
        " boundsMin=(",
        geometry.bounds.min.x, ", ", geometry.bounds.min.y, ", ", geometry.bounds.min.z,
        ") boundsMax=(",
        geometry.bounds.max.x, ", ", geometry.bounds.max.y, ", ", geometry.bounds.max.z,
        ")");

    RecastBuildContext context;
    RecastBuildScratch scratch;
    scratch.heightfield = rcAllocHeightfield();
    if (scratch.heightfield == nullptr)
    {
        DebugLog::Error("RecastNav", "Failed to allocate heightfield");
        return false;
    }
    if (!rcCreateHeightfield(
            &context,
            *scratch.heightfield,
            config.width,
            config.height,
            config.bmin,
            config.bmax,
            config.cs,
            config.ch))
    {
        DebugLog::Error("RecastNav", "Failed to create heightfield");
        return false;
    }

    std::vector<unsigned char> triangleAreas(static_cast<std::size_t>(numTriangles), 0u);
    rcMarkWalkableTriangles(
        &context,
        config.walkableSlopeAngle,
        geometry.vertices.data(),
        numVertices,
        geometry.indices.data(),
        numTriangles,
        triangleAreas.data());
    if (!rcRasterizeTriangles(
            &context,
            geometry.vertices.data(),
            numVertices,
            geometry.indices.data(),
            triangleAreas.data(),
            numTriangles,
            *scratch.heightfield,
            config.walkableClimb))
    {
        DebugLog::Error("RecastNav", "Failed to rasterize triangles");
        return false;
    }

    rcFilterLowHangingWalkableObstacles(&context, config.walkableClimb, *scratch.heightfield);
    rcFilterLedgeSpans(&context, config.walkableHeight, config.walkableClimb, *scratch.heightfield);
    rcFilterWalkableLowHeightSpans(&context, config.walkableHeight, *scratch.heightfield);

    scratch.compactHeightfield = rcAllocCompactHeightfield();
    if (scratch.compactHeightfield == nullptr)
    {
        DebugLog::Error("RecastNav", "Failed to allocate compact heightfield");
        return false;
    }
    if (!rcBuildCompactHeightfield(
            &context,
            config.walkableHeight,
            config.walkableClimb,
            *scratch.heightfield,
            *scratch.compactHeightfield))
    {
        DebugLog::Error("RecastNav", "Failed to build compact heightfield");
        return false;
    }
    if (!rcErodeWalkableArea(&context, config.walkableRadius, *scratch.compactHeightfield))
    {
        DebugLog::Error("RecastNav", "Failed to erode walkable area");
        return false;
    }

    if (!rcBuildRegionsMonotone(&context, *scratch.compactHeightfield, 0, config.minRegionArea, config.mergeRegionArea))
    {
        DebugLog::Error("RecastNav", "Failed to build monotone regions");
        return false;
    }

    scratch.contourSet = rcAllocContourSet();
    if (scratch.contourSet == nullptr)
    {
        DebugLog::Error("RecastNav", "Failed to allocate contour set");
        return false;
    }
    if (!rcBuildContours(&context, *scratch.compactHeightfield, config.maxSimplificationError, config.maxEdgeLen, *scratch.contourSet))
    {
        DebugLog::Error("RecastNav", "Failed to build contours");
        return false;
    }

    scratch.polyMesh = rcAllocPolyMesh();
    if (scratch.polyMesh == nullptr)
    {
        DebugLog::Error("RecastNav", "Failed to allocate poly mesh");
        return false;
    }
    if (!rcBuildPolyMesh(&context, *scratch.contourSet, config.maxVertsPerPoly, *scratch.polyMesh))
    {
        DebugLog::Error("RecastNav", "Failed to build poly mesh");
        return false;
    }

    scratch.detailMesh = rcAllocPolyMeshDetail();
    if (scratch.detailMesh == nullptr)
    {
        DebugLog::Error("RecastNav", "Failed to allocate detail mesh");
        return false;
    }
    if (!rcBuildPolyMeshDetail(
            &context,
            *scratch.polyMesh,
            *scratch.compactHeightfield,
            config.detailSampleDist,
            config.detailSampleMaxError,
            *scratch.detailMesh))
    {
        DebugLog::Error("RecastNav", "Failed to build detail mesh");
        return false;
    }

    for (int polygonIndex = 0; polygonIndex < scratch.polyMesh->npolys; ++polygonIndex)
    {
        if (scratch.polyMesh->areas[polygonIndex] == RC_WALKABLE_AREA)
        {
            scratch.polyMesh->areas[polygonIndex] = kGroundArea;
            scratch.polyMesh->flags[polygonIndex] = kWalkFlag;
        }
        else
        {
            scratch.polyMesh->flags[polygonIndex] = 0u;
        }
    }

    dtNavMeshCreateParams params {};
    params.verts = scratch.polyMesh->verts;
    params.vertCount = scratch.polyMesh->nverts;
    params.polys = scratch.polyMesh->polys;
    params.polyAreas = scratch.polyMesh->areas;
    params.polyFlags = scratch.polyMesh->flags;
    params.polyCount = scratch.polyMesh->npolys;
    params.nvp = scratch.polyMesh->nvp;
    params.detailMeshes = scratch.detailMesh->meshes;
    params.detailVerts = scratch.detailMesh->verts;
    params.detailVertsCount = scratch.detailMesh->nverts;
    params.detailTris = scratch.detailMesh->tris;
    params.detailTriCount = scratch.detailMesh->ntris;
    params.walkableHeight = buildSettings_.agentHeight;
    params.walkableRadius = buildSettings_.agentRadius;
    params.walkableClimb = buildSettings_.agentMaxClimb;
    rcVcopy(params.bmin, scratch.polyMesh->bmin);
    rcVcopy(params.bmax, scratch.polyMesh->bmax);
    params.cs = config.cs;
    params.ch = config.ch;
    params.buildBvTree = true;

    unsigned char* navData = nullptr;
    int navDataSize = 0;
    if (!dtCreateNavMeshData(&params, &navData, &navDataSize))
    {
        DebugLog::Error("RecastNav", "Failed to create Detour navmesh data");
        return false;
    }

    navMesh_ = dtAllocNavMesh();
    if (navMesh_ == nullptr)
    {
        dtFree(navData);
        DebugLog::Error("RecastNav", "Failed to allocate Detour navmesh");
        return false;
    }

    dtStatus status = navMesh_->init(navData, navDataSize, DT_TILE_FREE_DATA);
    if (dtStatusFailed(status))
    {
        dtFree(navData);
        DebugLog::Error("RecastNav", "Failed to initialize Detour navmesh");
        Clear();
        return false;
    }

    navQuery_ = dtAllocNavMeshQuery();
    if (navQuery_ == nullptr)
    {
        DebugLog::Error("RecastNav", "Failed to allocate Detour navmesh query");
        Clear();
        return false;
    }

    status = navQuery_->init(navMesh_, kMaxQueryNodes);
    if (dtStatusFailed(status))
    {
        DebugLog::Error("RecastNav", "Failed to initialize Detour navmesh query");
        Clear();
        return false;
    }

    debugFrame_ = BuildNavMeshDebugFrame(*scratch.polyMesh);
    DebugLog::Info(
        "RecastNav",
        "Navmesh build complete polys=", scratch.polyMesh->npolys,
        " verts=", scratch.polyMesh->nverts,
        " detailTris=", scratch.detailMesh->ntris);
    return true;
}

bool RecastNavigationWorld::IsReady() const
{
    return navMesh_ != nullptr && navQuery_ != nullptr;
}

const WalkableBuildSettings& RecastNavigationWorld::GetBuildSettings() const
{
    return buildSettings_;
}

bool RecastNavigationWorld::SamplePosition(const glm::vec3& desiredPosition, float searchRadius, WalkableSample& sample) const
{
    sample = WalkableSample {};
    if (!IsReady())
    {
        return false;
    }

    const float halfExtents[3] {
        std::max(searchRadius, buildSettings_.agentRadius),
        std::max(buildSettings_.agentHeight, buildSettings_.agentMaxClimb * 4.0f),
        std::max(searchRadius, buildSettings_.agentRadius)
    };

    float center[3];
    SetRecastVector(center, desiredPosition);
    float nearestPoint[3] {};
    dtPolyRef nearestRef = 0;
    bool isOverPoly = false;
    const dtStatus status = navQuery_->findNearestPoly(center, halfExtents, &queryFilter_, &nearestRef, nearestPoint, &isOverPoly);
    if (dtStatusFailed(status) || nearestRef == 0)
    {
        return false;
    }

    float height = nearestPoint[1];
    navQuery_->getPolyHeight(nearestRef, nearestPoint, &height);
    nearestPoint[1] = height;

    sample.valid = true;
    sample.position = FromRecastVector(nearestPoint);
    sample.polyRef = static_cast<std::uint64_t>(nearestRef);
    return true;
}

bool RecastNavigationWorld::MoveAlongSurface(
    std::uint64_t startPolyRef,
    const glm::vec3& startPosition,
    const glm::vec3& desiredPosition,
    WalkableMoveResult& result) const
{
    result = WalkableMoveResult {};
    if (!IsReady())
    {
        return false;
    }

    dtPolyRef workingRef = static_cast<dtPolyRef>(startPolyRef);
    float startPoint[3];
    SetRecastVector(startPoint, startPosition);
    if (workingRef == 0 || !navQuery_->isValidPolyRef(workingRef, &queryFilter_))
    {
        WalkableSample sample;
        if (!SamplePosition(startPosition, buildSettings_.agentRadius * 4.0f, sample))
        {
            return false;
        }
        workingRef = static_cast<dtPolyRef>(sample.polyRef);
        SetRecastVector(startPoint, sample.position);
    }

    float desiredPoint[3];
    glm::vec3 queryDesiredPosition = desiredPosition;
    BlockerHit blockerHit;
    const bool hitDynamicBlocker = FindFirstBlockerHit(
        FromRecastVector(startPoint),
        desiredPosition,
        dynamicBlockers_,
        buildSettings_,
        blockerHit);
    if (hitDynamicBlocker)
    {
        const float safeFraction = std::max(blockerHit.fraction - 0.05f, 0.0f);
        queryDesiredPosition = FromRecastVector(startPoint) + ((desiredPosition - FromRecastVector(startPoint)) * safeFraction);
    }

    SetRecastVector(desiredPoint, queryDesiredPosition);
    float movedPoint[3] {};
    dtPolyRef visited[kMaxVisitedPolys] {};
    int visitedCount = 0;
    dtStatus status = navQuery_->moveAlongSurface(
        workingRef,
        startPoint,
        desiredPoint,
        &queryFilter_,
        movedPoint,
        visited,
        &visitedCount,
        kMaxVisitedPolys);
    if (dtStatusFailed(status))
    {
        return false;
    }

    dtPolyRef resultingRef = visitedCount > 0 ? visited[visitedCount - 1] : workingRef;
    float height = movedPoint[1];
    if (resultingRef != 0)
    {
        navQuery_->getPolyHeight(resultingRef, movedPoint, &height);
    }
    movedPoint[1] = height;

    result.valid = true;
    result.position = FromRecastVector(movedPoint);
    result.polyRef = static_cast<std::uint64_t>(resultingRef);

    if (hitDynamicBlocker)
    {
        result.blocked = true;
        result.wallPoint = blockerHit.point;
        result.wallNormal = blockerHit.normal;
        return true;
    }

    float hitParameter = FLT_MAX;
    float hitNormal[3] {};
    status = navQuery_->raycast(
        workingRef,
        startPoint,
        desiredPoint,
        &queryFilter_,
        &hitParameter,
        hitNormal,
        nullptr,
        nullptr,
        0);
    if (dtStatusSucceed(status) && hitParameter >= 0.0f && hitParameter < FLT_MAX && hitParameter < 1.0f)
    {
        const glm::vec3 start = FromRecastVector(startPoint);
        const glm::vec3 end = FromRecastVector(desiredPoint);
        result.blocked = true;
        result.wallPoint = start + ((end - start) * hitParameter);
        result.wallNormal = glm::normalize(FromRecastVector(hitNormal));
    }

    return true;
}

void RecastNavigationWorld::SetDynamicBlockers(std::vector<WalkableBlocker> blockers)
{
    dynamicBlockers_ = std::move(blockers);
}

bool RecastNavigationWorld::ResolveDynamicBlockers(
    const glm::vec3& currentPosition,
    float maxPushDistance,
    WalkablePushResult& result) const
{
    result = WalkablePushResult {};
    if (!IsReady() || dynamicBlockers_.empty())
    {
        return false;
    }

    BlockerOverlap overlap;
    if (!FindDeepestBlockerOverlap(currentPosition, dynamicBlockers_, buildSettings_, overlap))
    {
        return false;
    }

    const float cappedPush = std::clamp(overlap.penetration, 0.0f, std::max(maxPushDistance, 0.0f));
    const glm::vec3 pushedPosition = currentPosition + (overlap.normal * cappedPush);

    WalkableSample sample;
    glm::vec3 resolvedPosition = pushedPosition;
    std::uint64_t resolvedPolyRef = 0;
    if (SamplePosition(pushedPosition, std::max(buildSettings_.agentRadius * 4.0f, 1.5f), sample) && sample.valid)
    {
        resolvedPosition = sample.position;
        resolvedPolyRef = sample.polyRef;
    }

    result.valid = true;
    result.adjusted = cappedPush > 0.0001f;
    result.position = resolvedPosition;
    result.polyRef = resolvedPolyRef;
    result.pushFrom = overlap.closestPoint;
    result.pushNormal = overlap.normal;
    result.blockerName = overlap.name;
    return true;
}

PhysicsDebugFrame RecastNavigationWorld::BuildDebugFrame() const
{
    PhysicsDebugFrame frame = debugFrame_;
    for (const WalkableBlocker& blocker : dynamicBlockers_)
    {
        AppendBlockerDebug(frame, blocker);
    }
    return frame;
}
