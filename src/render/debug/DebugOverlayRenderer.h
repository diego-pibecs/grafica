#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <GL/glew.h>
#include <glm/glm.hpp>

#include "ShaderProgram.h"

class DebugOverlayRenderer
{
public:
    enum class TextAlign
    {
        Left,
        Center,
        Right
    };

    struct OverlayVertex
    {
        glm::vec3 position { 0.0f };
        glm::vec3 color { 1.0f };
    };

    explicit DebugOverlayRenderer(const std::filesystem::path& assetsRoot);
    ~DebugOverlayRenderer();

    DebugOverlayRenderer(const DebugOverlayRenderer&) = delete;
    DebugOverlayRenderer& operator=(const DebugOverlayRenderer&) = delete;

    void Render(const std::vector<std::string>& lines, int framebufferWidth, int framebufferHeight) const;
    void RenderText(
        const std::vector<std::string>& lines,
        int framebufferWidth,
        int framebufferHeight,
        float x,
        float y,
        float pixelSize,
        const glm::vec3& textColor,
        float backgroundAlpha,
        TextAlign align) const;
    void RenderFullscreenTint(
        int framebufferWidth,
        int framebufferHeight,
        const glm::vec3& color,
        float alpha) const;

private:
    ShaderProgram shader_;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;

    void UploadVertices(const std::vector<OverlayVertex>& vertices) const;
};
