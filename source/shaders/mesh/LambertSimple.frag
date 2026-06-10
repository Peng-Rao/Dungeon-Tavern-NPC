#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 fragPos;
layout(location = 1) in vec3 fragNorm;
layout(location = 2) in vec2 fragUV;

layout(location = 0) out vec4 outColor;

layout(binding = 0, set = 0) uniform GlobalUniformBufferObject {
    vec3 lightPos;
    vec4 lightColor;
    vec3 eyePos;
} gubo;

layout(binding = 1, set = 1) uniform sampler2D albedoMap;

const float AMBIENT_STRENGTH = 0.03;

void main() {
    vec3 albedo = texture(albedoMap, fragUV).rgb;

    vec3 N = normalize(fragNorm);
    vec3 lightVec = gubo.lightPos - fragPos;
    vec3 L = normalize(lightVec);
    float dist = length(lightVec);
    float attenuation = 1.0 / (1.0 + dist * dist);

    float NdotL = max(dot(N, L), 0.0);

    vec3 ambient = albedo * AMBIENT_STRENGTH;
    vec3 diffuse = albedo * gubo.lightColor.rgb * NdotL * attenuation;

    vec3 color = ambient + diffuse;
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));

    outColor = vec4(color, 1.0);
}
