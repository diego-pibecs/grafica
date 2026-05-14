#include "Model.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <functional>
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
#include <glm/gtc/matrix_transform.hpp>
#include "DebugLog.h"
#include "ShaderProgram.h"
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
    const bool parentHasTextureDir = fs::exists(parent / "textures") || fs::exists(parent / "Textures");
    if (!parent.empty()
        && parent != directory
        && (directory.filename() == "source"
            || directory.filename() == "Low_poly"
            || directory.filename() == "High_poly"
            || parentHasTextureDir))
    {
        searchRoots.push_back(parent);
        if (fs::exists(parent / "textures"))
        {
            searchRoots.push_back(parent / "textures");
        }
        if (fs::exists(parent / "Textures"))
        {
            searchRoots.push_back(parent / "Textures");
        }
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

glm::vec3 AiToGlm(const aiVector3D& value)
{
    return glm::vec3(value.x, value.y, value.z);
}

glm::quat AiToGlm(const aiQuaternion& value)
{
    return glm::quat(value.w, value.x, value.y, value.z);
}

bool IsKirbySourceName(const std::string& sourceFilename)
{
    return sourceFilename.find("kirby") != std::string::npos || sourceFilename.find("kirb") != std::string::npos;
}
}

Model::Model(const std::filesystem::path& path, bool loadTextures)
    : loadTextures_(loadTextures)
{
    LoadModel(path);
}

Model::Model(const std::filesystem::path& path, bool loadTextures, ModelLoadOptions options)
    : loadOptions_(std::move(options))
    , loadTextures_(loadTextures)
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

bool Model::ApplyAnimation(const std::string& preferredClipName, float timeSeconds)
{
    if (!HasSkeletalAnimation())
    {
        return false;
    }

    const AnimationClip* clip = FindAnimationClip(preferredClipName);
    if (clip == nullptr || clip->duration <= 0.0f)
    {
        return false;
    }

    const bool clipChanged = activeAnimationClip_ != clip->name;
    if (clipChanged)
    {
        activeAnimationClip_ = clip->name;
        DebugLog::Info("ANIM", "Runtime skeletal clip source=", sourceFilename_, " clip=", activeAnimationClip_);
    }

    const float ticksPerSecond = clip->ticksPerSecond > 0.0f ? clip->ticksPerSecond : 25.0f;
    const float animationTime = std::fmod(timeSeconds * ticksPerSecond, clip->duration);
    std::fill(finalBoneMatrices_.begin(), finalBoneMatrices_.end(), glm::mat4(1.0f));
    CalculateBoneTransform(rootAnimationNode_, glm::mat4(1.0f), *clip, animationTime);
    if (clipChanged && IsKirbySourceName(sourceFilename_))
    {
        bool hasBadMatrix = false;
        float maxAbsValue = 0.0f;
        const std::size_t matrixCount = std::min<std::size_t>(5u, finalBoneMatrices_.size());
        for (std::size_t matrixIndex = 0; matrixIndex < matrixCount; ++matrixIndex)
        {
            const glm::mat4& matrix = finalBoneMatrices_[matrixIndex];
            for (int column = 0; column < 4; ++column)
            {
                for (int row = 0; row < 4; ++row)
                {
                    const float value = matrix[column][row];
                    hasBadMatrix = hasBadMatrix || !std::isfinite(value);
                    maxAbsValue = std::max(maxAbsValue, std::abs(value));
                }
            }
            DebugLog::Info(
                "ANIM",
                "Kirby finalBoneMatrix clip=", clip->name,
                " index=", matrixIndex,
                " row0=(",
                matrix[0][0], ", ", matrix[1][0], ", ", matrix[2][0], ", ", matrix[3][0], ")");
        }
        DebugLog::Info(
            "ANIM",
            "Kirby runtime diagnostics clip=", clip->name,
            " animationTime=", animationTime,
            " hasNaNOrInf=", hasBadMatrix ? "yes" : "no",
            " maxAbsMatrixValue=", maxAbsValue);
    }
    return true;
}

void Model::UploadBoneMatrices(const ShaderProgram& shader) const
{
    shader.SetMat4Array("finalBonesMatrices", finalBoneMatrices_);
}

bool Model::IsLoaded() const noexcept
{
    return isLoaded_;
}

