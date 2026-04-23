#include "TextureUtils.h"

#include <stdexcept>

#include <assimp/texture.h>
#include <GL/glew.h>

#include "SOIL2/SOIL2.h"

namespace
{
GLuint CreateTexture(GLenum internalFormat, GLenum dataFormat, int width, int height, const void* data)
{
    GLuint textureId = 0;
    glGenTextures(1, &textureId);
    glBindTexture(GL_TEXTURE_2D, textureId);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, dataFormat, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glBindTexture(GL_TEXTURE_2D, 0);
    return textureId;
}
}

GLuint LoadTexture2D(const std::filesystem::path& path)
{
    int width = 0;
    int height = 0;
    int channelCount = 0;
    unsigned char* imageData = SOIL_load_image(path.string().c_str(), &width, &height, &channelCount, SOIL_LOAD_AUTO);

    if (imageData == nullptr)
    {
        throw std::runtime_error("Failed to load texture: " + path.string());
    }

    GLenum format = GL_RGB;
    if (channelCount == 1)
    {
        format = GL_RED;
    }
    else if (channelCount == 2)
    {
        format = GL_RG;
    }
    else if (channelCount == 4)
    {
        format = GL_RGBA;
    }

    const GLuint textureId = CreateTexture(static_cast<GLint>(format), format, width, height, imageData);
    SOIL_free_image_data(imageData);
    return textureId;
}

GLuint LoadTexture2DFromMemory(const unsigned char* data, std::size_t size)
{
    int width = 0;
    int height = 0;
    int channelCount = 0;
    unsigned char* imageData = SOIL_load_image_from_memory(
        data,
        static_cast<int>(size),
        &width,
        &height,
        &channelCount,
        SOIL_LOAD_AUTO);

    if (imageData == nullptr)
    {
        throw std::runtime_error("Failed to load texture from embedded memory.");
    }

    GLenum format = GL_RGB;
    if (channelCount == 1)
    {
        format = GL_RED;
    }
    else if (channelCount == 2)
    {
        format = GL_RG;
    }
    else if (channelCount == 4)
    {
        format = GL_RGBA;
    }

    const GLuint textureId = CreateTexture(static_cast<GLint>(format), format, width, height, imageData);
    SOIL_free_image_data(imageData);
    return textureId;
}

GLuint LoadEmbeddedTexture2D(const aiTexture& texture)
{
    if (texture.mHeight == 0)
    {
        return LoadTexture2DFromMemory(
            reinterpret_cast<const unsigned char*>(texture.pcData),
            static_cast<std::size_t>(texture.mWidth));
    }

    return CreateTexture(
        GL_RGBA,
        GL_BGRA,
        static_cast<int>(texture.mWidth),
        static_cast<int>(texture.mHeight),
        texture.pcData);
}

GLuint CreateSolidTexture2D(unsigned char red, unsigned char green, unsigned char blue, unsigned char alpha)
{
    const unsigned char pixel[4] { red, green, blue, alpha };
    return CreateTexture(GL_RGBA, GL_RGBA, 1, 1, pixel);
}

GLuint CreateTexture2DFromRgbaPixels(int width, int height, const std::vector<unsigned char>& pixels)
{
    if (width <= 0 || height <= 0 || pixels.size() != static_cast<std::size_t>(width * height * 4))
    {
        throw std::runtime_error("Invalid RGBA texture pixel buffer.");
    }

    return CreateTexture(GL_RGBA, GL_RGBA, width, height, pixels.data());
}

GLuint LoadCubemap(const std::vector<std::filesystem::path>& facePaths)
{
    if (facePaths.size() != 6)
    {
        throw std::runtime_error("Cubemap requires exactly 6 face textures.");
    }

    GLuint textureId = 0;
    glGenTextures(1, &textureId);
    glBindTexture(GL_TEXTURE_CUBE_MAP, textureId);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    for (std::size_t index = 0; index < facePaths.size(); ++index)
    {
        int width = 0;
        int height = 0;
        int channelCount = 0;
        unsigned char* imageData = SOIL_load_image(
            facePaths[index].string().c_str(),
            &width,
            &height,
            &channelCount,
            SOIL_LOAD_AUTO);

        if (imageData == nullptr)
        {
            glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
            glDeleteTextures(1, &textureId);
            throw std::runtime_error("Failed to load cubemap face: " + facePaths[index].string());
        }

        GLenum format = GL_RGB;
        if (channelCount == 1)
        {
            format = GL_RED;
        }
        else if (channelCount == 2)
        {
            format = GL_RG;
        }
        else if (channelCount == 4)
        {
            format = GL_RGBA;
        }

        glTexImage2D(
            GL_TEXTURE_CUBE_MAP_POSITIVE_X + static_cast<GLenum>(index),
            0,
            static_cast<GLint>(format),
            width,
            height,
            0,
            format,
            GL_UNSIGNED_BYTE,
            imageData);

        SOIL_free_image_data(imageData);
    }

    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    return textureId;
}
