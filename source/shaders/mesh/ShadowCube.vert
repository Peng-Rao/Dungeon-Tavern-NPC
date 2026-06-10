#version 450
#extension GL_ARB_separate_shader_objects : enable

// ---- Shadow cube map pass: vertex stage ----
// We render the scene six times per shadow-casting light, once per cube face,
// from the light's point of view. This stage just transforms each vertex into
// the light's clip space (so the rasterizer fills that face) AND hands the
// fragment stage the vertex's WORLD position. We store world position rather
// than depth because the fragment stage writes the *linear distance* to the
// light, which is far easier to compare against later than raw NDC depth.

// We reuse the object's existing uniform block (set 0 here) only for its model
// matrix; the camera mvp/normal matrices in it are irrelevant to this pass.
layout(set = 0, binding = 0) uniform ObjectUBO {
    mat4 mvpMat;
    mat4 mMat;
    mat4 nMat;
    vec4 matParams;
} obj;

// The per-face light view-projection and the light position come as push
// constants: they change for every one of the six faces, and push constants are
// recorded straight into the command buffer per draw, so each face naturally
// gets its own values without juggling six separate uniform buffers.
layout(push_constant) uniform PushConstants {
    mat4 lightVP;   // light view * 90-degree perspective, for this face
    vec4 lightPos;  // xyz = light world position
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNorm; // unused, declared to match the vertex layout
layout(location = 2) in vec2 inUV;   // unused

layout(location = 0) out vec3 worldPos;

void main() {
    vec4 wp  = obj.mMat * vec4(inPosition, 1.0);
    worldPos = wp.xyz;
    gl_Position = pc.lightVP * wp;
}
