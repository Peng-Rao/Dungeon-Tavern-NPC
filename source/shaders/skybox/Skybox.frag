#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 fragDir;

layout(location = 0) out vec4 outColor;

layout(binding = 0, set = 0) uniform SkyboxUBO {
    mat4 mvpMat;
    vec4 sunDirDay;  // xyz = direction to the sun, w = day factor
    vec4 sunColor;   // rgb = sun/moon colour
} ubo;

layout(binding = 1, set = 0) uniform samplerCube envMap;

void main() {
    vec3 dir = normalize(fragDir);

    // The cube map is an SRGB texture, so the sampler returns linear colour.
    vec3 sky = texture(envMap, dir).rgb;

    float dayFactor = ubo.sunDirDay.w;
    vec3  toSun     = normalize(ubo.sunDirDay.xyz);

    // Night: dim and tint the daytime panorama cool instead of swapping assets.
    vec3 nightSky = sky * vec3(0.05, 0.07, 0.15);
    vec3 col = mix(nightSky, sky, dayFactor);

    // Sun/moon disc along the to-sun direction. Sharp core + soft halo.
    float aligned = max(dot(dir, toSun), 0.0);
    float disc = smoothstep(0.9988, 0.9994, aligned);
    float halo = smoothstep(0.985, 1.0, aligned) * 0.25;
    vec3  discColor = mix(vec3(0.75, 0.82, 1.0), ubo.sunColor.rgb, dayFactor); // moon vs sun
    col += (disc + halo) * discColor * (0.4 + dayFactor);

    // Match the main pass: it works in linear and gamma-encodes before output.
    col = pow(col, vec3(1.0 / 2.2));
    outColor = vec4(col, 1.0);
}
