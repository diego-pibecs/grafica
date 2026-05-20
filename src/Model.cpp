#include "Model.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <unordered_set>

#include <assimp/Importer.hpp>
#include <assimp/matrix4x4.h>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <glm/gtc/matrix_inverse.hpp>
#include "DebugLog.h"
#include "TextureUtils.h"

namespace fs = std::filesystem;

namespace
{
std::string ToLowerAscii(std::string value)
{
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char character)
        {
            return static_cast<char>(std::tolower(character));
        });
    return value;
}

std::string NormalizeTextureReference(std::string value)
{
    std::replace(value.begin(), value.end(), '\\', '/');
    return value;
}

bool IsHouseDiagnosticName(const std::string& lowerName)
{
    constexpr std::array<const char*, 15> kNeedles {
        "lighting",
        "table",
        "043203",
        "043103",
        "chair_35",
        "50x50",
        "chair7",
        "sofa_03",
        "300x100",
        "legs",
        "arms",
        "ceiling",
        "roof",
        "techo",
        "diningtable"
    };
    return std::any_of(
        kNeedles.begin(),
        kNeedles.end(),
        [&](const char* needle)
        {
            return lowerName.find(needle) != std::string::npos;
        });
}

std::string NormalizeLookupKey(const std::string& value)
{
    std::string normalized;
    normalized.reserve(value.size());
    for (const unsigned char character : value)
    {
        if (std::isalnum(character) != 0)
        {
            normalized.push_back(static_cast<char>(std::tolower(character)));
        }
    }
    return normalized;
}

bool StartsWith(const std::string& value, const std::string& prefix)
{
    return value.rfind(prefix, 0) == 0;
}

unsigned char HashNoise(int x, int y, int seed)
{
    std::uint32_t value = static_cast<std::uint32_t>(x * 374761393u)
        ^ static_cast<std::uint32_t>(y * 668265263u)
        ^ static_cast<std::uint32_t>(seed * 2246822519u);
    value = (value ^ (value >> 13u)) * 1274126177u;
    value ^= value >> 16u;
    return static_cast<unsigned char>(value & 0xFFu);
}

std::array<long long, 3> QuantizedPositionKey(const glm::vec3& position)
{
    constexpr float kDuplicateTriangleGrid = 0.01f;
    const auto quantize = [](float value)
    {
        return static_cast<long long>(std::llround(value / kDuplicateTriangleGrid));
    };

    return { quantize(position.x), quantize(position.y), quantize(position.z) };
}

std::array<long long, 9> TriangleKey(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c)
{
    std::array<std::array<long long, 3>, 3> keys {
        QuantizedPositionKey(a),
        QuantizedPositionKey(b),
        QuantizedPositionKey(c)
    };
    std::sort(keys.begin(), keys.end());

    return {
        keys[0][0], keys[0][1], keys[0][2],
        keys[1][0], keys[1][1], keys[1][2],
        keys[2][0], keys[2][1], keys[2][2]
    };
}

bool IsDegenerateTriangle(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c)
{
    constexpr float kMinimumTriangleAreaSquared = 0.00000001f;
    const glm::vec3 cross = glm::cross(b - a, c - a);
    return glm::dot(cross, cross) <= kMinimumTriangleAreaSquared;
}

bool IsImageFile(const fs::path& path)
{
    const std::string extension = ToLowerAscii(path.extension().string());
    return extension == ".png"
        || extension == ".jpg"
        || extension == ".jpeg"
        || extension == ".tga"
        || extension == ".bmp";
}

unsigned char ToByte(float value)
{
    const float clamped = std::clamp(value, 0.0f, 1.0f);
    return static_cast<unsigned char>(std::lround(clamped * 255.0f));
}

