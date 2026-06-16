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

    // Night: heavily dim and tint the daytime panorama cool instead of swapping
    // assets — barely visible, just a faint cold glow so the sky reads as dark.
    vec3 nightSky = sky * vec3(0.012, 0.018, 0.045);
    vec3 col = mix(nightSky, sky, dayFactor);

    // Sun/moon body along the to-sun direction. Both the disc and its halo grow
    // with the body's height: small near the horizon, full size high in the sky.
    // This reads as a small rising/setting orb and, crucially, fades the body in
    // smoothly just above the horizon (bodyFade) so nothing snaps to full size as
    // it crosses the skyline — that pop was the residual "flash" at sunrise/sunset.
    float aligned   = max(dot(dir, toSun), 0.0);

    // 0.35 at the horizon -> 1.0 once well up; drives both disc size and halo.
    float bodyScale = mix(0.35, 1.0, smoothstep(0.0, 0.35, toSun.y));
    // Soft-edged disc (a gradient, not a hard rim); its inner edge tightens as the
    // body shrinks, so a low sun is a smaller orb.
    float discEdge  = mix(0.99955, 0.9988, bodyScale);
    float disc      = smoothstep(discEdge, 1.0, aligned);
    // Wider soft halo, scaled down near the horizon too.
    float halo      = smoothstep(0.985, 1.0, aligned) * 0.22 * bodyScale;
    // Gate the whole body just above the horizon so it eases in instead of popping.
    float bodyFade  = smoothstep(0.0, 0.05, toSun.y);

    vec3  discColor = mix(vec3(0.75, 0.82, 1.0), ubo.sunColor.rgb, dayFactor); // moon vs sun
    col += (disc + halo) * discColor * (0.4 + dayFactor) * bodyFade;

    // Match the main pass: it works in linear and gamma-encodes before output.
    col = pow(col, vec3(1.0 / 2.2));
    outColor = vec4(col, 1.0);
}
