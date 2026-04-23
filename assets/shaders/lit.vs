#version 330 core

layout (location = 0) in vec3 aPosition;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;

out vec3 FragPos;
out vec3 Normal;
out vec2 TexCoords;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform float depthOffset;

void main()
{
    vec4 worldPosition = model * vec4(aPosition, 1.0);
    vec3 worldNormal = normalize(mat3(transpose(inverse(model))) * aNormal);
    FragPos = worldPosition.xyz;
    Normal = worldNormal;
    TexCoords = aTexCoords;
    vec4 clipPosition = projection * view * worldPosition;
    clipPosition.z -= depthOffset * clipPosition.w;
    gl_Position = clipPosition;
}