bool Model::HasTextures() const noexcept
{
    return std::any_of(meshes_.begin(), meshes_.end(), [](const Mesh& mesh) { return mesh.HasTexture(); });
}

bool Model::HasSkeletalAnimation() const noexcept
{
    return !boneLimitExceeded_ && !animationClips_.empty() && !boneInfoMap_.empty() && boneCounter_ > 0 && boneCounter_ <= kMaxBones;
}

void Model::InspectAssimpScene(const std::filesystem::path& path)
{
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(
        path.string(),
        aiProcess_Triangulate
            | aiProcess_GenSmoothNormals
            | aiProcess_JoinIdenticalVertices
            | aiProcess_LimitBoneWeights);
    if (scene == nullptr || scene->mRootNode == nullptr)
    {
        DebugLog::Info("FBX INSPECT", "path=", path.string(), " failed=", importer.GetErrorString());
        return;
    }

    DebugLog::Info(
        "FBX INSPECT",
        "path=", path.string(),
        " meshes=", scene->mNumMeshes,
        " materials=", scene->mNumMaterials,
        " animations=", scene->mNumAnimations,
        " root=", scene->mRootNode->mName.C_Str());

    std::function<void(const aiNode*, int)> logNode = [&](const aiNode* node, int depth)
    {
        if (node == nullptr)
        {
            return;
        }
        std::string indent(static_cast<std::size_t>(std::min(depth, 8)) * 2u, ' ');
        std::string meshList;
        for (unsigned int meshRefIndex = 0; meshRefIndex < node->mNumMeshes; ++meshRefIndex)
        {
            const unsigned int meshIndex = node->mMeshes[meshRefIndex];
            const aiMesh* mesh = scene->mMeshes[meshIndex];
            if (!meshList.empty())
            {
                meshList += ", ";
            }
            meshList += std::to_string(meshIndex);
            meshList += ":";
            meshList += mesh != nullptr ? mesh->mName.C_Str() : "<null>";
        }
        DebugLog::Info(
            "FBX INSPECT",
            indent,
            "Node=", node->mName.C_Str(),
            " meshes=[", meshList, "] children=", node->mNumChildren);
        for (unsigned int childIndex = 0; childIndex < node->mNumChildren; ++childIndex)
        {
            logNode(node->mChildren[childIndex], depth + 1);
        }
    };
    logNode(scene->mRootNode, 0);

    for (unsigned int meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex)
    {
        const aiMesh* mesh = scene->mMeshes[meshIndex];
        DebugLog::Info(
            "FBX INSPECT",
            "Mesh ", meshIndex,
            " name=", mesh->mName.C_Str(),
            " vertices=", mesh->mNumVertices,
            " faces=", mesh->mNumFaces,
            " material=", mesh->mMaterialIndex,
            " bones=", mesh->mNumBones);
    }

    for (unsigned int animationIndex = 0; animationIndex < scene->mNumAnimations; ++animationIndex)
    {
        const aiAnimation* animation = scene->mAnimations[animationIndex];
        DebugLog::Info(
            "FBX INSPECT",
            "Clip ", animationIndex,
            " name=", animation->mName.C_Str(),
            " duration=", animation->mDuration,
            " ticksPerSecond=", animation->mTicksPerSecond,
            " channels=", animation->mNumChannels);
    }
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

const std::vector<Model::NodeMarker>& Model::GetNodeMarkers() const noexcept
{
    return nodeMarkers_;
}

void Model::LoadModel(const std::filesystem::path& path)
{
    DebugLog::ScopedTrace trace("Model", path.string());
    minBounds_ = glm::vec3(std::numeric_limits<float>::max());
    maxBounds_ = glm::vec3(std::numeric_limits<float>::lowest());
    namedBounds_.clear();
    nodeMarkers_.clear();
    triangleKeys_.clear();
    sourceTriangleCount_ = 0;
    removedDuplicateTriangleCount_ = 0;
    removedDegenerateTriangleCount_ = 0;
    animationCount_ = 0;
    boneCount_ = 0;
    boneCounter_ = 0;
    boneInfoMap_.clear();
    animationClips_.clear();
    finalBoneMatrices_.assign(static_cast<std::size_t>(kMaxBones), glm::mat4(1.0f));
    activeAnimationClip_.clear();
    boneLimitExceeded_ = false;

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
    animationCount_ = scene->mNumAnimations;
    rootAnimationNode_ = BuildAnimationNodeTree(scene->mRootNode);
    globalInverseTransform_ = glm::inverse(AiToGlm(scene->mRootNode->mTransformation));
    DebugLog::Info(
        "Model",
        "Scene stats path=", path.string(),
        " meshes=", scene->mNumMeshes,
        " materials=", scene->mNumMaterials,
        " textures=", scene->mNumTextures,
        " animations=", scene->mNumAnimations);
    if (IsKirbySourceName(sourceFilename_))
    {
        DebugLog::Info(
            "ANIM",
            "Kirby diagnostics path=", path.string(),
            " rootNode=", scene->mRootNode != nullptr ? scene->mRootNode->mName.C_Str() : "<null>",
            " globalInverseTransform[3]=(",
            globalInverseTransform_[3][0], ", ",
            globalInverseTransform_[3][1], ", ",
            globalInverseTransform_[3][2], ", ",
            globalInverseTransform_[3][3], ")");
    }
    for (unsigned int animationIndex = 0; animationIndex < scene->mNumAnimations; ++animationIndex)
    {
        const aiAnimation* animation = scene->mAnimations[animationIndex];
        DebugLog::Info(
            "ANIM",
            "Animation source=", sourceFilename_,
            " index=", animationIndex,
            " name=", animation->mName.C_Str(),
            " duration=", animation->mDuration,
            " ticksPerSecond=", animation->mTicksPerSecond,
            " channels=", animation->mNumChannels);
        const unsigned int channelLogCount = std::min(animation->mNumChannels, 4u);
        for (unsigned int channelIndex = 0; channelIndex < channelLogCount; ++channelIndex)
        {
            const aiNodeAnim* channel = animation->mChannels[channelIndex];
            DebugLog::Info(
                "ANIM",
                "Channel source=", sourceFilename_,
                " clip=", animationIndex,
                " index=", channelIndex,
                " node=", channel->mNodeName.C_Str(),
                " positionKeys=", channel->mNumPositionKeys,
                " rotationKeys=", channel->mNumRotationKeys,
                " scalingKeys=", channel->mNumScalingKeys);
        }
    }
    LoadAnimations(scene);
    if (!loadOptions_.onlyNodeName.empty())
    {
        const aiNode* selectedNode = FindNodeByName(scene->mRootNode, loadOptions_.onlyNodeName);
        if (selectedNode != nullptr)
        {
            DebugLog::Info("FBX INSPECT", "Selected node for load source=", sourceFilename_, " node=", loadOptions_.onlyNodeName);
            ProcessNode(const_cast<aiNode*>(selectedNode), scene, glm::mat4(1.0f), "");
        }
        else
        {
            DebugLog::Info("FBX INSPECT", "Requested node not found source=", sourceFilename_, " node=", loadOptions_.onlyNodeName, " loading full scene");
            ProcessNode(scene->mRootNode, scene, glm::mat4(1.0f), "");
        }
    }
    else
    {
        ProcessNode(scene->mRootNode, scene, glm::mat4(1.0f), "");
    }
    DebugLog::Info(
        "ANIM",
        "Model=", sourceFilename_,
        " animations=", animationCount_,
        " bones=", boneCount_,
        " supportsSkeletalAnimation=", (HasSkeletalAnimation() ? "yes" : "no"),
        " boneLimitExceeded=", (boneLimitExceeded_ ? "yes" : "no"),
        " runtimeMode=", (HasSkeletalAnimation() ? "skinning-runtime" : "static/procedural"));
    isLoaded_ = !meshes_.empty();
    DebugLog::Info(
        "Model",
        "Model load result path=", path.string(),
        " loaded=", isLoaded_,
        " meshCount=", meshes_.size(),
        " nodeMarkers=", nodeMarkers_.size(),
        " namedBounds=", namedBounds_.size(),
        " sourceTriangles=", sourceTriangleCount_,
        " removedDuplicates=", removedDuplicateTriangleCount_,
        " removedDegenerate=", removedDegenerateTriangleCount_);
    triangleKeys_.clear();
}

const aiNode* Model::FindNodeByName(const aiNode* node, const std::string& nodeName) const
{
    if (node == nullptr)
    {
        return nullptr;
    }
    if (node->mName.C_Str() == nodeName)
    {
        return node;
    }
    for (unsigned int childIndex = 0; childIndex < node->mNumChildren; ++childIndex)
    {
        if (const aiNode* result = FindNodeByName(node->mChildren[childIndex], nodeName))
        {
            return result;
        }
    }
    return nullptr;
}

Model::AnimationNode Model::BuildAnimationNodeTree(const aiNode* node) const
{
    AnimationNode result;
    if (node == nullptr)
    {
        return result;
    }

    result.name = node->mName.length > 0 ? std::string(node->mName.C_Str()) : "node";
    result.transform = AiToGlm(node->mTransformation);
    result.children.reserve(node->mNumChildren);
    for (unsigned int childIndex = 0; childIndex < node->mNumChildren; ++childIndex)
    {
        result.children.push_back(BuildAnimationNodeTree(node->mChildren[childIndex]));
    }
    return result;
}

void Model::LoadAnimations(const aiScene* scene)
{
    animationClips_.clear();
    if (scene == nullptr)
    {
        return;
    }

    animationClips_.reserve(scene->mNumAnimations);
    for (unsigned int animationIndex = 0; animationIndex < scene->mNumAnimations; ++animationIndex)
    {
        const aiAnimation* sourceAnimation = scene->mAnimations[animationIndex];
        AnimationClip clip;
        clip.name = sourceAnimation->mName.length > 0
            ? std::string(sourceAnimation->mName.C_Str())
            : ("Animation " + std::to_string(animationIndex));
        clip.duration = static_cast<float>(sourceAnimation->mDuration);
        clip.ticksPerSecond = sourceAnimation->mTicksPerSecond > 0.0
            ? static_cast<float>(sourceAnimation->mTicksPerSecond)
            : 25.0f;

        for (unsigned int channelIndex = 0; channelIndex < sourceAnimation->mNumChannels; ++channelIndex)
        {
            const aiNodeAnim* sourceChannel = sourceAnimation->mChannels[channelIndex];
            BoneChannel channel;
            channel.name = sourceChannel->mNodeName.C_Str();
            channel.positions.reserve(sourceChannel->mNumPositionKeys);
            channel.rotations.reserve(sourceChannel->mNumRotationKeys);
            channel.scales.reserve(sourceChannel->mNumScalingKeys);

            for (unsigned int keyIndex = 0; keyIndex < sourceChannel->mNumPositionKeys; ++keyIndex)
            {
                channel.positions.push_back(KeyPosition {
                    AiToGlm(sourceChannel->mPositionKeys[keyIndex].mValue),
                    static_cast<float>(sourceChannel->mPositionKeys[keyIndex].mTime)
                });
            }
            for (unsigned int keyIndex = 0; keyIndex < sourceChannel->mNumRotationKeys; ++keyIndex)
            {
                channel.rotations.push_back(KeyRotation {
                    glm::normalize(AiToGlm(sourceChannel->mRotationKeys[keyIndex].mValue)),
                    static_cast<float>(sourceChannel->mRotationKeys[keyIndex].mTime)
                });
            }
            for (unsigned int keyIndex = 0; keyIndex < sourceChannel->mNumScalingKeys; ++keyIndex)
            {
                channel.scales.push_back(KeyScale {
                    AiToGlm(sourceChannel->mScalingKeys[keyIndex].mValue),
                    static_cast<float>(sourceChannel->mScalingKeys[keyIndex].mTime)
                });
            }
            clip.channels[channel.name] = std::move(channel);
        }

        animationClips_.push_back(std::move(clip));
    }
}

void Model::SetVertexBoneData(Vertex& vertex, int boneId, float weight) const
{
    for (int index = 0; index < 4; ++index)
    {
        if (vertex.boneIDs[index] < 0)
        {
            vertex.boneIDs[index] = boneId;
            vertex.weights[index] = weight;
            return;
        }
    }
}

void Model::ExtractBoneWeights(aiMesh* mesh, std::vector<Vertex>& vertices)
{
    if (mesh == nullptr)
    {
        return;
    }

    std::vector<int> originalInfluenceCounts(vertices.size(), 0);
    std::size_t discardedInfluences = 0;
    const bool kirbyDiagnostics = IsKirbySourceName(sourceFilename_);

    for (unsigned int boneIndex = 0; boneIndex < mesh->mNumBones; ++boneIndex)
    {
        aiBone* sourceBone = mesh->mBones[boneIndex];
        const std::string boneName = sourceBone->mName.C_Str();
        int boneId = -1;
        const auto existingBone = boneInfoMap_.find(boneName);
        if (existingBone == boneInfoMap_.end())
        {
            if (boneCounter_ >= kMaxBones)
            {
                if (!boneLimitExceeded_)
                {
                    DebugLog::Info("ANIM", "Bone limit exceeded source=", sourceFilename_, " limit=", kMaxBones);
                }
                boneLimitExceeded_ = true;
                continue;
            }

            boneId = boneCounter_;
            BoneInfo info;
            info.id = boneId;
            info.offset = AiToGlm(sourceBone->mOffsetMatrix);
            boneInfoMap_[boneName] = info;
            boneCounter_ += 1;
        }
        else
        {
            boneId = existingBone->second.id;
        }

        if (kirbyDiagnostics)
        {
            const glm::mat4 offset = AiToGlm(sourceBone->mOffsetMatrix);
            DebugLog::Info(
                "ANIM",
                "Kirby bone mesh=", mesh->mName.C_Str(),
                " id=", boneId,
                " name=", boneName,
                " hasNode=", AnimationNodeContains(rootAnimationNode_, boneName) ? "yes" : "no",
                " hasChannel=", ClipHasChannelForNode(boneName) ? "yes" : "no",
                " offset[3]=(",
                offset[3][0], ", ",
                offset[3][1], ", ",
                offset[3][2], ", ",
                offset[3][3], ")");
        }

        for (unsigned int weightIndex = 0; weightIndex < sourceBone->mNumWeights; ++weightIndex)
        {
            const aiVertexWeight& sourceWeight = sourceBone->mWeights[weightIndex];
            if (sourceWeight.mVertexId < vertices.size())
            {
                originalInfluenceCounts[sourceWeight.mVertexId] += 1;
                if (originalInfluenceCounts[sourceWeight.mVertexId] > 4)
                {
                    discardedInfluences += 1;
                }
                SetVertexBoneData(vertices[sourceWeight.mVertexId], boneId, sourceWeight.mWeight);
            }
        }
    }

    std::size_t verticesWithoutWeights = 0;
    std::size_t nonUnitWeightVertices = 0;
    int maxOriginalInfluences = 0;
    for (Vertex& vertex : vertices)
    {
        const float weightSum = vertex.weights.x + vertex.weights.y + vertex.weights.z + vertex.weights.w;
        if (weightSum > 0.0f)
        {
            if (std::abs(weightSum - 1.0f) > 0.01f)
            {
                nonUnitWeightVertices += 1;
            }
            vertex.weights /= weightSum;
        }
        else
        {
            verticesWithoutWeights += 1;
        }
    }
    for (int influenceCount : originalInfluenceCounts)
    {
        maxOriginalInfluences = std::max(maxOriginalInfluences, influenceCount);
    }

    if (kirbyDiagnostics)
    {
        DebugLog::Info(
            "ANIM",
            "Kirby mesh weights mesh=", mesh->mName.C_Str(),
            " vertices=", vertices.size(),
            " bones=", mesh->mNumBones,
            " verticesWithoutWeights=", verticesWithoutWeights,
            " nonUnitWeightVerticesBeforeNormalize=", nonUnitWeightVertices,
            " maxOriginalInfluences=", maxOriginalInfluences,
            " discardedInfluences=", discardedInfluences);
    }
}

const Model::AnimationClip* Model::FindAnimationClip(const std::string& preferredClipName) const
{
    if (animationClips_.empty())
    {
        return nullptr;
    }

    const std::string preferred = ToLowerAscii(preferredClipName);
    if (!preferred.empty())
    {
        for (const AnimationClip& clip : animationClips_)
        {
            if (ToLowerAscii(clip.name).find(preferred) != std::string::npos)
            {
                return &clip;
            }
        }
    }
    return &animationClips_.front();
}

bool Model::AnimationNodeContains(const AnimationNode& node, const std::string& name) const
{
    if (node.name == name)
    {
        return true;
    }
    for (const AnimationNode& child : node.children)
    {
        if (AnimationNodeContains(child, name))
        {
            return true;
        }
    }
    return false;
}

bool Model::ClipHasChannelForNode(const std::string& nodeName) const
{
    for (const AnimationClip& clip : animationClips_)
    {
        if (clip.channels.find(nodeName) != clip.channels.end())
        {
            return true;
        }
    }
    return false;
}

glm::mat4 Model::InterpolateChannelTransform(const BoneChannel& channel, float animationTime) const
{
    glm::vec3 position(0.0f);
    if (channel.positions.size() == 1u)
    {
        position = channel.positions.front().position;
    }
    else if (!channel.positions.empty())
    {
        std::size_t keyIndex = 0;
        while (keyIndex + 1u < channel.positions.size() && animationTime > channel.positions[keyIndex + 1u].timeStamp)
        {
            keyIndex += 1u;
        }
        const KeyPosition& current = channel.positions[keyIndex];
        const KeyPosition& next = channel.positions[std::min(keyIndex + 1u, channel.positions.size() - 1u)];
        const float span = std::max(next.timeStamp - current.timeStamp, 0.001f);
        const float factor = std::clamp((animationTime - current.timeStamp) / span, 0.0f, 1.0f);
        position = glm::mix(current.position, next.position, factor);
    }

    glm::quat rotation(1.0f, 0.0f, 0.0f, 0.0f);
    if (channel.rotations.size() == 1u)
    {
        rotation = channel.rotations.front().orientation;
    }
    else if (!channel.rotations.empty())
    {
        std::size_t keyIndex = 0;
        while (keyIndex + 1u < channel.rotations.size() && animationTime > channel.rotations[keyIndex + 1u].timeStamp)
        {
            keyIndex += 1u;
        }
        const KeyRotation& current = channel.rotations[keyIndex];
        const KeyRotation& next = channel.rotations[std::min(keyIndex + 1u, channel.rotations.size() - 1u)];
        const float span = std::max(next.timeStamp - current.timeStamp, 0.001f);
        const float factor = std::clamp((animationTime - current.timeStamp) / span, 0.0f, 1.0f);
        rotation = glm::normalize(glm::slerp(current.orientation, next.orientation, factor));
    }

    glm::vec3 scale(1.0f);
    if (channel.scales.size() == 1u)
    {
        scale = channel.scales.front().scale;
    }
    else if (!channel.scales.empty())
    {
        std::size_t keyIndex = 0;
        while (keyIndex + 1u < channel.scales.size() && animationTime > channel.scales[keyIndex + 1u].timeStamp)
        {
            keyIndex += 1u;
        }
        const KeyScale& current = channel.scales[keyIndex];
        const KeyScale& next = channel.scales[std::min(keyIndex + 1u, channel.scales.size() - 1u)];
        const float span = std::max(next.timeStamp - current.timeStamp, 0.001f);
        const float factor = std::clamp((animationTime - current.timeStamp) / span, 0.0f, 1.0f);
        scale = glm::mix(current.scale, next.scale, factor);
    }

    return glm::translate(glm::mat4(1.0f), position)
        * glm::mat4_cast(rotation)
        * glm::scale(glm::mat4(1.0f), scale);
}

void Model::CalculateBoneTransform(const AnimationNode& node, const glm::mat4& parentTransform, const AnimationClip& clip, float animationTime)
{
    glm::mat4 nodeTransform = node.transform;
    const auto channel = clip.channels.find(node.name);
    if (channel != clip.channels.end())
    {
        nodeTransform = InterpolateChannelTransform(channel->second, animationTime);
    }

    const glm::mat4 globalTransform = parentTransform * nodeTransform;
    const auto bone = boneInfoMap_.find(node.name);
    if (bone != boneInfoMap_.end())
    {
        const int id = bone->second.id;
        if (id >= 0 && id < static_cast<int>(finalBoneMatrices_.size()))
        {
            finalBoneMatrices_[static_cast<std::size_t>(id)] = globalInverseTransform_ * globalTransform * bone->second.offset;
        }
    }

    for (const AnimationNode& child : node.children)
    {
        CalculateBoneTransform(child, globalTransform, clip, animationTime);
    }
}

void Model::ProcessNode(
    aiNode* node,
    const aiScene* scene,
    const glm::mat4& parentTransform,
    const std::string& parentPath)
{
    const glm::mat4 nodeTransform = parentTransform * AiToGlm(node->mTransformation);
    const std::string nodeName = node->mName.length > 0 ? std::string(node->mName.C_Str()) : "node";
    const std::string nodePath = parentPath.empty() ? nodeName : parentPath + "/" + nodeName;

    nodeMarkers_.push_back(NodeMarker {
        nodeName,
        nodePath,
        nodeTransform,
        glm::vec3(nodeTransform * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)),
        node->mNumMeshes > 0
    });

    for (unsigned int meshIndex = 0; meshIndex < node->mNumMeshes; ++meshIndex)
    {
        aiMesh* mesh = scene->mMeshes[node->mMeshes[meshIndex]];
        meshes_.push_back(ProcessMesh(mesh, scene, nodeTransform));
    }

    if (!loadOptions_.onlyNodeName.empty() && !loadOptions_.includeChildren && parentPath.empty())
    {
        return;
    }

    for (unsigned int childIndex = 0; childIndex < node->mNumChildren; ++childIndex)
    {
        ProcessNode(node->mChildren[childIndex], scene, nodeTransform, nodePath);
    }
}

