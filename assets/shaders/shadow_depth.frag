#version 330 core

in vec2 TexCoords;

uniform sampler2D texture_diffuse1;
uniform bool useTexture;

void main()
{
    if (useTexture)
    {
        vec4 sampled = texture(texture_diffuse1, TexCoords);
        if (sampled.a < 0.1)
        {
            discard;
        }
    }
}
