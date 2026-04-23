#pragma once

#include <cstddef>
#include <filesystem>
#include <vector>

#include <GL/glew.h>

struct aiTexture;

GLuint LoadTexture2D(const std::filesystem::path& path);
GLuint LoadTexture2DFromMemory(const unsigned char* data, std::size_t size);
GLuint LoadEmbeddedTexture2D(const aiTexture& texture);
GLuint CreateSolidTexture2D(unsigned char red, unsigned char green, unsigned char blue, unsigned char alpha);
GLuint CreateTexture2DFromRgbaPixels(int width, int height, const std::vector<unsigned char>& pixels);
GLuint LoadCubemap(const std::vector<std::filesystem::path>& facePaths);
