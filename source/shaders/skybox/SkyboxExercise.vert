#version 450
#extension GL_ARB_separate_shader_objects : enable

// ============================================================
//  VERTEX SHADER — SKYBOX EXERCISE (E08)
// ============================================================

layout(binding = 0, set = 0) uniform SkyboxUBO {
    mat4 mvpMat;    // proj * rotationOnlyView * model  (no translation)
} ubo;

layout(location = 0) in vec3 inPosition;

layout(location = 0) out vec3 fragDir;


// ---- [TODO 1] Skybox vertex transform ---------------------
void main() {
    // replace with your implementation
    gl_Position = vec4(0.0);
    fragDir     = vec3(0.0);
}
