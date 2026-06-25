#version 450
#extension GL_ARB_separate_shader_objects : enable

// Main-pass vertex shader for every textured mesh. It does no lighting itself
// it just hands the fragment stage (BlinnPhong.frag) the data lighting needs in
// world space: the world-space position and normal, plus the UV.
layout(binding = 0, set = 1) uniform UniformBufferObject {
	mat4 mvpMat;
	mat4 mMat;
	mat4 nMat;
	vec4 matParams;
} ubo;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNorm;
layout(location = 2) in vec2 inUV;

layout(location = 0) out vec3 fragPos;
layout(location = 1) out vec3 fragNorm;
layout(location = 2) out vec2 fragUV;


void main() {
	// Clip-space position for rasterisation (model-view-projection).
	gl_Position = ubo.mvpMat * vec4(inPosition, 1.0);
	// World-space position, so the fragment shader can compute per-pixel light
	// and view directions (lighting is done in world space).
	fragPos = (ubo.mMat * vec4(inPosition, 1.0)).xyz;
	// Normal transformed by nMat (the inverse-transpose of mMat), not mMat: that
	// keeps normals perpendicular to the surface under non-uniform scaling. w=0
	// drops the translation. Renormalised because the matrix can change length.
	fragNorm = normalize((ubo.nMat * vec4(inNorm, 0.0)).xyz);
	fragUV = inUV;
}
