#include "Model.h"

#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include "TextureUtils.h"

Model::Model(const std::filesystem::path& path)
{
    LoadModel(path);
}

void Model::Draw() const
{
    for (const Mesh& mesh : meshes_)
    {
        mesh.Draw();
    }
}

bool Model::IsLoaded() const noexcept
{
    return isLoaded_;
}

bool Model::HasTextures() const noexcept
{
    return std::any_of(meshes_.begin(), meshes_.end(), [](const Mesh& mesh) { return mesh.HasTexture(); });
}

glm::vec3 Model::GetMinBounds() const noexcept
{
    return minBounds_;
}

glm::vec3 Model::GetMaxBounds() const noexcept
{
    return maxBounds_;
}

glm::vec3 Model::GetCenter() const noexcept
{
    return (minBounds_ + maxBounds_) * 0.5f;
}

glm::vec3 Model::GetSize() const noexcept
{
    return maxBounds_ - minBounds_;
}

void Model::LoadModel(const std::filesystem::path& path)
{
    minBounds_ = glm::vec3(std::numeric_limits<float>::max());
    maxBounds_ = glm::vec3(std::numeric_limits<float>::lowest());

    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(
        path.string(),
        aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_GenSmoothNormals);

    if (scene == nullptr || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) != 0 || scene->mRootNode == nullptr)
    {
        std::cerr << "ERROR::ASSIMP:: " << importer.GetErrorString() << '\n';
        return;
    }

    directory_ = path.parent_path();
    ProcessNode(scene->mRootNode, scene);
    isLoaded_ = !meshes_.empty();
}

void Model::ProcessNode(aiNode* node, const aiScene* scene)
{
    for (unsigned int meshIndex = 0; meshIndex < node->mNumMeshes; ++meshIndex)
    {
        aiMesh* mesh = scene->mMeshes[node->mMeshes[meshIndex]];
        meshes_.push_back(ProcessMesh(mesh, scene));
    }

    for (unsigned int childIndex = 0; childIndex < node->mNumChildren; ++childIndex)
    {
        ProcessNode(node->mChildren[childIndex], scene);
    }
}

Mesh Model::ProcessMesh(aiMesh* mesh, const aiScene* scene)
{
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
    std::vector<Texture> textures;

    vertices.reserve(mesh->mNumVertices);
    for (unsigned int vertexIndex = 0; vertexIndex < mesh->mNumVertices; ++vertexIndex)
    {
        Vertex vertex {};
        vertex.position = glm::vec3(
            mesh->mVertices[vertexIndex].x,
            mesh->mVertices[vertexIndex].y,
            mesh->mVertices[vertexIndex].z);
        ExpandBounds(vertex.position);

        if (mesh->HasNormals())
        {
            vertex.normal = glm::vec3(
                mesh->mNormals[vertexIndex].x,
                mesh->mNormals[vertexIndex].y,
                mesh->mNormals[vertexIndex].z);
        }
        else
        {
            vertex.normal = glm::vec3(0.0f, 1.0f, 0.0f);
        }

        if (mesh->mTextureCoords[0] != nullptr)
        {
            vertex.texCoords = glm::vec2(
                mesh->mTextureCoords[0][vertexIndex].x,
                mesh->mTextureCoords[0][vertexIndex].y);
        }
        else
        {
            vertex.texCoords = glm::vec2(0.0f, 0.0f);
        }

        vertices.push_back(vertex);
    }

    for (unsigned int faceIndex = 0; faceIndex < mesh->mNumFaces; ++faceIndex)
    {
        const aiFace& face = mesh->mFaces[faceIndex];
        for (unsigned int index = 0; index < face.mNumIndices; ++index)
        {
            indices.push_back(face.mIndices[index]);
        }
    }

    if (mesh->mMaterialIndex >= 0)
    {
        aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];
        std::vector<Texture> diffuseTextures = LoadMaterialTextures(material, aiTextureType_DIFFUSE, "texture_diffuse");
        textures.insert(textures.end(), diffuseTextures.begin(), diffuseTextures.end());
    }

    return Mesh(std::move(vertices), std::move(indices), std::move(textures));
}

std::vector<Texture> Model::LoadMaterialTextures(aiMaterial* material, int type, const std::string& typeName)
{
    std::vector<Texture> textures;

    for (unsigned int index = 0; index < material->GetTextureCount(static_cast<aiTextureType>(type)); ++index)
    {
        aiString textureName;
        material->GetTexture(static_cast<aiTextureType>(type), index, &textureName);

        const std::filesystem::path texturePath = (directory_ / textureName.C_Str()).lexically_normal();
        const std::string normalizedPath = texturePath.string();

        const auto existing = std::find_if(
            texturesLoaded_.begin(),
            texturesLoaded_.end(),
            [&normalizedPath](const Texture& texture) { return texture.path == normalizedPath; });

        if (existing != texturesLoaded_.end())
        {
            textures.push_back(*existing);
            continue;
        }

        try
        {
            Texture texture;
            texture.id = LoadTexture2D(texturePath);
            texture.type = typeName;
            texture.path = normalizedPath;
            textures.push_back(texture);
            texturesLoaded_.push_back(texture);
        }
        catch (const std::exception& error)
        {
            std::cerr << error.what() << '\n';
        }
    }

    return textures;
}

void Model::ExpandBounds(const glm::vec3& position)
{
    minBounds_.x = std::min(minBounds_.x, position.x);
    minBounds_.y = std::min(minBounds_.y, position.y);
    minBounds_.z = std::min(minBounds_.z, position.z);

    maxBounds_.x = std::max(maxBounds_.x, position.x);
    maxBounds_.y = std::max(maxBounds_.y, position.y);
    maxBounds_.z = std::max(maxBounds_.z, position.z);
}
