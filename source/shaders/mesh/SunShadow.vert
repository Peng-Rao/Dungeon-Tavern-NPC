#version 450
#extension GL_ARB_separate_shader_objects : enable

// Depth-only vertex shader, shared by BOTH shadow passes: the sun's directional
// map and every spotlight's map. The only thing that differs between them is the
// light's view-projection, so that is a push constant the host sets per pass
// (orthographic for the sun, perspective for a spot) — one shader, reused.
//
// world-space transform from set 0's mMat, then into light clip space; the
// rasteriser fills the depth buffer and the fragment shader writes nothing.
layout(push_constant) uniform SunPush {
    mat4 lightVP;
} pc;

// Object UBO (set 1 layout reused as set 0 here); only mMat is needed.
layout(binding = 0, set = 0) uniform UniformBufferObject {
    mat4 mvpMat;
    mat4 mMat;
    mat4 nMat;
    vec4 matParams;
} ubo;

layout(location = 0) in vec3 inPosition;

void main() {
    gl_Position = pc.lightVP * ubo.mMat * vec4(inPosition, 1.0);
}
