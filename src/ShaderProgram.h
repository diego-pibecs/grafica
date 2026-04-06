#pragma once

#include <filesystem>
#include <string>

#include <GL/glew.h>
#include <glm/glm.hpp>

class ShaderProgram
{
public:
    ShaderProgram(const std::filesystem::path& vertexPath, const std::filesystem::path& fragmentPath);
    ~ShaderProgram();

    ShaderProgram(const ShaderProgram&) = delete;
    ShaderProgram& operator=(const ShaderProgram&) = delete;
    ShaderProgram(ShaderProgram&& other) noexcept;
    ShaderProgram& operator=(ShaderProgram&& other) noexcept;

    void Use() const;

    [[nodiscard]] GLuint GetId() const noexcept;
    void SetBool(const std::string& name, bool value) const;
    void SetInt(const std::string& name, int value) const;
    void SetFloat(const std::string& name, float value) const;
    void SetVec3(const std::string& name, const glm::vec3& value) const;
    void SetMat4(const std::string& name, const glm::mat4& value) const;

private:
    GLuint programId_ = 0;
};
