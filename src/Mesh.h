#pragma once

#include <string>
#include <vector>

#include <GL/glew.h>
#include <glm/glm.hpp>

struct Vertex
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoords;
    glm::ivec4 boneIDs { -1, -1, -1, -1 };
    glm::vec4 weights { 0.0f };
};

struct Texture
{
    GLuint id = 0;
    std::string type;
    std::string path;
};

class Mesh
{
public:
    Mesh(std::vector<Vertex> vertices, std::vector<unsigned int> indices, std::vector<Texture> textures = {});

    void Draw() const;
    void DrawWithoutTextures() const;
    void DrawWithDepthOffset(GLint depthOffsetLocation, float depthOffset) const;

    [[nodiscard]] bool HasTexture() const noexcept;

private:
    std::vector<Vertex> vertices_;
    std::vector<unsigned int> indices_;
    std::vector<Texture> textures_;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint ebo_ = 0;

    void DrawInternal(GLint depthOffsetLocation, float depthOffset, bool bindTextures) const;
    void SetupMesh();
};
