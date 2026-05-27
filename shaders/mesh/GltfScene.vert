#version 450

layout(std140, set = 1, binding = 0) uniform LocalUniformBufferObject {
  mat4 mvpMat;
  mat4 mMat;
  mat4 nMat;
  vec4 baseColorFactor;
} localUbo;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inTangent;

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragUV;

void main() {
  vec4 worldPos = localUbo.mMat * vec4(inPosition, 1.0);
  gl_Position = localUbo.mvpMat * vec4(inPosition, 1.0);
  fragWorldPos = worldPos.xyz;
  fragNormal = normalize((localUbo.nMat * vec4(inNormal, 0.0)).xyz);
  fragUV = inUV;
}
