#version 330 core

in vec3 FragPos;

uniform vec3 lightPosition;
uniform float farPlane;

void main()
{
    float lightDistance = length(FragPos - lightPosition);
    gl_FragDepth = lightDistance / farPlane;
}
