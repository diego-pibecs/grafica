#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "physics/core/PhysicsTypes.h"

struct ImportedSubmesh
{
    std::string name;
    std::string nodePath;
    glm::mat4 worldTransform { 1.0f };
    std::vector<glm::vec3> localVertices;
    std::vector<std::uint32_t> indices;
    std::vector<glm::vec3> worldVertices;
    PhysicsAabb localBounds;
    PhysicsAabb worldBounds;
};

struct ImportedModelAsset
{
    std::filesystem::path sourcePath;
    std::vector<ImportedSubmesh> submeshes;
    PhysicsAabb worldBounds;

    [[nodiscard]] bool IsValid() const noexcept
    {
        return !submeshes.empty();
    }
};

ImportedModelAsset ImportModelAsset(
    const std::filesystem::path& sourcePath,
    const glm::mat4& instanceTransform = glm::mat4(1.0f));
