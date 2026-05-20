#pragma once

#include <array>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include <glm/glm.hpp>

#include "Mesh.h"

class Model
{
public:
    struct NamedBounds
    {
        std::string name;
        glm::vec3 min { 0.0f };
        glm::vec3 max { 0.0f };
    };

    struct NamedNode
    {
        std::string name;
        glm::mat4 transform { 1.0f };
    };

    explicit Model(const std::filesystem::path& path, bool loadTextures = true);

    void Draw() const;
    void DrawWithoutTextures() const;

    [[nodiscard]] bool IsLoaded() const noexcept;
    [[nodiscard]] bool HasTextures() const noexcept;
    [[nodiscard]] glm::vec3 GetMinBounds() const noexcept;
    [[nodiscard]] glm::vec3 GetMaxBounds() const noexcept;
    [[nodiscard]] glm::vec3 GetCenter() const noexcept;
    [[nodiscard]] glm::vec3 GetSize() const noexcept;
    [[nodiscard]] const std::vector<NamedBounds>& GetNamedBounds() const noexcept;
    [[nodiscard]] const std::vector<NamedNode>& GetNamedNodes() const noexcept;

private:
    std::vector<Mesh> meshes_;
    std::vector<NamedBounds> namedBounds_;
    std::vector<NamedNode> namedNodes_;
    std::vector<Texture> texturesLoaded_;
    std::filesystem::path directory_;
    std::string sourceFilename_;
    bool loadTextures_ = true;
    bool isLoaded_ = false;
    glm::vec3 minBounds_ { 0.0f };
    glm::vec3 maxBounds_ { 0.0f };
    std::size_t sourceTriangleCount_ = 0;
    std::size_t removedDuplicateTriangleCount_ = 0;
    std::size_t removedDegenerateTriangleCount_ = 0;

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
    void ProcessNode(struct aiNode* node, const struct aiScene* scene, const glm::mat4& parentTransform);
    Mesh ProcessMesh(struct aiMesh* mesh, const struct aiScene* scene, const glm::mat4& nodeTransform);
    [[nodiscard]] bool ShouldSkipNode(const struct aiNode* node) const;
    [[nodiscard]] bool ShouldSkipMesh(const struct aiMesh* mesh) const;
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
