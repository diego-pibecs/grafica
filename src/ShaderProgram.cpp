#include "ShaderProgram.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>

#include <glm/gtc/type_ptr.hpp>

namespace
{
std::string ReadTextFile(const std::filesystem::path& path)
{
    std::ifstream input(path);
    if (!input)
    {
        throw std::runtime_error("Failed to open shader file: " + path.string());
    }

    std::stringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

GLuint CompileShader(GLenum shaderType, const std::string& source, const std::filesystem::path& path)
{
    GLuint shaderId = glCreateShader(shaderType);
    const char* sourcePtr = source.c_str();
    glShaderSource(shaderId, 1, &sourcePtr, nullptr);
    glCompileShader(shaderId);

    GLint didCompile = GL_FALSE;
    glGetShaderiv(shaderId, GL_COMPILE_STATUS, &didCompile);
    if (didCompile == GL_TRUE)
    {
        return shaderId;
    }

    GLint logLength = 0;
    glGetShaderiv(shaderId, GL_INFO_LOG_LENGTH, &logLength);
    std::string log(static_cast<std::size_t>(std::max(logLength, 1)), '\0');
    glGetShaderInfoLog(shaderId, logLength, nullptr, log.data());
    glDeleteShader(shaderId);

    throw std::runtime_error("Failed to compile shader " + path.string() + ": " + log);
}
}

ShaderProgram::ShaderProgram(const std::filesystem::path& vertexPath, const std::filesystem::path& fragmentPath)
{
    const GLuint vertexShader = CompileShader(GL_VERTEX_SHADER, ReadTextFile(vertexPath), vertexPath);
    const GLuint fragmentShader = CompileShader(GL_FRAGMENT_SHADER, ReadTextFile(fragmentPath), fragmentPath);

    programId_ = glCreateProgram();
    glAttachShader(programId_, vertexShader);
    glAttachShader(programId_, fragmentShader);
    glLinkProgram(programId_);

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    GLint didLink = GL_FALSE;
    glGetProgramiv(programId_, GL_LINK_STATUS, &didLink);
    if (didLink == GL_TRUE)
    {
        return;
    }

    GLint logLength = 0;
    glGetProgramiv(programId_, GL_INFO_LOG_LENGTH, &logLength);
    std::string log(static_cast<std::size_t>(std::max(logLength, 1)), '\0');
    glGetProgramInfoLog(programId_, logLength, nullptr, log.data());
    glDeleteProgram(programId_);
    programId_ = 0;

    throw std::runtime_error("Failed to link shader program: " + log);
}

ShaderProgram::~ShaderProgram()
{
    if (programId_ != 0)
    {
        glDeleteProgram(programId_);
    }
}

ShaderProgram::ShaderProgram(ShaderProgram&& other) noexcept
    : programId_(std::exchange(other.programId_, 0))
{
}

ShaderProgram& ShaderProgram::operator=(ShaderProgram&& other) noexcept
{
    if (this == &other)
    {
        return *this;
    }

    if (programId_ != 0)
    {
        glDeleteProgram(programId_);
    }

    programId_ = std::exchange(other.programId_, 0);
    return *this;
}

void ShaderProgram::Use() const
{
    glUseProgram(programId_);
}

GLuint ShaderProgram::GetId() const noexcept
{
    return programId_;
}

void ShaderProgram::SetBool(const std::string& name, bool value) const
{
    glUniform1i(glGetUniformLocation(programId_, name.c_str()), value ? 1 : 0);
}

void ShaderProgram::SetInt(const std::string& name, int value) const
{
    glUniform1i(glGetUniformLocation(programId_, name.c_str()), value);
}

void ShaderProgram::SetFloat(const std::string& name, float value) const
{
    glUniform1f(glGetUniformLocation(programId_, name.c_str()), value);
}

void ShaderProgram::SetVec3(const std::string& name, const glm::vec3& value) const
{
    glUniform3fv(glGetUniformLocation(programId_, name.c_str()), 1, glm::value_ptr(value));
}

void ShaderProgram::SetMat4(const std::string& name, const glm::mat4& value) const
{
    glUniformMatrix4fv(glGetUniformLocation(programId_, name.c_str()), 1, GL_FALSE, glm::value_ptr(value));
}

void ShaderProgram::SetMat4Array(const std::string& name, const std::vector<glm::mat4>& values) const
{
    if (values.empty())
    {
        return;
    }
    glUniformMatrix4fv(
        glGetUniformLocation(programId_, name.c_str()),
        static_cast<GLsizei>(values.size()),
        GL_FALSE,
        glm::value_ptr(values.front()));
}
