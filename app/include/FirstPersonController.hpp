#pragma once

#include <vector>

#include <glm/glm.hpp>

struct SceneObject;

/**
 * @brief First-person camera controller with mouse look, movement input, jumping, and collisions.
 */
class FirstPersonController {
public:
  /** @brief Camera height above the floor, also used as the player collider height. */
  static constexpr float EYE_HEIGHT = 1.8F;

  /**
   * @brief Snapshot returned after each update.
   */
  struct State {
    glm::vec3 position; /**< Camera world position. */
    glm::vec3 forward;  /**< Normalized camera forward direction. */
  };

  /**
   * @brief Advances the controller by one frame.
   *
   * @param deltaT Frame time in seconds.
   * @param moveInput Input axes from getSixAxis; x is strafe, z is forward/back.
   * @param mouseX Current cursor x position.
   * @param mouseY Current cursor y position.
   * @param cursorLocked When false, mouse movement is ignored.
   * @param jumpPressed True while Space is held; jump starts only on the press edge.
   * @param scene Scene objects used for player collision checks.
   * @return Updated camera position and forward direction.
   */
  State update(float deltaT, const glm::vec3 &moveInput, double mouseX, double mouseY,
               bool cursorLocked, bool jumpPressed, std::vector<SceneObject> &scene);

  /**
   * @brief Resets mouse delta tracking after toggling cursor capture.
   */
  void resetMouseTracking();

private:
  static constexpr float MOVE_SPEED = 4.0F;
  static constexpr float MOUSE_SENS = 0.005F;
  static constexpr float PLAYER_RADIUS = 0.3F;
  static constexpr float JUMP_SPEED = 5.0F;
  static constexpr float GRAVITY = -12.0F;

  glm::vec3 camPos = glm::vec3(-6.5F, EYE_HEIGHT, 0.0F);
  glm::vec3 forward = glm::vec3(1.0F, 0.0F, 0.0F);
  float yaw = glm::radians(90.0F);
  float pitch = 0.0F;
  double lastMouseX = 0.0;
  double lastMouseY = 0.0;
  bool firstFrame = true;
  bool grounded = true;
  bool jumpWasDown = false;
  float verticalVelocity = 0.0F;

  /**
   * @brief Updates yaw/pitch from mouse movement and recalculates the forward direction.
   *
   * @param mouseX Current cursor x position.
   * @param mouseY Current cursor y position.
   * @param cursorLocked When false, mouse movement is ignored.
   */
  void updateLook(double mouseX, double mouseY, bool cursorLocked);

  /**
   * @brief Applies jump impulse and gravity.
   *
   * @param deltaT Frame time in seconds.
   * @param jumpPressed True while Space is held; jump starts only on the press edge.
   */
  void updateVerticalMotion(float deltaT, bool jumpPressed);

  /**
   * @brief Moves horizontally relative to yaw and resolves wall collisions with sliding.
   *
   * @param deltaT Frame time in seconds.
   * @param moveInput Input axes from getSixAxis; x is strafe, z is forward/back.
   * @param scene Scene objects used for player collision checks.
   */
  void updatePosition(float deltaT, const glm::vec3 &moveInput, std::vector<SceneObject> &scene);

  /**
   * @brief Tests the player's AABB at a camera position against collidable scene objects.
   *
   * @param testPos Candidate camera world position.
   * @param scene Scene objects used for collision checks.
   * @return True if the player collider overlaps any collidable scene object.
   */
  bool collides(glm::vec3 testPos, std::vector<SceneObject> &scene) const;
};
