#version 450
#extension GL_ARB_separate_shader_objects : enable

// ============================================================
//  FRAGMENT SHADER — SKYBOX EXERCISE (E08)
// ============================================================

layout(location = 0) in vec3 fragDir;

layout(location = 0) out vec4 outColor;

layout(binding = 1, set = 0) uniform samplerCube envMap;


// ---- [TODO 2] Sample the environment cube map -------------
void main() {
    // replace with your implementation
    outColor = vec4(0.0, 0.0, 0.0, 1.0);
}
