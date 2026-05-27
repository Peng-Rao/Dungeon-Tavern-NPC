#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 fragPos;
layout(location = 1) in vec3 fragNorm;
layout(location = 2) in vec2 fragUV;

layout(location = 0) out vec4 outColor;

// ---- Light types ----
#define LIGHT_POINT       0
#define LIGHT_SPOT        1
#define LIGHT_DIRECTIONAL 2
#define MAX_LIGHTS        8

struct Light {
    vec4 pos;    // xyz = world position,  w = type (LIGHT_*)
    vec4 dir;    // xyz = direction,        w = intensity
    vec4 color;  // rgb = color,            a = range (0 = infinite)
    vec4 cones;  // x  = cos(innerAngle),   y = cos(outerAngle)  [spot only]
};

layout(binding = 0, set = 0) uniform GlobalUBO {
    vec4  eyePos;              // xyz = eye position, w = active light count
    Light lights[MAX_LIGHTS];
} gubo;

// matParams: x = specular exponent, yzw = emissive RGB color
layout(binding = 0, set = 1) uniform ObjectUBO {
    mat4 mvpMat;
    mat4 mMat;
    mat4 nMat;
    vec4 matParams;
} ubo;

layout(binding = 1, set = 1) uniform sampler2D albedoMap;

const float AMBIENT = 0.04;

// Smooth attenuation: 1 at centre, 0 at range; falls off as 1/(1+d^2) when range==0
float computeAttenuation(float dist, float range) {
    if (range > 0.0) {
        float x = dist / range;
        return max(0.0, 1.0 - x * x);
    }
    return 1.0 / (1.0 + dist * dist);
}

vec3 blinnPhong(Light light, vec3 N, vec3 V, vec3 albedo, float specExp) {
    int   type      = int(light.pos.w);
    float intensity = light.dir.w;
    vec3  lightCol  = light.color.rgb;
    float range     = light.color.a;

    vec3  L;
    float attenuation;

    if (type == LIGHT_DIRECTIONAL) {
        L           = normalize(-light.dir.xyz);
        attenuation = 1.0;
    } else {
        vec3  toLight = light.pos.xyz - fragPos;
        float dist    = length(toLight);
        L             = toLight / dist;
        attenuation   = computeAttenuation(dist, range);

        if (type == LIGHT_SPOT) {
            float cosAngle  = dot(-L, normalize(light.dir.xyz));
            float spotFactor = smoothstep(light.cones.y, light.cones.x, cosAngle);
            attenuation *= spotFactor;
        }
    }

    float NdotL = max(dot(N, L), 0.0);
    vec3  H     = normalize(L + V);
    float NdotH = max(dot(N, H), 0.0);

    vec3 diffuse  = albedo * lightCol * NdotL;
    // Mask specular so it only fires on lit faces
    vec3 specular = lightCol * pow(NdotH, specExp) * step(0.001, NdotL);

    return (diffuse + specular) * attenuation * intensity;
}

void main() {
    vec3  albedo   = texture(albedoMap, fragUV).rgb;
    vec3  N        = normalize(fragNorm);
    vec3  V        = normalize(gubo.eyePos.xyz - fragPos);
    float specExp  = ubo.matParams.x;
    vec3  emissive = ubo.matParams.yzw;

    int numLights = int(gubo.eyePos.w);

    vec3 color = albedo * AMBIENT + emissive;

    for (int i = 0; i < numLights; i++) {
        color += blinnPhong(gubo.lights[i], N, V, albedo, specExp);
    }

    // Reinhard tonemapping + gamma correction
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));

    outColor = vec4(color, 1.0);
}
