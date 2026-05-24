#version 450
#extension GL_ARB_separate_shader_objects : enable

// ============================================================
//  FRAGMENT SHADER — IMAGE BASED LIGHTING EXERCISE (E08)
// ============================================================

// ------------------------------------------------------------
//  INPUT / OUTPUT
// ------------------------------------------------------------
layout(location = 0) in vec3 fragPos;
layout(location = 1) in vec3 fragNorm;
layout(location = 2) in vec2 fragUV;
layout(location = 3) in vec4 fragTan;

layout(location = 0) out vec4 outColor;


// ------------------------------------------------------------
//  TEXTURE MAPS — PBR MATERIAL (same as E07)
// ------------------------------------------------------------
layout(binding = 1, set = 1) uniform sampler2D albedoMap[4];
layout(binding = 2, set = 1) uniform sampler2D normalMap[4];
layout(binding = 3, set = 1) uniform sampler2D metallicMap[4];
layout(binding = 4, set = 1) uniform sampler2D roughnessMap[4];
layout(binding = 5, set = 1) uniform sampler2D aoMap[4];


// ------------------------------------------------------------
//  UNIFORMS
// ------------------------------------------------------------
layout(binding = 0, set = 0) uniform GlobalUniformBufferObject {

    // --- Directional light ---
    vec3 lightDir;
    vec4 lightColor;

    // --- Camera ---
    vec3 eyePos;

    // --- Debug ---
    vec4 debugView;     // z = texture index (0–3)

} gubo;

// --- IBL cube maps (global environment) ---
layout(binding = 1, set = 0) uniform samplerCube irrMap;   // irradiance (diffuse IBL)
layout(binding = 2, set = 0) uniform samplerCube envMap;   // environment (specular IBL via mip)


const float PI             = 3.14159265359;
const float MAX_REFLECT_LOD = 4.0;  

// ============================================================
//  SECTION 1 — PBR HELPERS 
// ============================================================

mat3 computeTBN(vec3 N, vec3 T, float tangentW) {
    vec3 n = normalize(N);
    vec3 t = normalize(T);
    t = normalize(t - dot(t, n) * n);
    vec3 b = normalize(cross(n, t)) * tangentW;
    return mat3(t, b, n);
}

vec3 getNormalFromMap(mat3 TBN, int ti) {
    vec3 tangentN = texture(normalMap[ti], fragUV).xyz * 2.0 - 1.0;
    return normalize(TBN * tangentN);
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a2    = roughness * roughness;
    float NdotH = max(dot(N, H), 0.0);
    float denom = NdotH * NdotH * (a2 - 1.0) + 1.0;
    denom = max(denom, 0.0001);
    return a2 / (PI * denom * denom);
}

float GeometrySchlickGGX(float NdotA, float roughness) {
    float k = (roughness + 1.0);
    k = (k * k) / 8.0;
    return NdotA / (NdotA * (1.0 - k) + k);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    return GeometrySchlickGGX(max(dot(N, V), 0.0), roughness) *
           GeometrySchlickGGX(max(dot(N, L), 0.0), roughness);
}

vec3 computeF0(vec3 albedo, float metallic) {
    return mix(vec3(0.04), albedo, metallic);
}


// ============================================================
//  SECTION 2 — IBL DIFFUSE
// ============================================================

// ---- [TODO 1] Compute the IBL diffuse ambient term --------
vec3 iblDiffuse(vec3 N, vec3 albedo, vec3 F, float metallic) {
    return vec3(0.0);   // replace
}


// ============================================================
//  SECTION 3 — IBL SPECULAR (mip-level approximation)
// ============================================================

// ---- Karis (Epic Games) analytical BRDF approximation (provided) ---
vec2 envBRDFApprox(float roughness, float NdotV) {
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572,  0.022);
    const vec4 c1 = vec4( 1.0,  0.0425,  1.04,  -0.04 );
    vec4  r = roughness * c0 + c1;
    float a = min(r.x * r.x, exp2(-9.28 * NdotV)) * r.x + r.y;
    return vec2(-1.04, 1.04) * a + r.zw;
}


// ---- [TODO 2] Compute the IBL specular ambient term -------

vec3 iblSpecular(vec3 N, vec3 V, float roughness, vec3 F, float NdotV) {
    return vec3(0.0);   // replace
}


// ============================================================
//  MAIN
// ============================================================
void main() {
    int ti = int(gubo.debugView.z);

    // ---- Sample PBR textures ----
    vec3  albedo    = pow(texture(albedoMap[ti], fragUV).rgb, vec3(2.2));
    float metallic  = texture(metallicMap[ti], fragUV).r;
    float roughness = texture(roughnessMap[ti], fragUV).r;
    float ao        = texture(aoMap[ti], fragUV).r;

    // ---- TBN + normal map (provided) -----------------------
    mat3 TBN = computeTBN(fragNorm, fragTan.xyz, fragTan.w);
    vec3 N   = getNormalFromMap(TBN, ti);

    // ---- Geometry setup (provided) -------------------------
    vec3 V = normalize(gubo.eyePos - fragPos);
    vec3 L = normalize(-gubo.lightDir);
    vec3 H = normalize(V + L);

    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);

    // ---- Direct lighting — Cook-Torrance (provided) --------
    vec3  F0 = computeF0(albedo, metallic);
    vec3  F_direct = fresnelSchlick(max(dot(V, H), 0.0), F0);
    float D  = DistributionGGX(N, H, roughness);
    float G  = GeometrySmith(N, V, L, roughness);

    vec3 f_diff = albedo * NdotL;
    vec3 f_spec = (D * G * F_direct) / max(4.0 * NdotV, 0.001);
    vec3 fr     = (vec3(1.0) - F_direct) * (1.0 - metallic) * f_diff + f_spec;
    vec3 direct = gubo.lightColor.rgb * fr;

    // ---- [TODO 3] IBL ambient + Ambient Occlusion ----------
    vec3 ambient = vec3(0.0);   // replace

    // ---- Tone mapping + gamma correction (provided) --------
    vec3 color = direct + ambient;
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));

    outColor = vec4(color, 1.0);
}
