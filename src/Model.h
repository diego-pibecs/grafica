#pragma once

#include <array>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "Mesh.h"

class ShaderProgram;

struct ModelLoadOptions
{
    std::string onlyNodeName;
    bool includeChildren = true;
    bool bakeSkinnedMeshes = false;
    bool preserveFbxPivots = true;
    float globalScaleFactor = 1.0f;
};

class Model
{
public:
    enum class SkinningSpaceMode
    {
        SceneRootInverse,
        NoGlobalInverse,
        RootScaleCompensated
    };

    struct NamedBounds
    {
        std::string name;
        glm::vec3 min { 0.0f };
        glm::vec3 max { 0.0f };
    };

    struct NodeMarker
    {
        std::string name;
        std::string path;
        glm::mat4 transform { 1.0f };
        glm::vec3 position { 0.0f };
        bool hasMesh = false;
    };

    explicit Model(const std::filesystem::path& path, bool loadTextures = true);
    Model(const std::filesystem::path& path, bool loadTextures, ModelLoadOptions options);

    void Draw() const;
    void DrawWithoutTextures() const;
    bool ApplyAnimation(
        const std::string& preferredClipName,
        float timeSeconds,
        SkinningSpaceMode spaceMode = SkinningSpaceMode::SceneRootInverse);
    bool ApplyBindPose(SkinningSpaceMode spaceMode = SkinningSpaceMode::SceneRootInverse);
    void UploadBoneMatrices(const ShaderProgram& shader) const;
    void UploadIdentityBoneMatrices(const ShaderProgram& shader) const;

    [[nodiscard]] bool IsLoaded() const noexcept;
    [[nodiscard]] bool HasTextures() const noexcept;
    [[nodiscard]] bool HasSkeletalAnimation() const noexcept;
    [[nodiscard]] glm::vec3 GetMinBounds() const noexcept;
    [[nodiscard]] glm::vec3 GetMaxBounds() const noexcept;
    [[nodiscard]] glm::vec3 GetCenter() const noexcept;
    [[nodiscard]] glm::vec3 GetSize() const noexcept;
    [[nodiscard]] const std::vector<NamedBounds>& GetNamedBounds() const noexcept;
    [[nodiscard]] const std::vector<NodeMarker>& GetNodeMarkers() const noexcept;
    [[nodiscard]] std::size_t GetMeshCount() const noexcept;
    [[nodiscard]] std::size_t GetAnimationCount() const noexcept;
    [[nodiscard]] std::size_t GetBoneCount() const noexcept;
    [[nodiscard]] std::size_t GetVertexCount() const noexcept;
    [[nodiscard]] std::size_t GetVerticesWithoutWeights() const noexcept;
    [[nodiscard]] std::size_t GetNonUnitWeightVertexCount() const noexcept;
    [[nodiscard]] int GetMaxOriginalInfluences() const noexcept;
    [[nodiscard]] std::size_t GetDiscardedInfluenceCount() const noexcept;
    static void InspectAssimpScene(const std::filesystem::path& path);

private:
    static constexpr int kMaxBones = 100;

    struct BoneInfo
    {
        int id = -1;
        std::string nodeName;
        std::string key;
        glm::mat4 offset { 1.0f };
    };

    struct KeyPosition
    {
        glm::vec3 position { 0.0f };
        float timeStamp = 0.0f;
    };

    struct KeyRotation
    {
        glm::quat orientation { 1.0f, 0.0f, 0.0f, 0.0f };
        float timeStamp = 0.0f;
    };

    struct KeyScale
    {
        glm::vec3 scale { 1.0f };
        float timeStamp = 0.0f;
    };

    struct BoneChannel
    {
        std::string name;
        std::vector<KeyPosition> positions;
        std::vector<KeyRotation> rotations;
        std::vector<KeyScale> scales;
    };

    struct AnimationClip
    {
        std::string name;
        float duration = 0.0f;
        float ticksPerSecond = 25.0f;
        std::unordered_map<std::string, BoneChannel> channels;
    };

    struct AnimationNode
    {
        std::string name;
        glm::mat4 transform { 1.0f };
        std::vector<AnimationNode> children;
    };

