#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 fragPos;
layout(location = 1) in vec3 fragNorm;
layout(location = 2) in vec2 fragUV;
layout(location = 3) in vec4 fragTan;

layout(location = 0) out vec4 outColor;

layout(binding = 0, set = 0) uniform GlobalUniformBufferObject {
    vec3 lightDir;
    vec4 lightColor;
    vec3 eyePos;
} gubo;

const vec3 BASE_COLOR = vec3(0.65, 0.70, 0.85);
const float AMBIENT_STRENGTH = 0.18;

void main() {
    vec3 N = normalize(fragNorm);
    vec3 L = normalize(-gubo.lightDir);
    float NdotL = max(dot(N, L), 0.0);

    vec3 ambient = BASE_COLOR * AMBIENT_STRENGTH;
    vec3 diffuse = BASE_COLOR * gubo.lightColor.rgb * NdotL;

    vec3 color = ambient + diffuse;
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));

    outColor = vec4(color, 1.0);
}
