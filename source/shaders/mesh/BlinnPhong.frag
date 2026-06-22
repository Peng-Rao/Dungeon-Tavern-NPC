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
#define MAX_LIGHTS        32   // must match MAX_LIGHTS on the C++ side
#define MAX_SHADOW_SPOTS  4    // must match MAX_SHADOW_SPOTS on the C++ side

struct Light {
    vec4 pos;    // xyz = world position,  w = type (LIGHT_*)
    vec4 dir;    // xyz = direction,        w = intensity
    vec4 color;  // rgb = color,            a = range (0 = infinite)
    vec4 cones;  // x = cos(innerAngle), y = cos(outerAngle) [spotlights];
                 // z = shadow-map slot for a spotlight (-1 = casts no shadow)
};

layout(binding = 0, set = 0) uniform GlobalUBO {
    vec4  eyePos;                          // xyz = eye position, w = active light count
    mat4  sunLightVP;                       // world -> sun clip space (sun shadow map)
    mat4  spotLightVP[MAX_SHADOW_SPOTS];    // world -> clip space, one per shadow-casting spot
    Light lights[MAX_LIGHTS];
} gubo;

// The sun's 2D shadow map: each texel is the depth of the nearest occluder, as
// seen from the sun through its orthographic frustum (E07-style directional map).
layout(binding = 1, set = 0) uniform sampler2D sunShadowMap;

// One 2D shadow map per shadow-casting spotlight (same idea as the sun map, but
// from a perspective frustum down each torch's cone).
layout(binding = 2, set = 0) uniform sampler2D spotShadowMaps[MAX_SHADOW_SPOTS];

// Directional (sun) shadow: project the world position into the sun's ortho
// clip space, map to [0,1] UVs and PCF-compare against the stored depth. A 3x3
// kernel softens the edges; the slope-scaled bias keeps grazing surfaces from
// shadowing themselves.
float sunShadowFactor(vec3 worldPos, vec3 N, vec3 L) {
    vec4 sp   = gubo.sunLightVP * vec4(worldPos, 1.0);
    vec3 proj = sp.xyz / sp.w;                  // ortho: w == 1, division is harmless
    vec2 uv   = proj.xy * 0.5 + 0.5;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || proj.z > 1.0) {
        return 1.0;                              // outside the sun's frustum: lit
    }
    float current = proj.z;
    float bias    = max(0.0015 * (1.0 - dot(N, L)), 0.0004);
    vec2  texel   = 1.0 / vec2(textureSize(sunShadowMap, 0));
    float lit = 0.0;
    for (int x = -1; x <= 1; x++) {
        for (int y = -1; y <= 1; y++) {
            float depth = texture(sunShadowMap, uv + vec2(x, y) * texel).r;
            lit += (current - bias <= depth) ? 1.0 : 0.0;
        }
    }
    return lit / 9.0;
}

// Spotlight shadow: identical idea to sunShadowFactor, but the light has a
// PERSPECTIVE frustum, so after transforming the world position into the spot's
// clip space we must do the perspective divide (xyz / w) before mapping to UVs.
// `slot` selects this spot's matrix and shadow map.
float spotShadowFactor(int slot, vec3 worldPos, vec3 N, vec3 L) {
    vec4 sp = gubo.spotLightVP[slot] * vec4(worldPos, 1.0);
    if (sp.w <= 0.0) {
        return 1.0;                              // behind the light: treat as lit
    }
    vec3 proj = sp.xyz / sp.w;                    // perspective divide
    vec2 uv   = proj.xy * 0.5 + 0.5;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || proj.z > 1.0 || proj.z < 0.0) {
        return 1.0;                              // outside the cone's frustum: lit
    }
    float current = proj.z;
    float bias    = max(0.0015 * (1.0 - dot(N, L)), 0.0004);
    vec2  texel   = 1.0 / vec2(textureSize(spotShadowMaps[slot], 0));
    float lit = 0.0;
    for (int x = -1; x <= 1; x++) {
        for (int y = -1; y <= 1; y++) {
            float depth = texture(spotShadowMaps[slot], uv + vec2(x, y) * texel).r;
            lit += (current - bias <= depth) ? 1.0 : 0.0;
        }
    }
    return lit / 9.0;
}

// matParams: x = specular exponent, yzw = emissive RGB color
layout(binding = 0, set = 1) uniform ObjectUBO {
    mat4 mvpMat;
    mat4 mMat;
    mat4 nMat;
    vec4 matParams;
} ubo;

layout(binding = 1, set = 1) uniform sampler2D albedoMap;