    std::vector<Mesh> meshes_;
    std::vector<NamedBounds> namedBounds_;
    std::vector<NodeMarker> nodeMarkers_;
    std::vector<Texture> texturesLoaded_;
    std::filesystem::path directory_;
    std::string sourceFilename_;
    ModelLoadOptions loadOptions_;
    AnimationNode rootAnimationNode_;
    std::unordered_map<std::string, BoneInfo> boneInfoMap_;
    std::unordered_map<std::string, std::vector<int>> boneIdsByNodeName_;
    std::vector<BoneInfo> boneInfosById_;
    std::vector<AnimationClip> animationClips_;
    std::vector<glm::mat4> finalBoneMatrices_;
    std::vector<std::string> boneNamesById_;
    glm::mat4 globalInverseTransform_ { 1.0f };
    std::string activeAnimationClip_;
    int boneCounter_ = 0;
    bool boneLimitExceeded_ = false;
    bool loadTextures_ = true;
    bool isLoaded_ = false;
    glm::vec3 minBounds_ { 0.0f };
    glm::vec3 maxBounds_ { 0.0f };
    std::size_t sourceTriangleCount_ = 0;
    std::size_t removedDuplicateTriangleCount_ = 0;
    std::size_t removedDegenerateTriangleCount_ = 0;
    std::size_t animationCount_ = 0;
    std::size_t boneCount_ = 0;
    std::size_t vertexCount_ = 0;
    std::size_t verticesWithoutWeights_ = 0;
    std::size_t nonUnitWeightVertices_ = 0;
    std::size_t discardedInfluences_ = 0;
    int maxOriginalInfluences_ = 0;
    bool kirbyScaleDiagnosticsLogged_ = false;
    mutable std::unordered_set<std::string> kirbyLoggedScaleLabels_;

    struct QuantizedTriangleKeyHash
    {
        std::size_t operator()(const std::array<long long, 9>& key) const noexcept
        {
            std::size_t seed = 1469598103934665603ull;
            for (long long value : key)
            {
                seed ^= std::hash<long long> {}(value);
                seed *= 1099511628211ull;
            }
            return seed;
        }
    };

    std::unordered_set<std::array<long long, 9>, QuantizedTriangleKeyHash> triangleKeys_;

    void LoadModel(const std::filesystem::path& path);
    [[nodiscard]] const struct aiNode* FindNodeByName(const struct aiNode* node, const std::string& nodeName) const;
    AnimationNode BuildAnimationNodeTree(const struct aiNode* node) const;
    void LoadAnimations(const struct aiScene* scene);
    void ExtractBoneWeights(struct aiMesh* mesh, std::vector<Vertex>& vertices);
    void SetVertexBoneData(Vertex& vertex, int boneId, float weight) const;
    [[nodiscard]] const AnimationClip* FindAnimationClip(const std::string& preferredClipName) const;
    [[nodiscard]] bool AnimationNodeContains(const AnimationNode& node, const std::string& name) const;
    [[nodiscard]] bool ClipHasChannelForNode(const std::string& nodeName) const;
    void CalculateBoneTransform(
        const AnimationNode& node,
        const glm::mat4& parentTransform,
        const AnimationClip& clip,
        float animationTime,
        SkinningSpaceMode spaceMode);
    void CalculateBindPoseTransform(
        const AnimationNode& node,
        const glm::mat4& parentTransform,
        SkinningSpaceMode spaceMode);
    [[nodiscard]] glm::mat4 BuildFinalBoneMatrix(
        const glm::mat4& globalTransform,
        const glm::mat4& offset,
        SkinningSpaceMode spaceMode) const;
    void LogKirbyScaleDiagnostics(const std::string& label) const;
    [[nodiscard]] glm::mat4 InterpolateChannelTransform(const BoneChannel& channel, float animationTime) const;
    void ProcessNode(
        struct aiNode* node,
        const struct aiScene* scene,
        const glm::mat4& parentTransform,
        const std::string& parentPath);
    Mesh ProcessMesh(struct aiMesh* mesh, const struct aiScene* scene, const glm::mat4& nodeTransform);
    std::vector<Texture> LoadMaterialTextures(
        struct aiMaterial* material,
        const struct aiScene* scene,
        int type,
        const std::string& typeName);
    std::optional<Texture> TryBuildMaterialColorTexture(struct aiMaterial* material, const std::string& typeName);
    std::optional<Texture> TryBuildGeneratedTreeTexture(const struct aiMesh* mesh, const std::string& typeName);
    std::optional<Texture> TryLoadFallbackTexture(const std::string& typeName);
    std::optional<std::filesystem::path> ResolveTexturePath(const std::string& textureReference) const;
    void ExpandBounds(const glm::vec3& position);
};
