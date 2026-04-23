#include "import/ImportedGeometry.h"

#include "DebugLog.h"

#include <limits>
#include <stdexcept>

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

namespace
{
glm::mat4 AiToGlm(const aiMatrix4x4& matrix)
{
    glm::mat4 result(1.0f);
    result[0][0] = matrix.a1;
    result[1][0] = matrix.a2;
    result[2][0] = matrix.a3;
    result[3][0] = matrix.a4;
    result[0][1] = matrix.b1;
    result[1][1] = matrix.b2;
    result[2][1] = matrix.b3;
    result[3][1] = matrix.b4;
    result[0][2] = matrix.c1;
    result[1][2] = matrix.c2;
    result[2][2] = matrix.c3;
    result[3][2] = matrix.c4;
    result[0][3] = matrix.d1;
    result[1][3] = matrix.d2;
    result[2][3] = matrix.d3;
    result[3][3] = matrix.d4;
    return result;
}

PhysicsAabb MakeEmptyBounds()
{
    return PhysicsAabb {
        glm::vec3(std::numeric_limits<float>::max()),
        glm::vec3(std::numeric_limits<float>::lowest())
    };
}

void ExpandBounds(PhysicsAabb& bounds, const glm::vec3& point)
{
    bounds.min = glm::min(bounds.min, point);
    bounds.max = glm::max(bounds.max, point);
}

std::string MakeNodePath(const std::string& parentPath, const aiNode* node)
{
    const std::string nodeName = node != nullptr && node->mName.length > 0
        ? std::string(node->mName.C_Str())
        : std::string("node");

    if (parentPath.empty())
    {
        return nodeName;
    }

    return parentPath + "/" + nodeName;
}

void ProcessNode(
    aiNode* node,
    const aiScene* scene,
    const glm::mat4& parentTransform,
    const glm::mat4& instanceTransform,
    const std::string& parentPath,
    ImportedModelAsset& asset)
{
    const std::string nodePath = MakeNodePath(parentPath, node);
    const glm::mat4 localTransform = AiToGlm(node->mTransformation);
    const glm::mat4 accumulatedTransform = parentTransform * localTransform;

    for (unsigned int meshIndex = 0; meshIndex < node->mNumMeshes; ++meshIndex)
    {
        aiMesh* mesh = scene->mMeshes[node->mMeshes[meshIndex]];
        ImportedSubmesh submesh;
        submesh.name = mesh->mName.length > 0
            ? std::string(mesh->mName.C_Str())
            : nodePath + "#" + std::to_string(meshIndex);
        submesh.nodePath = nodePath;
        submesh.worldTransform = instanceTransform * accumulatedTransform;
        submesh.localBounds = MakeEmptyBounds();
        submesh.worldBounds = MakeEmptyBounds();

        submesh.localVertices.reserve(mesh->mNumVertices);
        submesh.worldVertices.reserve(mesh->mNumVertices);
        for (unsigned int vertexIndex = 0; vertexIndex < mesh->mNumVertices; ++vertexIndex)
        {
            const glm::vec3 localPosition(
                mesh->mVertices[vertexIndex].x,
                mesh->mVertices[vertexIndex].y,
                mesh->mVertices[vertexIndex].z);
            const glm::vec4 worldPosition4 = submesh.worldTransform * glm::vec4(localPosition, 1.0f);
            const glm::vec3 worldPosition(worldPosition4.x, worldPosition4.y, worldPosition4.z);

            submesh.localVertices.push_back(localPosition);
            submesh.worldVertices.push_back(worldPosition);
            ExpandBounds(submesh.localBounds, localPosition);
            ExpandBounds(submesh.worldBounds, worldPosition);
            ExpandBounds(asset.worldBounds, worldPosition);
        }

        for (unsigned int faceIndex = 0; faceIndex < mesh->mNumFaces; ++faceIndex)
        {
            const aiFace& face = mesh->mFaces[faceIndex];
            if (face.mNumIndices != 3)
            {
                continue;
            }

            submesh.indices.push_back(static_cast<std::uint32_t>(face.mIndices[0]));
            submesh.indices.push_back(static_cast<std::uint32_t>(face.mIndices[1]));
            submesh.indices.push_back(static_cast<std::uint32_t>(face.mIndices[2]));
        }

        if (!submesh.indices.empty())
        {
            asset.submeshes.push_back(std::move(submesh));
        }
    }

    for (unsigned int childIndex = 0; childIndex < node->mNumChildren; ++childIndex)
    {
        ProcessNode(
            node->mChildren[childIndex],
            scene,
            accumulatedTransform,
            instanceTransform,
            nodePath,
            asset);
    }
}
}

ImportedModelAsset ImportModelAsset(const std::filesystem::path& sourcePath, const glm::mat4& instanceTransform)
{
    DebugLog::ScopedTrace trace("ImportedGeometry", sourcePath.string());
    Assimp::Importer importer;
    importer.SetPropertyInteger(
        AI_CONFIG_PP_RVC_FLAGS,
        aiComponent_NORMALS
            | aiComponent_TANGENTS_AND_BITANGENTS
            | aiComponent_COLORS
            | aiComponent_TEXCOORDS
            | aiComponent_BONEWEIGHTS
            | aiComponent_ANIMATIONS
            | aiComponent_TEXTURES
            | aiComponent_LIGHTS
            | aiComponent_CAMERAS
            | aiComponent_MATERIALS);
    DebugLog::Info("ImportedGeometry", "ReadFile begin path=", sourcePath.string());
    const aiScene* scene = importer.ReadFile(
        sourcePath.string(),
        aiProcess_Triangulate | aiProcess_RemoveComponent | aiProcess_FindInvalidData);
    DebugLog::Info("ImportedGeometry", "ReadFile returned path=", sourcePath.string(), " scene=", scene != nullptr);

    if (scene == nullptr || scene->mRootNode == nullptr || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) != 0)
    {
        DebugLog::Error("ImportedGeometry", "Assimp import failed for ", sourcePath.string(), ": ", importer.GetErrorString());
        throw std::runtime_error("Failed to import geometry for collision: " + sourcePath.string());
    }

    ImportedModelAsset asset;
    asset.sourcePath = sourcePath;
    asset.worldBounds = MakeEmptyBounds();

    DebugLog::Info("ImportedGeometry", "Processing node hierarchy path=", sourcePath.string(), " meshes=", scene->mNumMeshes);
    ProcessNode(scene->mRootNode, scene, glm::mat4(1.0f), instanceTransform, "", asset);

    if (!asset.IsValid())
    {
        DebugLog::Error("ImportedGeometry", "No triangulated submeshes for ", sourcePath.string());
        throw std::runtime_error("Imported model has no triangulated submeshes: " + sourcePath.string());
    }

    DebugLog::Info(
        "ImportedGeometry",
        "Imported asset path=", sourcePath.string(),
        " submeshes=", asset.submeshes.size());
    return asset;
}