int ScoreDiffuseCandidate(const fs::path& path)
{
    const std::string filename = ToLowerAscii(path.filename().string());
    int score = 0;

    if (filename.find("basecolor") != std::string::npos || filename.find("base_color") != std::string::npos)
    {
        score += 12;
    }
    if (filename.find("albedo") != std::string::npos)
    {
        score += 12;
    }
    if (filename.find("diffuse") != std::string::npos)
    {
        score += 10;
    }
    if (filename.find("color") != std::string::npos)
    {
        score += 6;
    }
    if (filename.find("normal") != std::string::npos
        || filename.find("rough") != std::string::npos
        || filename.find("metal") != std::string::npos
        || filename.find("spec") != std::string::npos
        || filename.find("ao") != std::string::npos
        || filename.find("cavity") != std::string::npos
        || filename.find("gloss") != std::string::npos
        || filename.find("bump") != std::string::npos
        || filename.find("opacity") != std::string::npos
        || filename.find("emissive") != std::string::npos
        || filename.find("displacement") != std::string::npos)
    {
        score -= 10;
    }

    return score;
}

std::vector<fs::path> BuildLocalSearchRoots(const fs::path& directory)
{
    std::vector<fs::path> searchRoots;
    searchRoots.push_back(directory);

    const fs::path parent = directory.parent_path();
    if (!parent.empty()
        && parent != directory
        && (directory.filename() == "source" || fs::exists(parent / "textures")))
    {
        searchRoots.push_back(parent);
    }

    return searchRoots;
}

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
}

Model::Model(const std::filesystem::path& path, bool loadTextures)
    : loadTextures_(loadTextures)
{
    LoadModel(path);
}

void Model::Draw() const
{
    GLint currentProgram = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &currentProgram);
    const GLint depthOffsetLocation = currentProgram != 0
        ? glGetUniformLocation(static_cast<GLuint>(currentProgram), "depthOffset")
        : -1;

    constexpr float kCoplanarDepthOffsetBase = 0.000004f;
    constexpr float kCoplanarDepthOffsetStep = 0.000003f;
    constexpr std::size_t kCoplanarDepthOffsetLanes = 31;

    for (std::size_t meshIndex = 0; meshIndex < meshes_.size(); ++meshIndex)
    {
        // Tiny deterministic depth bias reduces fighting without changing mesh silhouettes.
        const std::size_t lane = (meshIndex * 7u) % kCoplanarDepthOffsetLanes;
        const float depthOffset = kCoplanarDepthOffsetBase + (static_cast<float>(lane) * kCoplanarDepthOffsetStep);
        meshes_[meshIndex].DrawWithDepthOffset(depthOffsetLocation, depthOffset);
    }

    if (depthOffsetLocation >= 0)
    {
        glUniform1f(depthOffsetLocation, 0.0f);
    }
}

