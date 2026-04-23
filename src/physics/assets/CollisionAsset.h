#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "import/ImportedGeometry.h"
#include "physics/core/PhysicsTypes.h"

enum class CollisionSemantic
{
    StaticWorld,
    DynamicBody,
    Trigger,
    Ignore
};

enum class ColliderBuildMode
{
    DetailedTriMesh,
    Box,
    Sphere,
    Capsule,
    ConvexHull,
    Compound
};

struct CollisionRule
{
    std::string pattern;
    CollisionSemantic semantic = CollisionSemantic::StaticWorld;
    ColliderBuildMode buildMode = ColliderBuildMode::DetailedTriMesh;
    std::uint16_t categoryBits = CollisionLayers::WorldStatic;
    std::uint16_t maskBits = CollisionLayers::All;
    std::string regionId = "default";
    bool enableCharacterQueries = false;
};

struct CollisionProfile
{
    std::vector<CollisionRule> rules;
};

struct CollisionAsset
{
    std::filesystem::path sourcePath;
    std::vector<StaticRegionDesc> staticRegions;
    std::vector<StaticPrimitiveDesc> staticPrimitives;
    std::vector<DynamicBodyDesc> dynamicBodies;
    std::vector<TriggerDesc> triggers;
    PhysicsAabb worldBounds;
};

[[nodiscard]] std::filesystem::path GetCollisionProfilePathForAsset(const std::filesystem::path& assetPath);
[[nodiscard]] CollisionProfile LoadCollisionProfile(const std::filesystem::path& profilePath);
[[nodiscard]] CollisionAsset BuildCollisionAsset(
    const ImportedModelAsset& importedAsset,
    const CollisionProfile& profile);
