#version 330 core

layout (location = 0) in vec3 aPosition;
layout (location = 2) in vec2 aTexCoords;

out vec2 TexCoords;

uniform mat4 model;
uniform mat4 lightSpaceMatrix;

void main()
{
    TexCoords = aTexCoords;
    gl_Position = lightSpaceMatrix * model * vec4(aPosition, 1.0);
}
