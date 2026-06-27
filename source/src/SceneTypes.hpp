#pragma once

/**
 * @file SceneTypes.hpp
 * @brief Core data model shared across the app: light/uniform GPU structs and
 *        the per-object SceneObject (rendering, collision, flame, NPC and door
 *        state).
 */

#include <memory>
#include <string>
#include <vector>

// NOTE: Starter.hpp MUST come first — it owns the single GLM include and sets
// GLM_FORCE_DEPTH_ZERO_TO_ONE *before* pulling GLM in. We must NOT include
// <glm/glm.hpp> ourselves before this, or GLM would latch OpenGL's [-1,1] depth
// for the whole translation unit (which silently breaks glm::ortho/perspective).
// It must also come before Colliders.hpp, which is not self-contained and relies
// on the Vulkan/GLM symbols Starter.hpp pulls in.
#include "modules/Starter.hpp"
#include "modules/Colliders.hpp"

/// @name Light types — must match the #define values in BlinnPhong.frag
/// @{
constexpr int LIGHT_POINT       = 0; ///< Omnidirectional point light.
constexpr int LIGHT_SPOT        = 1; ///< Cone spotlight (wall torch).
constexpr int LIGHT_DIRECTIONAL = 2; ///< Parallel rays (the sun/moon).
constexpr int MAX_LIGHTS        = 32; ///< Fixed engine budget; each torch uses two
                                      ///< lights (a shadowing spotlight + a dim point "fill").
constexpr int MAX_SHADOW_SPOTS  = 4;  ///< How many spotlights can cast a 2D shadow map at once.
/// @}

/**
 * @brief One light as uploaded to the GPU (std140-aligned).
 *
 * The vec4s pack scalar parameters into spare channels to keep the struct
 * compact and the alignment simple.
 */
struct Light {
  alignas(16) glm::vec4 pos;    ///< xyz = world position, w = type (LIGHT_*).
  alignas(16) glm::vec4 dir;    ///< xyz = direction, w = intensity.
  alignas(16) glm::vec4 color;  ///< rgb = color, a = range (0 = infinite).
  alignas(16) glm::vec4 cones;  ///< x = cos(innerAngle), y = cos(outerAngle) [spotlights].
};

/** @brief Per-frame global uniforms shared by every object (set 0). */
struct GlobalUniformBufferObject {
  alignas(16) glm::vec4 eyePos;           ///< xyz = eye position, w = active light count.
  alignas(16) glm::mat4 sunLightVP;       ///< world->light clip for the sun's shadow map.
  alignas(16) glm::mat4 spotLightVP[MAX_SHADOW_SPOTS]; ///< one per shadow-casting spotlight.
  Light lights[MAX_LIGHTS];               ///< active lights this frame.
};

/** @brief Per-object uniforms (transforms + material), one per draw. */
struct UniformBufferObject {
  alignas(16) glm::mat4 mvpMat; ///< model-view-projection.
  alignas(16) glm::mat4 mMat;   ///< model/world matrix.
  alignas(16) glm::mat4 nMat;   ///< normal matrix, inverse-transpose of mMat.
  alignas(16) glm::vec4 matParams; ///< x = specular exponent, yzw = emissive RGB color.
};

/** @brief Vertex layout for the textured mesh pipeline. */
struct VertexSimple {
  glm::vec3 pos;  ///< object-space position.
  glm::vec3 norm; ///< object-space normal.
  glm::vec2 UV;   ///< texture coordinates.
};

/**
 * @brief One placed thing in the world: its mesh, transform, collider, and any
 *        flame/NPC/door state.
 *
 * Move-only (it owns collider child boxes via unique_ptr). All optional roles
 * (flame, patrol, door) are gated by the boolean flags below, so a plain prop
 * simply leaves them at their defaults.
 */
