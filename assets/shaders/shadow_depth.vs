#version 330 core

layout (location = 0) in vec3 aPosition;
layout (location = 2) in vec2 aTexCoords;
layout (location = 3) in ivec4 aBoneIDs;
layout (location = 4) in vec4 aWeights;

out vec2 TexCoords;

uniform mat4 model;
uniform mat4 lightSpaceMatrix;
uniform bool useSkinning;

const int MAX_BONES = 100;
uniform mat4 finalBonesMatrices[MAX_BONES];

void main()
{
    TexCoords = aTexCoords;
    vec4 localPosition = vec4(aPosition, 1.0);
    if (useSkinning)
    {
        mat4 skinTransform = mat4(0.0);
        float totalWeight = 0.0;
        for (int index = 0; index < 4; ++index)
        {
            int boneId = aBoneIDs[index];
            float weight = aWeights[index];
            if (boneId >= 0 && boneId < MAX_BONES && weight > 0.0)
            {
                skinTransform += finalBonesMatrices[boneId] * weight;
                totalWeight += weight;
            }
        }
        if (totalWeight > 0.0)
        {
            localPosition = skinTransform * localPosition;
        }
    }
    gl_Position = lightSpaceMatrix * model * localPosition;
}