Mesh Model::ProcessMesh(aiMesh* mesh, const aiScene* scene, const glm::mat4& nodeTransform)
{
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
    std::vector<Texture> textures;
    if (mesh->mNumBones > 0)
    {
        boneCount_ += mesh->mNumBones;
        DebugLog::Info(
            "ANIM",
            "Mesh bones source=", sourceFilename_,
            " mesh=", mesh->mName.C_Str(),
            " bones=", mesh->mNumBones);
    }
    const bool skinnedMesh = mesh->mNumBones > 0 && !IsKirbySourceName(sourceFilename_);
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
        const glm::vec3 transformedPosition = glm::vec3(nodeTransform * sourcePosition);
        vertex.position = skinnedMesh ? glm::vec3(sourcePosition) : transformedPosition;
        ExpandBounds(transformedPosition);
        meshMin = glm::min(meshMin, transformedPosition);
        meshMax = glm::max(meshMax, transformedPosition);

        if (mesh->HasNormals())
        {
            const glm::vec3 sourceNormal(
                mesh->mNormals[vertexIndex].x,
                mesh->mNormals[vertexIndex].y,
                mesh->mNormals[vertexIndex].z);
            vertex.normal = skinnedMesh ? glm::normalize(sourceNormal) : glm::normalize(normalTransform * sourceNormal);
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

    ExtractBoneWeights(mesh, vertices);

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
            const std::optional<Texture> fallbackTexture = TryLoadFallbackTexture("texture_diffuse");
            if (fallbackTexture.has_value())
            {
                diffuseTextures.push_back(*fallbackTexture);
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

        DebugLog::Info("Model", "Texture requested source=", sourceFilename_, " ref=", textureReference);
        const std::optional<fs::path> texturePath = ResolveTexturePath(textureReference);
        if (!texturePath.has_value())
        {
            DebugLog::Info("Model", "Texture unresolved source=", sourceFilename_, " ref=", textureReference);
            continue;
        }
        DebugLog::Info("Model", "Texture resolved source=", sourceFilename_, " path=", texturePath->string());

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
    bool hasLocalTextureContainer = directory_.filename() == "source"
        || directory_.filename() == "Low_poly"
        || directory_.filename() == "High_poly"
        || fs::exists(directory_ / "textures")
        || fs::exists(directory_ / "Textures")
        || fs::exists(directory_.parent_path() / "textures")
        || fs::exists(directory_.parent_path() / "Textures");
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
        DebugLog::Info("Model", "Fallback texture selected source=", sourceFilename_, " path=", normalizedPath);
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
    candidatePaths.push_back((directory_ / "Textures" / filename).lexically_normal());

    const std::vector<fs::path> searchRoots = BuildLocalSearchRoots(directory_);
    for (const fs::path& root : searchRoots)
    {
        candidatePaths.push_back((root / filename).lexically_normal());
        candidatePaths.push_back((root / "textures" / filename).lexically_normal());
        candidatePaths.push_back((root / "Textures" / filename).lexically_normal());
        candidatePaths.push_back((root / "source" / filename).lexically_normal());
        candidatePaths.push_back((root / "Low_poly" / filename).lexically_normal());
        candidatePaths.push_back((root / "High_poly" / filename).lexically_normal());
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
