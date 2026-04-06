#include "TextureUtils.h"

#include <stdexcept>

#include <GL/glew.h>

#include "SOIL2/SOIL2.h"

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

    GLuint textureId = 0;
    glGenTextures(1, &textureId);
    glBindTexture(GL_TEXTURE_2D, textureId);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(format), width, height, 0, format, GL_UNSIGNED_BYTE, imageData);
    glGenerateMipmap(GL_TEXTURE_2D);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    SOIL_free_image_data(imageData);
    glBindTexture(GL_TEXTURE_2D, 0);
    return textureId;
}
