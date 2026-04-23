#version 330 core

in vec3 TexCoords;

out vec4 FragColor;

uniform sampler2D cloudTextureA;
uniform sampler2D cloudTextureB;
uniform vec3 horizonColor;
uniform vec3 zenithColor;
uniform vec3 groundColor;
uniform float time;

const float kPi = 3.14159265359;

vec2 SkyUv(vec3 direction, float scale, vec2 drift)
{
    vec3 normalizedDirection = normalize(direction);
    vec2 uv;
    uv.x = atan(normalizedDirection.z, normalizedDirection.x) / (2.0 * kPi) + 0.5;
    uv.y = clamp((normalizedDirection.y * 0.5) + 0.5, 0.0, 1.0);
    uv *= vec2(scale, scale * 0.7);
    uv += drift;
    return uv;
}

void main()
{
    vec3 direction = normalize(TexCoords);
    float verticalFactor = clamp((direction.y * 0.5) + 0.5, 0.0, 1.0);
    float skyBlend = smoothstep(0.10, 0.85, verticalFactor);
    vec3 color = mix(horizonColor, zenithColor, skyBlend);

    float groundBlend = smoothstep(-0.22, 0.05, direction.y);
    color = mix(groundColor, color, groundBlend);

    vec2 cloudUvA = SkyUv(direction, 1.35, vec2(time * 0.0025, 0.0));
    vec2 cloudUvB = SkyUv(direction, 2.10, vec2(-time * 0.0030, 0.08));
    vec4 cloudA = texture(cloudTextureA, cloudUvA);
    vec4 cloudB = texture(cloudTextureB, cloudUvB);

    float upperMask = smoothstep(0.04, 0.42, direction.y);
    float cloudMaskA = cloudA.a * upperMask * 0.50;
    float cloudMaskB = cloudB.a * upperMask * 0.32;
    vec3 cloudColorA = mix(vec3(1.0), cloudA.rgb, 0.18);
    vec3 cloudColorB = mix(vec3(1.0), cloudB.rgb, 0.12);

    color = mix(color, cloudColorA, cloudMaskA);
    color = mix(color, cloudColorB, cloudMaskB);
    FragColor = vec4(color, 1.0);
}
