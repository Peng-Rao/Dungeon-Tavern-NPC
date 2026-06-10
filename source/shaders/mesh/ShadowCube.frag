#version 450
#extension GL_ARB_separate_shader_objects : enable

// ---- Shadow cube map pass: fragment stage ----
// Output is a single float: the straight-line distance from the light to this
// fragment, in world units. After all six faces are rendered, the six images
// form a cube map that answers "for a ray leaving the light in direction D,
// how far away is the nearest surface?". The main shader later compares that
// stored nearest distance against the real distance of the point being shaded:
// if the point is farther than the nearest occluder, it's in shadow.

layout(push_constant) uniform PushConstants {
    mat4 lightVP;
    vec4 lightPos;  // xyz = light world position
} pc;

layout(location = 0) in vec3 worldPos;

layout(location = 0) out float outDistance;

void main() {
    outDistance = length(worldPos - pc.lightPos.xyz);
}