struct SceneObject {
  DescriptorSet DS;              ///< per-object descriptor set (uniforms + texture).
  Model *model = nullptr;        ///< mesh drawn for this object (non-owning, cached).
  Texture *texture = nullptr;    ///< base-colour texture (non-owning, shared/cached).
  glm::vec3 pos;                 ///< world position.
  float yaw;                     ///< world yaw in degrees (animated for doors/NPCs).
  float scale = 1.0f;            ///< uniform scale applied to the mesh.
  std::string tag;               ///< authoring tag ("wall", "prop", "npc", "door", ...).
  Collider collider;             ///< collision volume (OOBB or BVH).
  /// Owns the BVH child boxes (e.g. a passable doorway); `collider.children`
  /// holds raw pointers into these. Makes SceneObject move-only — the scene
  /// vector never copies, so that is fine.
  std::vector<std::unique_ptr<Collider>> colliderParts;
  bool collidable = false;       ///< does the player collide with this object?
  /// Skip this object in every pass while the first-person camera is active, and
  /// draw it only from the overhead camera. Used for the player's own body, which
  /// sits where the first-person camera is and would otherwise fill the screen.
  bool firstPersonHidden = false;
  /// World-space bounding sphere of the placed model, computed once after the
  /// scene loads; used to cull objects from per-light shadow passes.
  glm::vec3 boundsCenter{0.0f};  ///< centre of the world-space bounding sphere.
  float boundsRadius = 0.0f;     ///< radius of the world-space bounding sphere.
  float specExp = 32.0f;         ///< Blinn-Phong specular exponent (material shininess).
  glm::vec3 emissive{0.0f};      ///< self-illumination (glows regardless of lights).

  // ---- Flame state (only meaningful for candles/torches) ----
  /// @note Each flame's light lives *on the object that owns it* rather than in
  /// a global list, so "is this lit?" is a plain bool and toggling one flame
  /// never disturbs the others. Each frame the app rebuilds the GPU light array
  /// from whichever flames are lit.
  bool  isFlame = false;          ///< can this object emit light? (candle/torch).
  bool  isTorch = false;          ///< a torch flame specifically: can be carried (candles can't).
  bool  lit = false;              ///< is it currently burning?
  Light light{};                  ///< the light it emits while lit.
  float baseIntensity = 0.0f;     ///< resting brightness, before flicker.
  glm::vec3 baseEmissive{0.0f};   ///< resting glow, before flicker.
  float flamePhase = 0.0f;        ///< per-flame offset so they don't flicker in sync.

  /// Some flames ship a separate "lit" mesh (e.g. torch_lit shows a flame). When
  /// one exists both meshes stay loaded and the app draws whichever matches @ref
  /// lit; @ref model is always the unlit mesh, @ref litModel the lit one. Flames
  /// without a lit variant keep @ref hasLitVariant false and never swap mesh.
  Model *litModel = nullptr;      ///< lit-variant mesh, or nullptr if none.
  bool  hasLitVariant = false;    ///< true when a paired lit mesh exists.

  std::string npcId;              ///< dialogue tree key (tag "npc" only).

  /// Authored facing a stationary NPC eases back to when not engaged (patrolling
  /// NPCs face their walk direction instead).
  float restYaw = 0.0f;

  // ---- Patrol state (NPCs with a "patrol" path in the scene) ----
  /// @note The NPC walks waypoint to waypoint (cycling), pausing at each, and
  /// stops to face the player while spoken to. pos/yaw animate directly, so
  /// rendering and interaction follow for free.
  std::vector<glm::vec3> patrolPoints; ///< waypoints walked in order, cycling.
  float patrolSpeed = 1.0f;  ///< walk speed, m/s.
  float patrolPause = 1.5f;  ///< idle time at each waypoint, seconds.
  int patrolTarget = 0;      ///< waypoint currently walked toward.
  float patrolWait = 0.0f;   ///< remaining pause time at the current waypoint.

  // ---- Door state (only meaningful for tag "door") ----
  /// @note The mesh is authored with the hinge on its local origin, so swinging
  /// the door just animates @ref yaw between the two poses. @ref doorOpen is the
  /// target state; each frame yaw eases toward the matching pose.
  bool  isDoor = false;     ///< is this a hinged door?
  bool  doorOpen = false;   ///< target state: open (true) or closed (false).
  float openYaw = 0.0f;     ///< yaw (degrees) of the fully open pose.
  float closedYaw = 0.0f;   ///< yaw (degrees) of the fully closed pose.
};
