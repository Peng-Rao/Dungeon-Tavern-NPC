#pragma once

#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

// NOTE: Starter.hpp MUST come before Colliders.hpp — the skeleton's Colliders.hpp
// is not self-contained and relies on Starter.hpp (and the Vulkan/GLM symbols it
// pulls in) being included first.
#include "modules/Starter.hpp"
#include "modules/Colliders.hpp"

// Light types — must match #define values in BlinnPhong.frag
constexpr int LIGHT_POINT       = 0;
constexpr int LIGHT_SPOT        = 1;
constexpr int LIGHT_DIRECTIONAL = 2;
constexpr int MAX_LIGHTS        = 12; // fixed engine budget, independent of scene content

struct Light {
  alignas(16) glm::vec4 pos;    // xyz = world position,  w = type (LIGHT_*)
  alignas(16) glm::vec4 dir;    // xyz = direction,        w = intensity
  alignas(16) glm::vec4 color;  // rgb = color,            a = range (0 = infinite)
  alignas(16) glm::vec4 cones;  // x = cos(inner), y = cos(outer); z = shadow cube
                                 // index (-1 = no shadow)
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
  glm::vec3 pos;
  float yaw;
  float scale = 1.0f;
  std::string tag;
  Collider collider;
  // When the collider is a BVH (e.g. a doorway whose opening must stay passable),
  // these own the child boxes; `collider.children` holds raw pointers into them.
  // SceneObject is move-only because of this — fine, the scene vector never copies.
  std::vector<std::unique_ptr<Collider>> colliderParts;
  bool collidable = false;
  // World-space bounding sphere of the placed model, computed once after the
  // scene loads; used to cull objects from per-light shadow passes.
  glm::vec3 boundsCenter{0.0f};
  float boundsRadius = 0.0f;
  float specExp = 32.0f;          // Blinn-Phong specular exponent (material shininess)
  glm::vec3 emissive{0.0f};       // self-illumination (glows regardless of lights)

  // ---- Flame state (only meaningful for candles/torches) ----
  // We keep each flame's light *with the object that owns it* instead of in a
  // separate global list. That way "is this candle lit?" is just a bool on the
  // object, and lighting/snuffing one at runtime never disturbs the others —
  // there are no shared indices to keep in sync. Each frame we rebuild the GPU
  // light array from whichever flames happen to be lit.
  bool  isFlame = false;          // can this object emit light? (candle/torch)
  bool  lit = false;              // is it currently burning?
  Light light{};                  // the light it emits while lit
  float baseIntensity = 0.0f;     // resting brightness, before flicker
  glm::vec3 baseEmissive{0.0f};   // resting glow, before flicker
  float flamePhase = 0.0f;        // per-flame offset so they don't flicker in sync

  // Some flames have a separate "lit" mesh (e.g. torch_lit shows a flame, torch
  // doesn't). When one exists we keep both meshes loaded and draw whichever
  // matches `lit`. `model` always holds the unlit mesh; `litModel` the lit one.
  // They share the same texture and uniforms, so swapping is just a matter of
  // binding a different vertex/index buffer at draw time. Flames without a lit
  // variant (e.g. candle_triple) keep hasLitVariant=false and never swap mesh.
  Model *litModel = nullptr;
  bool  hasLitVariant = false;

  // Which dialogue tree this NPC speaks with (tag "npc" only); key into
  // assets/dialogue/dialogues.json.
  std::string npcId;

  // Authored facing an NPC eases back to when the player is not engaging it.
  // (Patrolling NPCs face their walk direction instead; this is for the
  // stationary ones, who otherwise keep staring where the player last stood.)
  float restYaw = 0.0f;

  // ---- Patrol state (NPCs with a "patrol" path in the scene) ----
  // The NPC walks waypoint to waypoint (cycling), pausing at each; it stops
  // and faces the player while spoken to. pos/yaw are animated directly, so
  // rendering and interaction follow for free.
  std::vector<glm::vec3> patrolPoints;
  float patrolSpeed = 1.0f;  // walk speed, m/s
  float patrolPause = 1.5f;  // idle time at each waypoint, seconds
  int patrolTarget = 0;      // waypoint currently walked toward
  float patrolWait = 0.0f;   // remaining pause time

  // ---- Door state (only meaningful for tag "door") ----
  // The mesh is authored with the hinge on its local origin, so swinging the
  // door is just animating `yaw` between the two poses. `doorOpen` is the
  // target state; each frame yaw eases toward the matching pose.
  bool  isDoor = false;
  bool  doorOpen = false;
  float openYaw = 0.0f;     // yaw (degrees) of the fully open pose
  float closedYaw = 0.0f;   // yaw (degrees) of the fully closed pose

  int shadowCubeIndex = -1;       // index into shadowCubes if this flame casts shadows
};
