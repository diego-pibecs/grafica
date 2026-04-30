#include "render/debug/DebugOverlayRenderer.h"

#include <array>
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <stdexcept>

#include <glm/gtc/matrix_transform.hpp>

namespace
{
using Glyph = std::array<unsigned char, 7>;

Glyph GlyphFor(char rawCharacter)
{
    const char character = static_cast<char>(std::toupper(static_cast<unsigned char>(rawCharacter)));
    switch (character)
    {
        case '0': return { 0b11111, 0b10001, 0b10011, 0b10101, 0b11001, 0b10001, 0b11111 };
        case '1': return { 0b00100, 0b01100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110 };
        case '2': return { 0b11110, 0b00001, 0b00001, 0b11110, 0b10000, 0b10000, 0b11111 };
        case '3': return { 0b11110, 0b00001, 0b00001, 0b01110, 0b00001, 0b00001, 0b11110 };
        case '4': return { 0b10010, 0b10010, 0b10010, 0b11111, 0b00010, 0b00010, 0b00010 };
        case '5': return { 0b11111, 0b10000, 0b10000, 0b11110, 0b00001, 0b00001, 0b11110 };
        case '6': return { 0b01111, 0b10000, 0b10000, 0b11110, 0b10001, 0b10001, 0b01110 };
        case '7': return { 0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b01000, 0b01000 };
        case '8': return { 0b01110, 0b10001, 0b10001, 0b01110, 0b10001, 0b10001, 0b01110 };
        case '9': return { 0b01110, 0b10001, 0b10001, 0b01111, 0b00001, 0b00001, 0b11110 };
        case 'A': return { 0b01110, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001 };
        case 'B': return { 0b11110, 0b10001, 0b10001, 0b11110, 0b10001, 0b10001, 0b11110 };
        case 'C': return { 0b01111, 0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b01111 };
        case 'D': return { 0b11110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b11110 };
        case 'E': return { 0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b11111 };
        case 'F': return { 0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b10000 };
        case 'G': return { 0b01111, 0b10000, 0b10000, 0b10011, 0b10001, 0b10001, 0b01111 };
        case 'H': return { 0b10001, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001 };
        case 'I': return { 0b11111, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b11111 };
        case 'L': return { 0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b11111 };
        case 'M': return { 0b10001, 0b11011, 0b10101, 0b10101, 0b10001, 0b10001, 0b10001 };
        case 'N': return { 0b10001, 0b11001, 0b10101, 0b10011, 0b10001, 0b10001, 0b10001 };
        case 'O': return { 0b01110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110 };
        case 'P': return { 0b11110, 0b10001, 0b10001, 0b11110, 0b10000, 0b10000, 0b10000 };
        case 'R': return { 0b11110, 0b10001, 0b10001, 0b11110, 0b10100, 0b10010, 0b10001 };
        case 'S': return { 0b01111, 0b10000, 0b10000, 0b01110, 0b00001, 0b00001, 0b11110 };
        case 'T': return { 0b11111, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100 };
        case 'U': return { 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110 };
        case 'V': return { 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01010, 0b00100 };
        case 'W': return { 0b10001, 0b10001, 0b10001, 0b10101, 0b10101, 0b10101, 0b01010 };
        case 'X': return { 0b10001, 0b10001, 0b01010, 0b00100, 0b01010, 0b10001, 0b10001 };
        case 'Y': return { 0b10001, 0b10001, 0b01010, 0b00100, 0b00100, 0b00100, 0b00100 };
        case 'Z': return { 0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b10000, 0b11111 };
        case ':': return { 0b00000, 0b00100, 0b00100, 0b00000, 0b00100, 0b00100, 0b00000 };
        case '.': return { 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b01100, 0b01100 };
        case ',': return { 0b00000, 0b00000, 0b00000, 0b00000, 0b00100, 0b00100, 0b01000 };
        case '-': return { 0b00000, 0b00000, 0b00000, 0b11110, 0b00000, 0b00000, 0b00000 };
        case '+': return { 0b00000, 0b00100, 0b00100, 0b11111, 0b00100, 0b00100, 0b00000 };
        case '/': return { 0b00001, 0b00001, 0b00010, 0b00100, 0b01000, 0b10000, 0b10000 };
        case ' ': return { 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000 };
        default: return { 0b11111, 0b00001, 0b00010, 0b00100, 0b00000, 0b00100, 0b00000 };
    }
}

void AddQuad(
    std::vector<DebugOverlayRenderer::OverlayVertex>& vertices,
    float x,
    float y,
    float width,
    float height,
    const glm::vec3& color)
{
    const glm::vec3 topLeft(x, y, 0.0f);
    const glm::vec3 topRight(x + width, y, 0.0f);
    const glm::vec3 bottomRight(x + width, y + height, 0.0f);
    const glm::vec3 bottomLeft(x, y + height, 0.0f);

    vertices.push_back({ topLeft, color });
    vertices.push_back({ bottomLeft, color });
    vertices.push_back({ bottomRight, color });
    vertices.push_back({ topLeft, color });
    vertices.push_back({ bottomRight, color });
    vertices.push_back({ topRight, color });
}

float TextWidth(const std::string& text, float pixelSize)
{
    if (text.empty())
    {
        return 0.0f;
    }
    return ((static_cast<float>(text.size()) - 1.0f) * 6.0f + 5.0f) * pixelSize;
}

void AddText(
    std::vector<DebugOverlayRenderer::OverlayVertex>& vertices,
    const std::string& text,
    float x,
    float y,
    float pixelSize,
    const glm::vec3& color)
{
    float cursorX = x;
    for (char character : text)
    {
        const Glyph glyph = GlyphFor(character);
        for (int row = 0; row < 7; ++row)
        {
            for (int column = 0; column < 5; ++column)
            {
                const unsigned char bit = static_cast<unsigned char>(1u << (4 - column));
                if ((glyph[static_cast<std::size_t>(row)] & bit) == 0)
                {
                    continue;
                }

                AddQuad(
                    vertices,
                    cursorX + (static_cast<float>(column) * pixelSize),
                    y + (static_cast<float>(row) * pixelSize),
                    pixelSize,
                    pixelSize,
                    color);
            }
        }
        cursorX += pixelSize * 6.0f;
    }
}
}

