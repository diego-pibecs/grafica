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

void main()
{
    vec3 albedo = useTexture ? texture(texture_diffuse1, TexCoords).rgb : baseColor;

    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(lightPos - FragPos);
    vec3 viewDir = normalize(viewPos - FragPos);
    vec3 reflectDir = reflect(-lightDir, norm);

    float diffuseFactor = max(dot(norm, lightDir), 0.0);
    float specularFactor = pow(max(dot(viewDir, reflectDir), 0.0), shininess);

    vec3 ambient = ambientColor * albedo;
    vec3 diffuse = diffuseFactor * lightColor * albedo;
    vec3 specular = specularFactor * 0.35 * lightColor;

    FragColor = vec4(ambient + diffuse + specular, 1.0);
}
