#pragma once

#include <filesystem>
#include <vector>

#include <glm/glm.hpp>

#include "Mesh.h"

class Model
{
public:
    explicit Model(const std::filesystem::path& path);

    void Draw() const;

    [[nodiscard]] bool IsLoaded() const noexcept;
    [[nodiscard]] bool HasTextures() const noexcept;
    [[nodiscard]] glm::vec3 GetMinBounds() const noexcept;
    [[nodiscard]] glm::vec3 GetMaxBounds() const noexcept;
    [[nodiscard]] glm::vec3 GetCenter() const noexcept;
    [[nodiscard]] glm::vec3 GetSize() const noexcept;

private:
    std::vector<Mesh> meshes_;
    std::vector<Texture> texturesLoaded_;
    std::filesystem::path directory_;
    bool isLoaded_ = false;
    glm::vec3 minBounds_ { 0.0f };
    glm::vec3 maxBounds_ { 0.0f };

    void LoadModel(const std::filesystem::path& path);
    void ProcessNode(struct aiNode* node, const struct aiScene* scene);
    Mesh ProcessMesh(struct aiMesh* mesh, const struct aiScene* scene);
    std::vector<Texture> LoadMaterialTextures(struct aiMaterial* material, int type, const std::string& typeName);
    void ExpandBounds(const glm::vec3& position);
};