DebugOverlayRenderer::DebugOverlayRenderer(const std::filesystem::path& assetsRoot)
    : shader_(assetsRoot / "shaders" / "physics_debug.vs", assetsRoot / "shaders" / "physics_debug.frag")
{
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    if (vao_ == 0 || vbo_ == 0)
    {
        throw std::runtime_error("Failed to initialize debug overlay renderer buffers");
    }

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(OverlayVertex), reinterpret_cast<void*>(offsetof(OverlayVertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(OverlayVertex), reinterpret_cast<void*>(offsetof(OverlayVertex, color)));
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

DebugOverlayRenderer::~DebugOverlayRenderer()
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

void DebugOverlayRenderer::UploadVertices(const std::vector<OverlayVertex>& vertices) const
{
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(vertices.size() * sizeof(OverlayVertex)),
        vertices.data(),
        GL_DYNAMIC_DRAW);
}

void DebugOverlayRenderer::Render(const std::vector<std::string>& lines, int framebufferWidth, int framebufferHeight) const
{
    if (lines.empty() || framebufferWidth <= 0 || framebufferHeight <= 0)
    {
        return;
    }

    constexpr float kPixelSize = 3.0f;
    constexpr float kLineHeight = 28.0f;
    constexpr float kLeft = 18.0f;
    constexpr float kTop = 18.0f;
    constexpr float kPadding = 10.0f;
    const glm::vec3 textColor(0.86f, 1.0f, 0.76f);
    const glm::vec3 backgroundColor(0.01f, 0.015f, 0.02f);

    float maxLineWidth = 0.0f;
    for (const std::string& line : lines)
    {
        maxLineWidth = std::max(maxLineWidth, TextWidth(line, kPixelSize));
    }

    const float backgroundWidth = maxLineWidth + (kPadding * 2.0f);
    const float backgroundHeight = (static_cast<float>(lines.size()) * kLineHeight) + (kPadding * 2.0f) - 6.0f;

    shader_.Use();
    shader_.SetMat4("view", glm::mat4(1.0f));
    shader_.SetMat4(
        "projection",
        glm::ortho(0.0f, static_cast<float>(framebufferWidth), static_cast<float>(framebufferHeight), 0.0f));

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBindVertexArray(vao_);

    std::vector<OverlayVertex> backgroundVertices;
    backgroundVertices.reserve(6);
    AddQuad(backgroundVertices, kLeft - kPadding, kTop - kPadding, backgroundWidth, backgroundHeight, backgroundColor);
    UploadVertices(backgroundVertices);
    shader_.SetFloat("alpha", 0.62f);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(backgroundVertices.size()));

    std::vector<OverlayVertex> textVertices;
    for (std::size_t index = 0; index < lines.size(); ++index)
    {
        AddText(
            textVertices,
            lines[index],
            kLeft,
            kTop + (static_cast<float>(index) * kLineHeight),
            kPixelSize,
            textColor);
    }

    UploadVertices(textVertices);
    shader_.SetFloat("alpha", 1.0f);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(textVertices.size()));

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
}
