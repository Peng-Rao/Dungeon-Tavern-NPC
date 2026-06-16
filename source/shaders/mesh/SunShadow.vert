#version 450
#extension GL_ARB_separate_shader_objects : enable

// The sun's orthographic view-projection, pushed once per pass.
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
