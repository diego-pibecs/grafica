#include "Mesh.h"

#include <utility>

Mesh::Mesh(std::vector<Vertex> vertices, std::vector<unsigned int> indices, std::vector<Texture> textures)
    : vertices_(std::move(vertices))
    , indices_(std::move(indices))
    , textures_(std::move(textures))
{
    SetupMesh();
}

void Mesh::Draw() const
{
    DrawWithDepthOffset(-1, 0.0f);
}

void Mesh::DrawWithoutTextures() const
{
    DrawInternal(-1, 0.0f, false);
}

void Mesh::DrawWithDepthOffset(GLint depthOffsetLocation, float depthOffset) const
{
    DrawInternal(depthOffsetLocation, depthOffset, true);
}

void Mesh::DrawInternal(GLint depthOffsetLocation, float depthOffset, bool bindTextures) const
{
    if (bindTextures && !textures_.empty())
    {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, textures_.front().id);
    }

    if (depthOffsetLocation >= 0)
    {
        glUniform1f(depthOffsetLocation, depthOffset);
    }

    glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indices_.size()), GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);

    if (bindTextures && !textures_.empty())
    {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}

bool Mesh::HasTexture() const noexcept
{
    return !textures_.empty();
}

void Mesh::SetupMesh()
{
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ebo_);

    glBindVertexArray(vao_);

    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(vertices_.size() * sizeof(Vertex)),
        vertices_.data(),
        GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(
        GL_ELEMENT_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(indices_.size() * sizeof(unsigned int)),
        indices_.data(),
        GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, position)));

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, normal)));

    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, texCoords)));

    glBindVertexArray(0);
}