void Model::DrawWithoutTextures() const
{
    for (const Mesh& mesh : meshes_)
    {
        mesh.DrawWithoutTextures();
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

const std::vector<Model::NamedBounds>& Model::GetNamedBounds() const noexcept
{
    return namedBounds_;
}

const std::vector<Model::NamedNode>& Model::GetNamedNodes() const noexcept
{
    return namedNodes_;
}

void Model::LoadModel(const std::filesystem::path& path)
{
    DebugLog::ScopedTrace trace("Model", path.string());
    minBounds_ = glm::vec3(std::numeric_limits<float>::max());
    maxBounds_ = glm::vec3(std::numeric_limits<float>::lowest());
    namedBounds_.clear();
    namedNodes_.clear();
    triangleKeys_.clear();
    sourceTriangleCount_ = 0;
    removedDuplicateTriangleCount_ = 0;
    removedDegenerateTriangleCount_ = 0;

    Assimp::Importer importer;
    DebugLog::Info("Model", "ReadFile begin path=", path.string(), " loadTextures=", loadTextures_);
    const aiScene* scene = importer.ReadFile(
        path.string(),
        aiProcess_Triangulate
            | aiProcess_FlipUVs
            | aiProcess_GenSmoothNormals
            | aiProcess_JoinIdenticalVertices
            | aiProcess_FindDegenerates
            | aiProcess_FindInvalidData);
    DebugLog::Info("Model", "ReadFile returned path=", path.string(), " scene=", scene != nullptr);

    if (scene == nullptr || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) != 0 || scene->mRootNode == nullptr)
    {
        std::cerr << "ERROR::ASSIMP:: " << importer.GetErrorString() << '\n';
        DebugLog::Error("Model", "Assimp error for ", path.string(), ": ", importer.GetErrorString());
        return;
    }

    directory_ = path.parent_path();
    sourceFilename_ = ToLowerAscii(path.filename().string());
    DebugLog::Info(
        "Model",
        "Scene stats path=", path.string(),
        " meshes=", scene->mNumMeshes,
        " materials=", scene->mNumMaterials,
        " textures=", scene->mNumTextures);
    ProcessNode(scene->mRootNode, scene, glm::mat4(1.0f));
    isLoaded_ = !meshes_.empty();
    DebugLog::Info(
        "Model",
        "Model load result path=", path.string(),
        " loaded=", isLoaded_,
        " meshCount=", meshes_.size(),
        " namedBounds=", namedBounds_.size(),
        " sourceTriangles=", sourceTriangleCount_,
        " removedDuplicates=", removedDuplicateTriangleCount_,
        " removedDegenerate=", removedDegenerateTriangleCount_);
    triangleKeys_.clear();
}

void Model::ProcessNode(aiNode* node, const aiScene* scene, const glm::mat4& parentTransform)
{
    const glm::mat4 nodeTransform = parentTransform * AiToGlm(node->mTransformation);
    if (node->mName.length > 0)
    {
        namedNodes_.push_back(Model::NamedNode { node->mName.C_Str(), nodeTransform });
    }
    if (ShouldSkipNode(node))
    {
        DebugLog::Info(
            "HOUSE MESH SKIP",
            "node=", node->mName.C_Str(),
            " mesh=<node>",
            " reason=duplicate carryable / explicit skip");
        return;
    }

    for (unsigned int meshIndex = 0; meshIndex < node->mNumMeshes; ++meshIndex)
    {
        aiMesh* mesh = scene->mMeshes[node->mMeshes[meshIndex]];
        if (ShouldSkipMesh(mesh))
        {
            DebugLog::Info(
                "HOUSE MESH SKIP",
                "node=", node->mName.C_Str(),
                " mesh=", mesh->mName.C_Str(),
                " reason=duplicate carryable / explicit skip");
            continue;
        }
        if (sourceFilename_ == "example16_var1.fbx")
        {
            const std::string lowerNodeName = ToLowerAscii(node->mName.C_Str());
            const std::string lowerMeshName = ToLowerAscii(mesh->mName.C_Str());
            if (IsHouseDiagnosticName(lowerNodeName) || IsHouseDiagnosticName(lowerMeshName))
            {
                DebugLog::Info(
                    "HOUSE MESH",
                    "node=", node->mName.C_Str(),
                    " mesh=", mesh->mName.C_Str(),
                    " render=true collision=true reason=processed");
            }
        }
        meshes_.push_back(ProcessMesh(mesh, scene, nodeTransform));
    }

    for (unsigned int childIndex = 0; childIndex < node->mNumChildren; ++childIndex)
    {
        ProcessNode(node->mChildren[childIndex], scene, nodeTransform);
    }
}

bool Model::ShouldSkipNode(const aiNode* node) const
{
    if (node == nullptr || sourceFilename_ != "example16_var1.fbx")
    {
        return false;
    }

    const std::string nodeName = ToLowerAscii(node->mName.C_Str());
    return nodeName.find("endtable_07_40x40") != std::string::npos
        || nodeName.find("p_pl_08_01_walltv") != std::string::npos;
}

bool Model::ShouldSkipMesh(const aiMesh* mesh) const
{
    if (mesh == nullptr || sourceFilename_ != "example16_var1.fbx")
    {
        return false;
    }

    const std::string meshName = ToLowerAscii(mesh->mName.C_Str());
    return meshName.find("endtable_07_40x40") != std::string::npos
        || meshName.find("walltv") != std::string::npos;
}

Mesh Model::ProcessMesh(aiMesh* mesh, const aiScene* scene, const glm::mat4& nodeTransform)
{
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
    std::vector<Texture> textures;
    const glm::mat3 normalTransform = glm::inverseTranspose(glm::mat3(nodeTransform));
    glm::vec3 meshMin(std::numeric_limits<float>::max());
    glm::vec3 meshMax(std::numeric_limits<float>::lowest());

    vertices.reserve(mesh->mNumVertices);
    for (unsigned int vertexIndex = 0; vertexIndex < mesh->mNumVertices; ++vertexIndex)
    {
        Vertex vertex {};
        const glm::vec4 sourcePosition(
            mesh->mVertices[vertexIndex].x,
            mesh->mVertices[vertexIndex].y,
            mesh->mVertices[vertexIndex].z,
            1.0f);
        vertex.position = glm::vec3(nodeTransform * sourcePosition);
        ExpandBounds(vertex.position);
        meshMin = glm::min(meshMin, vertex.position);
        meshMax = glm::max(meshMax, vertex.position);

        if (mesh->HasNormals())
        {
            const glm::vec3 sourceNormal(
                mesh->mNormals[vertexIndex].x,
                mesh->mNormals[vertexIndex].y,
                mesh->mNormals[vertexIndex].z);
            vertex.normal = glm::normalize(normalTransform * sourceNormal);
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
        if (face.mNumIndices == 3)
        {
            ++sourceTriangleCount_;
            const unsigned int indexA = face.mIndices[0];
            const unsigned int indexB = face.mIndices[1];
            const unsigned int indexC = face.mIndices[2];
            if (indexA >= vertices.size() || indexB >= vertices.size() || indexC >= vertices.size())
            {
                continue;
            }

            const glm::vec3& a = vertices[indexA].position;
            const glm::vec3& b = vertices[indexB].position;
            const glm::vec3& c = vertices[indexC].position;
            if (IsDegenerateTriangle(a, b, c))
            {
                ++removedDegenerateTriangleCount_;
                continue;
            }

            const std::array<long long, 9> key = TriangleKey(a, b, c);
            if (!triangleKeys_.insert(key).second)
            {
                ++removedDuplicateTriangleCount_;
                continue;
            }

            indices.push_back(indexA);
            indices.push_back(indexB);
            indices.push_back(indexC);
            continue;
        }

        for (unsigned int index = 0; index < face.mNumIndices; ++index)
        {
            indices.push_back(face.mIndices[index]);
        }
    }

    if (mesh->mNumVertices > 0)
    {
        NamedBounds bounds;
        bounds.name = mesh->mName.length > 0
            ? std::string(mesh->mName.C_Str())
            : ("mesh_" + std::to_string(namedBounds_.size()));
        bounds.min = meshMin;
        bounds.max = meshMax;
        namedBounds_.push_back(std::move(bounds));
    }

    if (loadTextures_ && mesh->mMaterialIndex >= 0)
    {
        aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];
        std::vector<Texture> diffuseTextures = LoadMaterialTextures(material, scene, aiTextureType_DIFFUSE, "texture_diffuse");
        if (diffuseTextures.empty())
        {
            diffuseTextures = LoadMaterialTextures(material, scene, aiTextureType_BASE_COLOR, "texture_diffuse");
        }
        if (diffuseTextures.empty())
        {
            const std::optional<Texture> generatedTreeTexture = TryBuildGeneratedTreeTexture(mesh, "texture_diffuse");
            if (generatedTreeTexture.has_value())
            {
                diffuseTextures.push_back(*generatedTreeTexture);
            }
        }
        if (diffuseTextures.empty())
        {
            const std::optional<Texture> materialColorTexture = TryBuildMaterialColorTexture(material, "texture_diffuse");
            if (materialColorTexture.has_value())
            {
                diffuseTextures.push_back(*materialColorTexture);
            }
        }
        textures.insert(textures.end(), diffuseTextures.begin(), diffuseTextures.end());
    }

    if (loadTextures_ && textures.empty())
    {
        const std::optional<Texture> fallbackTexture = TryLoadFallbackTexture("texture_diffuse");
        if (fallbackTexture.has_value())
        {
            textures.push_back(*fallbackTexture);
        }
    }

    return Mesh(std::move(vertices), std::move(indices), std::move(textures));
}

std::vector<Texture> Model::LoadMaterialTextures(
    aiMaterial* material,
    const aiScene* scene,
    int type,
    const std::string& typeName)
{
    std::vector<Texture> textures;

    for (unsigned int index = 0; index < material->GetTextureCount(static_cast<aiTextureType>(type)); ++index)
    {
        aiString textureName;
        material->GetTexture(static_cast<aiTextureType>(type), index, &textureName);
        const std::string textureReference = NormalizeTextureReference(textureName.C_Str());

        if (const aiTexture* embeddedTexture = scene->GetEmbeddedTexture(textureReference.c_str()))
        {
            const std::string embeddedKey = "embedded:" + textureReference;
            const auto existing = std::find_if(
                texturesLoaded_.begin(),
                texturesLoaded_.end(),
                [&embeddedKey](const Texture& texture) { return texture.path == embeddedKey; });

            if (existing != texturesLoaded_.end())
            {
                textures.push_back(*existing);
                continue;
            }

            try
            {
                Texture texture;
                texture.id = LoadEmbeddedTexture2D(*embeddedTexture);
                texture.type = typeName;
                texture.path = embeddedKey;
                textures.push_back(texture);
                texturesLoaded_.push_back(texture);
            }
            catch (const std::exception& error)
            {
                std::cerr << error.what() << '\n';
            }

            continue;
        }

        const std::optional<fs::path> texturePath = ResolveTexturePath(textureReference);
        if (!texturePath.has_value())
        {
            continue;
        }

        const std::string normalizedPath = texturePath->lexically_normal().string();

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
            texture.id = LoadTexture2D(*texturePath);
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

std::optional<Texture> Model::TryBuildMaterialColorTexture(aiMaterial* material, const std::string& typeName)
{
    aiColor4D baseColor(1.0f, 1.0f, 1.0f, 1.0f);
    bool hasMaterialColor = false;

#ifdef AI_MATKEY_BASE_COLOR
    if (material->Get(AI_MATKEY_BASE_COLOR, baseColor) == aiReturn_SUCCESS)
    {
        hasMaterialColor = true;
    }
#endif

    if (!hasMaterialColor)
    {
        aiColor3D diffuseColor(1.0f, 1.0f, 1.0f);
        if (material->Get(AI_MATKEY_COLOR_DIFFUSE, diffuseColor) == aiReturn_SUCCESS)
        {
            baseColor.r = diffuseColor.r;
            baseColor.g = diffuseColor.g;
            baseColor.b = diffuseColor.b;
            hasMaterialColor = true;
        }
    }

    if (!hasMaterialColor)
    {
        return std::nullopt;
    }

    float opacity = 1.0f;
    material->Get(AI_MATKEY_OPACITY, opacity);
    baseColor.a = std::clamp(baseColor.a * opacity, 0.0f, 1.0f);

    const unsigned char red = ToByte(baseColor.r);
    const unsigned char green = ToByte(baseColor.g);
    const unsigned char blue = ToByte(baseColor.b);
    const unsigned char alpha = ToByte(baseColor.a);

    const std::string generatedKey = "generated:"
        + std::to_string(static_cast<int>(red)) + ","
        + std::to_string(static_cast<int>(green)) + ","
        + std::to_string(static_cast<int>(blue)) + ","
        + std::to_string(static_cast<int>(alpha));

    const auto existing = std::find_if(
        texturesLoaded_.begin(),
        texturesLoaded_.end(),
        [&generatedKey](const Texture& texture) { return texture.path == generatedKey; });

    if (existing != texturesLoaded_.end())
    {
        return *existing;
    }

    Texture texture;
    texture.id = CreateSolidTexture2D(red, green, blue, alpha);
    texture.type = typeName;
    texture.path = generatedKey;
    texturesLoaded_.push_back(texture);
    return texture;
}

std::optional<Texture> Model::TryBuildGeneratedTreeTexture(const aiMesh* mesh, const std::string& typeName)
{
    if (sourceFilename_ != "tree.fbx" || mesh == nullptr)
    {
        return std::nullopt;
    }

    const std::string meshName = ToLowerAscii(mesh->mName.C_Str());
    const bool foliage = meshName.find("ico") != std::string::npos
        || meshName.find("leaf") != std::string::npos
        || meshName.find("foliage") != std::string::npos;
    const std::string generatedKey = foliage ? "generated:tree:foliage" : "generated:tree:bark";

    const auto existing = std::find_if(
        texturesLoaded_.begin(),
        texturesLoaded_.end(),
        [&generatedKey](const Texture& texture) { return texture.path == generatedKey; });
    if (existing != texturesLoaded_.end())
    {
        return *existing;
    }

    constexpr int kTextureSize = 64;
    std::vector<unsigned char> pixels(static_cast<std::size_t>(kTextureSize * kTextureSize * 4), 255u);
    for (int y = 0; y < kTextureSize; ++y)
    {
        for (int x = 0; x < kTextureSize; ++x)
        {
            const unsigned char noise = HashNoise(x, y, foliage ? 17 : 31);
            const std::size_t offset = static_cast<std::size_t>((y * kTextureSize + x) * 4);
            if (foliage)
            {
                const int vein = ((x + (y / 3)) % 13) == 0 ? 18 : 0;
                pixels[offset + 0u] = static_cast<unsigned char>(std::clamp(38 + (noise / 18) - vein, 18, 78));
                pixels[offset + 1u] = static_cast<unsigned char>(std::clamp(98 + (noise / 5) + vein, 70, 168));
                pixels[offset + 2u] = static_cast<unsigned char>(std::clamp(34 + (noise / 20), 24, 72));
            }
            else
            {
                const int stripe = ((x + (noise / 24)) % 11) < 4 ? 30 : -8;
                pixels[offset + 0u] = static_cast<unsigned char>(std::clamp(96 + stripe + (noise / 18), 48, 142));
                pixels[offset + 1u] = static_cast<unsigned char>(std::clamp(58 + (stripe / 2) + (noise / 30), 30, 92));
                pixels[offset + 2u] = static_cast<unsigned char>(std::clamp(31 + (noise / 36), 18, 58));
            }
            pixels[offset + 3u] = 255u;
        }
    }

    Texture texture;
    texture.id = CreateTexture2DFromRgbaPixels(kTextureSize, kTextureSize, pixels);
    texture.type = typeName;
    texture.path = generatedKey;
    texturesLoaded_.push_back(texture);
    return texture;
}

std::optional<Texture> Model::TryLoadFallbackTexture(const std::string& typeName)
{
    bool hasLocalTextureContainer = directory_.filename() == "source" || fs::exists(directory_ / "textures");
    if (!hasLocalTextureContainer && fs::exists(directory_) && fs::is_directory(directory_))
    {
        for (const fs::directory_entry& entry : fs::directory_iterator(directory_))
        {
            if (entry.is_regular_file() && IsImageFile(entry.path()))
            {
                hasLocalTextureContainer = true;
                break;
            }
        }
    }

    if (!hasLocalTextureContainer)
    {
        return std::nullopt;
    }

    const std::vector<fs::path> searchRoots = BuildLocalSearchRoots(directory_);

    fs::path bestPath;
    int bestScore = std::numeric_limits<int>::min();

    for (const fs::path& root : searchRoots)
    {
        if (!fs::exists(root) || !fs::is_directory(root))
        {
            continue;
        }

        for (const fs::directory_entry& entry : fs::recursive_directory_iterator(root))
        {
            if (!entry.is_regular_file() || !IsImageFile(entry.path()))
            {
                continue;
            }

            const int score = ScoreDiffuseCandidate(entry.path());
            if (score > bestScore)
            {
                bestScore = score;
                bestPath = entry.path();
            }
        }

        if (!bestPath.empty() && bestScore >= 0)
        {
            break;
        }
    }

    if (bestPath.empty())
    {
        return std::nullopt;
    }

    const std::string normalizedPath = bestPath.lexically_normal().string();
    const auto existing = std::find_if(
        texturesLoaded_.begin(),
        texturesLoaded_.end(),
        [&normalizedPath](const Texture& texture) { return texture.path == normalizedPath; });

    if (existing != texturesLoaded_.end())
    {
        return *existing;
    }

    try
    {
        Texture texture;
        texture.id = LoadTexture2D(bestPath);
        texture.type = typeName;
        texture.path = normalizedPath;
        texturesLoaded_.push_back(texture);
        return texture;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
    }

    return std::nullopt;
}

std::optional<fs::path> Model::ResolveTexturePath(const std::string& textureReference) const
{
    if (textureReference.empty())
    {
        return std::nullopt;
    }

    const fs::path referencePath(textureReference);
    if (referencePath.is_absolute() && fs::exists(referencePath) && fs::is_regular_file(referencePath))
    {
        return referencePath;
    }

    const fs::path filename = referencePath.filename();
    if (filename.empty())
    {
        return std::nullopt;
    }

    std::vector<fs::path> candidatePaths;
    candidatePaths.push_back((directory_ / referencePath).lexically_normal());
    candidatePaths.push_back((directory_ / filename).lexically_normal());
    candidatePaths.push_back((directory_ / "textures" / filename).lexically_normal());

    const std::vector<fs::path> searchRoots = BuildLocalSearchRoots(directory_);
    for (const fs::path& root : searchRoots)
    {
        candidatePaths.push_back((root / filename).lexically_normal());
        candidatePaths.push_back((root / "textures" / filename).lexically_normal());
    }

    for (const fs::path& candidate : candidatePaths)
    {
        if (fs::exists(candidate) && fs::is_regular_file(candidate))
        {
            return candidate;
        }
    }

    const std::string targetFilename = ToLowerAscii(filename.string());
    const std::string targetKey = NormalizeLookupKey(filename.stem().string());
    std::vector<fs::path> looseMatches;
    std::unordered_set<std::string> uniqueImageKeys;
    fs::path firstImageCandidate;

    for (const fs::path& currentRoot : searchRoots)
    {
        if (!fs::exists(currentRoot) || !fs::is_directory(currentRoot))
        {
            continue;
        }

        for (const fs::directory_entry& entry : fs::recursive_directory_iterator(currentRoot))
        {
            if (!entry.is_regular_file() || !IsImageFile(entry.path()))
            {
                continue;
            }

            const std::string candidateFilename = ToLowerAscii(entry.path().filename().string());
            const std::string candidateKey = NormalizeLookupKey(entry.path().stem().string());
            uniqueImageKeys.insert(candidateKey);

            if (firstImageCandidate.empty())
            {
                firstImageCandidate = entry.path();
            }

            if (candidateFilename == targetFilename)
            {
                return entry.path();
            }

            if (candidateKey == targetKey
                || StartsWith(candidateKey, targetKey)
                || StartsWith(targetKey, candidateKey))
            {
                looseMatches.push_back(entry.path());
            }
        }
    }

    if (!looseMatches.empty())
    {
        const auto bestMatch = std::max_element(
            looseMatches.begin(),
            looseMatches.end(),
            [](const fs::path& left, const fs::path& right)
            {
                return ScoreDiffuseCandidate(left) < ScoreDiffuseCandidate(right);
            });
        return *bestMatch;
    }

    if (!firstImageCandidate.empty() && uniqueImageKeys.size() == 1)
    {
        return firstImageCandidate;
    }

    return std::nullopt;
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
