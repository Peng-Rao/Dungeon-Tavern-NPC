#pragma once

#include <string>

#include <glm/glm.hpp>

#include "modules/Colliders.hpp"
#include "modules/Starter.hpp"

// Light types must match #define values in BlinnPhong.frag.
constexpr int LIGHT_POINT       = 0;
constexpr int LIGHT_SPOT        = 1;
constexpr int LIGHT_DIRECTIONAL = 2;
constexpr int MAX_LIGHTS        = 32; // fixed engine budget, independent of scene content

struct Light {
  alignas(16) glm::vec4 pos;    // xyz = world position,  w = type (LIGHT_*)
  alignas(16) glm::vec4 dir;    // xyz = direction,        w = intensity
  alignas(16) glm::vec4 color;  // rgb = color,            a = range (0 = infinite)
  alignas(16) glm::vec4 cones;  // x  = cos(innerAngle),   y = cos(outerAngle)
};

struct GlobalUniformBufferObject {
  alignas(16) glm::vec4 eyePos;           // xyz = eye position, w = active light count
  Light lights[MAX_LIGHTS];
};

struct UniformBufferObject {
  alignas(16) glm::mat4 mvpMat;
  alignas(16) glm::mat4 mMat;
  alignas(16) glm::mat4 nMat;
  // x = specular exponent, yzw = emissive RGB color
  alignas(16) glm::vec4 matParams;
};

struct VertexSimple {
  glm::vec3 pos;
  glm::vec3 norm;
  glm::vec2 UV;
};

struct SceneObject {
  DescriptorSet DS;
  Model *model = nullptr;
  Texture *texture = nullptr;
  std::string modelPath;
  glm::vec3 pos;
  float yaw;
  float scale = 1.0f;
  std::string tag;
  Collider collider;
  bool collidable = false;
  float specExp = 32.0f;          // Blinn-Phong specular exponent (material shininess)
  glm::vec3 emissive{0.0f};       // self-illumination (glows regardless of lights)
  bool togglableLight = false;
  bool lightActive = false;
  int lightIndex = -1;
  Light litLight{};
  glm::vec3 litEmissive{0.0f};
  glm::vec3 unlitEmissive{0.0f};
  std::string litModelPath;
  std::string unlitModelPath;
};
