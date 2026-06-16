#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 0, set = 0) uniform SkyboxUBO {
    mat4 mvpMat;     // rotation-only view-projection (no camera translation)
    vec4 sunDirDay;  // xyz = direction to the sun, w = day factor
    vec4 sunColor;   // rgb = sun/moon colour
} ubo;

layout(location = 0) in vec3 inPosition;

layout(location = 0) out vec3 fragDir;

void main() {
    // The cube's local position doubles as the direction we sample the sky in.
    fragDir = inPosition;

    // Push the cube to the far plane (z = w -> NDC z = 1). With depth test
    // LESS_OR_EQUAL the sky survives only where nothing closer was drawn.
    vec4 pos = ubo.mvpMat * vec4(inPosition, 1.0);
    gl_Position = pos.xyww;
}
