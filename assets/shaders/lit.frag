#version 330 core

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoords;

out vec4 FragColor;

uniform vec3 viewPos;
uniform vec3 sunDirection;
uniform vec3 sunColor;
uniform vec3 skyAmbientColor;
uniform vec3 groundAmbientColor;
uniform vec3 baseColor;
uniform sampler2D texture_diffuse1;
uniform sampler2D shadowMap;
uniform bool useTexture;
uniform bool shadowsEnabled;
uniform bool pointShadowsEnabled;
uniform float unlitFactor;
uniform float shininess;
uniform float specularStrength;
uniform mat4 lightSpaceMatrix;
uniform int pointLightCount;

struct PointLight
{
    vec3 position;
    vec3 color;
    float intensity;
    float range;
    float shadowStrength;
};

#define MAX_POINT_LIGHTS 12
uniform PointLight pointLights[MAX_POINT_LIGHTS];
uniform samplerCube pointShadowMaps[MAX_POINT_LIGHTS];

float ComputeShadow(vec3 normal, vec3 lightDir)
{
    vec4 fragPosLightSpace = lightSpaceMatrix * vec4(FragPos, 1.0);
    vec3 projected = fragPosLightSpace.xyz / max(fragPosLightSpace.w, 0.0001);
    projected = (projected * 0.5) + 0.5;

    if (projected.z > 1.0
        || projected.x < 0.0 || projected.x > 1.0
        || projected.y < 0.0 || projected.y > 1.0)
    {
        return 0.0;
    }

    float bias = max(0.0010 * (1.0 - dot(normal, lightDir)), 0.00016);
    vec2 texelSize = 1.0 / vec2(textureSize(shadowMap, 0));
    float shadow = 0.0;

    for (int x = -1; x <= 1; ++x)
    {
        for (int y = -1; y <= 1; ++y)
        {
            float sampleDepth = texture(shadowMap, projected.xy + (vec2(x, y) * texelSize * 1.15)).r;
            shadow += projected.z - bias > sampleDepth ? 1.0 : 0.0;
        }
    }

    shadow /= 9.0;
    return shadow * 0.52;
}

float ComputePointShadow(int lightIndex, PointLight light, vec3 normal, vec3 lightDir)
{
    if (!pointShadowsEnabled || light.shadowStrength <= 0.0)
    {
        return 0.0;
    }

    vec3 fragToLight = FragPos - light.position;
    float currentDepth = length(fragToLight);
    float closestDepth = texture(pointShadowMaps[lightIndex], fragToLight).r * light.range;
    float bias = max(0.045 * (1.0 - dot(normal, lightDir)), 0.025);
    return currentDepth - bias > closestDepth ? light.shadowStrength : 0.0;
}

vec3 EvaluatePointLight(int lightIndex, PointLight light, vec3 normal, vec3 viewDir, vec3 albedo)
{
    vec3 toLight = light.position - FragPos;
    float distanceToLight = length(toLight);
    if (distanceToLight <= 0.0001 || distanceToLight >= light.range)
    {
        return vec3(0.0);
    }

    vec3 lightDir = toLight / distanceToLight;
    float diffuseFactor = max(dot(normal, lightDir), 0.0);
    if (diffuseFactor <= 0.0)
    {
        return vec3(0.0);
    }

    float normalizedDistance = clamp(distanceToLight / light.range, 0.0, 1.0);
    float attenuation = pow(1.0 - normalizedDistance, 2.0) / (1.0 + (0.12 * distanceToLight) + (0.045 * distanceToLight * distanceToLight));
    vec3 halfVector = normalize(lightDir + viewDir);
    float specularFactor = pow(max(dot(normal, halfVector), 0.0), shininess) * specularStrength;
    float shadow = ComputePointShadow(lightIndex, light, normal, lightDir);
    vec3 diffuse = diffuseFactor * albedo * light.color;
    vec3 specular = specularFactor * light.color;
    return (diffuse + specular) * light.intensity * attenuation * (1.0 - shadow);
}

void main()
{
    vec4 sampled = useTexture ? texture(texture_diffuse1, TexCoords) : vec4(baseColor, 1.0);
    if (useTexture && sampled.a < 0.1)
    {
        discard;
    }

    vec3 albedo = sampled.rgb;

    vec3 norm = normalize(Normal);
    vec3 viewDir = normalize(viewPos - FragPos);
    vec3 directLightDir = normalize(-sunDirection);
    vec3 halfVector = normalize(directLightDir + viewDir);

    float rawDiffuseFactor = dot(norm, directLightDir);
    float diffuseFactor = max(rawDiffuseFactor, 0.0);
    float wrappedDiffuseFactor = clamp((rawDiffuseFactor + 0.42) / 1.42, 0.0, 1.0);
    diffuseFactor = max(diffuseFactor, wrappedDiffuseFactor * 0.45);
    diffuseFactor *= mix(1.0, 0.82, clamp(norm.y, 0.0, 1.0));
    float specularFactor = pow(max(dot(norm, halfVector), 0.0), shininess) * specularStrength;
    float ambientMix = clamp((norm.y * 0.5) + 0.5, 0.0, 1.0);
    vec3 ambient = (mix(groundAmbientColor, skyAmbientColor, ambientMix) + (skyAmbientColor * 0.10)) * albedo;
    float shadow = shadowsEnabled ? ComputeShadow(norm, directLightDir) : 0.0;
    vec3 directDiffuse = diffuseFactor * sunColor * albedo;
    vec3 directSpecular = specularFactor * sunColor;
    vec3 color = ambient + ((directDiffuse + directSpecular) * (1.0 - shadow));

    for (int index = 0; index < pointLightCount && index < MAX_POINT_LIGHTS; ++index)
    {
        color += EvaluatePointLight(index, pointLights[index], norm, viewDir, albedo);
    }

    vec3 evenlyLitColor = albedo * (vec3(0.86) + (sunColor * 0.18));
    evenlyLitColor *= 1.0 - (shadow * 0.65);
    color = mix(color, evenlyLitColor, clamp(unlitFactor, 0.0, 1.0));
    FragColor = vec4(clamp(color, 0.0, 1.0), sampled.a);
}
