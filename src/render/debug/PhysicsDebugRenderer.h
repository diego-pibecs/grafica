#pragma once

#include <cstddef>
#include <filesystem>
#include <vector>

#include <GL/glew.h>
#include <glm/glm.hpp>

#include "ShaderProgram.h"
#include "physics/core/PhysicsTypes.h"

class PhysicsDebugRenderer
{
public:
    explicit PhysicsDebugRenderer(const std::filesystem::path& assetsRoot);
    ~PhysicsDebugRenderer();

    PhysicsDebugRenderer(const PhysicsDebugRenderer&) = delete;
    PhysicsDebugRenderer& operator=(const PhysicsDebugRenderer&) = delete;

    void Render(const PhysicsDebugFrame& frame, const glm::mat4& view, const glm::mat4& projection) const;

private:
    struct DebugVertex
    {
        glm::vec3 position { 0.0f };
        glm::vec3 color { 1.0f };
    };

    ShaderProgram shader_;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;

    void UploadVertices(const std::vector<DebugVertex>& vertices) const;
};
