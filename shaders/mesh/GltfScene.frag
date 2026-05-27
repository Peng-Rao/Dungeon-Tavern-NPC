#version 450

layout(std140, set = 0, binding = 0) uniform GlobalUniformBufferObject {
  vec3 lightDir;
  vec4 lightColor;
  vec3 eyePos;
} globalUbo;

layout(std140, set = 1, binding = 0) uniform LocalUniformBufferObject {
  mat4 mvpMat;
  mat4 mMat;
  mat4 nMat;
  vec4 baseColorFactor;
} localUbo;

layout(set = 1, binding = 1) uniform sampler2D baseColorTexture;

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragUV;

layout(location = 0) out vec4 outColor;

void main() {
  vec4 texel = texture(baseColorTexture, fragUV);
  vec3 baseColor = texel.rgb * localUbo.baseColorFactor.rgb;

  vec3 normal = normalize(fragNormal);
  vec3 light = normalize(-globalUbo.lightDir);
  float diffuse = max(dot(normal, light), 0.0);

  vec3 viewDir = normalize(globalUbo.eyePos - fragWorldPos);
  vec3 halfDir = normalize(light + viewDir);
  float specular = pow(max(dot(normal, halfDir), 0.0), 32.0) * 0.12;

  vec3 ambient = baseColor * 0.34;
  vec3 direct = baseColor * globalUbo.lightColor.rgb * diffuse * 0.92;
  vec3 highlight = globalUbo.lightColor.rgb * specular;

  outColor = vec4(ambient + direct + highlight, 1.0);
}