// Hemispheric ambient: instead of one flat fill colour everywhere (which makes
// everything look equally flat in shadow), we fake where bounced light comes
// from. Surfaces facing up catch a touch of cool "sky"/ceiling light; surfaces
// facing down sit in a darker warm "floor" tone, keeping a sense of volume.
const vec3 AMBIENT_SKY    = vec3(0.020, 0.022, 0.030); // up-facing, cool
const vec3 AMBIENT_GROUND = vec3(0.012, 0.007, 0.004); // down-facing, warm

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
        // Point and spot lights both radiate from a world position, so L points
        // from the fragment toward that position and the light falls off with
        // distance (computeAttenuation).
        vec3  toLight = light.pos.xyz - fragPos;
        float dist    = length(toLight);
        L             = toLight / dist;
        attenuation   = computeAttenuation(dist, range);

        // Spotlight cone (E08 model): dim from full brightness inside the inner
        // angle to dark past the outer angle. cosAngle is how aligned the
        // fragment is with the cone axis (light.dir.xyz); smoothstep gives the
        // soft inner->outer falloff. A point light skips this (stays a full sphere).
        if (type == LIGHT_SPOT) {
            float cosAngle   = dot(-L, normalize(light.dir.xyz));
            float spotFactor = smoothstep(light.cones.y, light.cones.x, cosAngle);
            attenuation *= spotFactor;
        }
    }

    float NdotL = max(dot(N, L), 0.0);
    vec3  H     = normalize(L + V);
    float NdotH = max(dot(N, H), 0.0);

    vec3 diffuse  = albedo * lightCol * NdotL;
    // Weight specular by NdotL (not just a step mask): at grazing angles NdotL->0,
    // which kills the razor-thin highlight that otherwise sweeps across the flat
    // ground as the sun crosses the horizon — the "flash" at sunrise/sunset. It
    // also keeps specular energy-consistent with the diffuse term.
    vec3 specular = lightCol * pow(NdotH, specExp) * NdotL;

    return (diffuse + specular) * attenuation * intensity;
}

void main() {
    vec3  albedo   = texture(albedoMap, fragUV).rgb;
    vec3  N        = normalize(fragNorm);
    vec3  V        = normalize(gubo.eyePos.xyz - fragPos);
    float specExp  = ubo.matParams.x;
    vec3  emissive = ubo.matParams.yzw;

    int numLights = int(gubo.eyePos.w);

    // Tie the ambient fill to the sun so the world falls dark at night: outdoors
    // (and any interior corner no torch reaches) should only be lit by daylight
    // or a nearby flame. The sun is the single LIGHT_DIRECTIONAL in the array and
    // is absent once it sets, so its intensity (dir.w, peaking at ~1.2 at noon)
    // drives the ambient. A small floor keeps shapes barely readable at night.
    float sunIntensity = 0.0;
    for (int i = 0; i < numLights; i++) {
        if (int(gubo.lights[i].pos.w) == LIGHT_DIRECTIONAL) {
            sunIntensity = max(sunIntensity, gubo.lights[i].dir.w);
        }
    }
    // Night floor: keep a meaningful slice of ambient after dark so torch-lit
    // rooms don't drop to pure black in every occluded corner — that crushed the
    // shadows into hard black edges. 0.18 lifts those blacks while the exterior
    // still reads as night.
    float ambientScale = max(0.15, clamp(sunIntensity / 1.2, 0.0, 1.0));

    // Ambient: pick between sky and ground tone by how much the normal points up.
    // (N.y * 0.5 + 0.5) maps straight-down(-1)->0 and straight-up(+1)->1.
    float skyMix  = N.y * 0.5 + 0.5;
    vec3  ambient = mix(AMBIENT_GROUND, AMBIENT_SKY, skyMix) * ambientScale;

    vec3 color = albedo * ambient + emissive;

    for (int i = 0; i < numLights; i++) {
        vec3 contrib = blinnPhong(gubo.lights[i], N, V, albedo, specExp);
        int  type    = int(gubo.lights[i].pos.w);
        if (type == LIGHT_DIRECTIONAL) {
            // The sun: clip it with the ortho shadow map so walls block it and
            // it only reaches the interior through windows and the doorway.
            // cones.z is a 0..1 shadow strength that fades in/out with the sun's
            // height (0 at/below the horizon, e.g. the moon). Lerping by it — and
            // letting the intensity fade alongside — avoids the hard pop you got
            // when the shadow was switched off as a boolean at sunset.
            float strength = clamp(gubo.lights[i].cones.z, 0.0, 1.0);
            if (strength > 0.0) {
                vec3 L = normalize(-gubo.lights[i].dir.xyz);
                contrib *= mix(1.0, sunShadowFactor(fragPos, N, L), strength);
            }
        } else if (type == LIGHT_SPOT) {
            // Spotlight: if it owns a shadow-map slot (cones.z >= 0), darken this
            // fragment where the torch's depth map says a closer surface blocks it.
            int slot = int(gubo.lights[i].cones.z);
            if (slot >= 0) {
                vec3 L = normalize(gubo.lights[i].pos.xyz - fragPos);
                contrib *= spotShadowFactor(slot, fragPos, N, L);
            }
        }
        color += contrib;
    }

    // Reinhard tonemapping + gamma correction
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));

    outColor = vec4(color, 1.0);
}
