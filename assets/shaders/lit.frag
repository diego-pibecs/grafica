#version 330 core

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoords;

out vec4 FragColor;

uniform vec3 lightPos;
uniform vec3 viewPos;
uniform vec3 lightColor;
uniform vec3 ambientColor;
uniform vec3 baseColor;
uniform sampler2D texture_diffuse1;
uniform bool useTexture;
uniform float shininess;
uniform float specularStrength;

void main()
{
    vec4 sampled = useTexture ? texture(texture_diffuse1, TexCoords) : vec4(baseColor, 1.0);
    if (useTexture && sampled.a < 0.1)
    {
        discard;
    }

    vec3 albedo = sampled.rgb;

    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(lightPos - FragPos);
    vec3 viewDir = normalize(viewPos - FragPos);
    vec3 reflectDir = reflect(-lightDir, norm);

    float diffuseFactor = max(dot(norm, lightDir), 0.0);
    float specularFactor = pow(max(dot(viewDir, reflectDir), 0.0), shininess);

    vec3 ambient = ambientColor * albedo;
    vec3 diffuse = diffuseFactor * lightColor * albedo;
    vec3 specular = specularFactor * specularStrength * lightColor;

    FragColor = vec4(ambient + diffuse + specular, sampled.a);
}
