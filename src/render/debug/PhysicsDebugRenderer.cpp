#include "render/debug/PhysicsDebugRenderer.h"

#include <stdexcept>

PhysicsDebugRenderer::PhysicsDebugRenderer(const std::filesystem::path& assetsRoot)
    : shader_(assetsRoot / "shaders" / "physics_debug.vs", assetsRoot / "shaders" / "physics_debug.frag")
{
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    if (vao_ == 0 || vbo_ == 0)
    {
        throw std::runtime_error("Failed to initialize physics debug renderer buffers");
    }

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(DebugVertex), reinterpret_cast<void*>(offsetof(DebugVertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(DebugVertex), reinterpret_cast<void*>(offsetof(DebugVertex, color)));
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

PhysicsDebugRenderer::~PhysicsDebugRenderer()
{
    if (vbo_ != 0)
    {
        glDeleteBuffers(1, &vbo_);
    }
    if (vao_ != 0)
    {
        glDeleteVertexArrays(1, &vao_);
    }
}

void PhysicsDebugRenderer::UploadVertices(const std::vector<DebugVertex>& vertices) const
{
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(vertices.size() * sizeof(DebugVertex)),
        vertices.data(),
        GL_DYNAMIC_DRAW);
}

void PhysicsDebugRenderer::Render(const PhysicsDebugFrame& frame, const glm::mat4& view, const glm::mat4& projection) const
{
    shader_.Use();
    shader_.SetMat4("view", view);
    shader_.SetMat4("projection", projection);

    glBindVertexArray(vao_);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    if (!frame.triangles.empty())
    {
        std::vector<DebugVertex> vertices;
        vertices.reserve(frame.triangles.size() * 3u);
        for (const PhysicsDebugTriangle& triangle : frame.triangles)
        {
            vertices.push_back(DebugVertex { triangle.a, triangle.colorA });
            vertices.push_back(DebugVertex { triangle.b, triangle.colorB });
            vertices.push_back(DebugVertex { triangle.c, triangle.colorC });
        }

        UploadVertices(vertices);
        shader_.SetFloat("alpha", 0.12f);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
    }

    if (!frame.lines.empty())
    {
        std::vector<DebugVertex> vertices;
        vertices.reserve(frame.lines.size() * 2u);
        for (const PhysicsDebugLine& line : frame.lines)
        {
            vertices.push_back(DebugVertex { line.start, line.startColor });
            vertices.push_back(DebugVertex { line.end, line.endColor });
        }

        UploadVertices(vertices);
        shader_.SetFloat("alpha", 0.92f);
        glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(vertices.size()));
    }

    if (!frame.points.empty())
    {
        std::vector<DebugVertex> vertices;
        vertices.reserve(frame.points.size());
        for (const PhysicsDebugPoint& point : frame.points)
        {
            vertices.push_back(DebugVertex { point.position, point.color });
        }

        UploadVertices(vertices);
        glPointSize(7.0f);
        shader_.SetFloat("alpha", 1.0f);
        glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(vertices.size()));
    }

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}
