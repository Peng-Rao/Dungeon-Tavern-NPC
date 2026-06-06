#pragma once

#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "modules/Colliders.hpp"

/**
 * @brief First-person camera controller with mouse look, WASD movement, jumping, and collisions.
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
   * @tparam SceneObjects Container type whose elements expose collidable and collider fields.
   * @param deltaT Frame time in seconds.
   * @param moveInput Input axes from getSixAxis; x is strafe, z is forward/back.
   * @param mouseX Current cursor x position.
   * @param mouseY Current cursor y position.
   * @param cursorLocked When false, mouse movement is ignored.
   * @param jumpPressed True while Space is held; jump starts only on the press edge.
   * @param scene Scene objects used for player collision checks.
   * @return Updated camera position and forward direction.
   */
  template <typename SceneObjects>
  State update(float deltaT, const glm::vec3 &moveInput, double mouseX, double mouseY,
               bool cursorLocked, bool jumpPressed, SceneObjects &scene) {
    updateLook(mouseX, mouseY, cursorLocked);
    updateVerticalMotion(deltaT, jumpPressed);
    updatePosition(deltaT, moveInput, scene);
    return {camPos, forward};
  }

  /**
   * @brief Resets mouse delta tracking after toggling cursor capture.
   */
  void resetMouseTracking() {
    firstFrame = true;
  }

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
  void updateLook(double mouseX, double mouseY, bool cursorLocked) {
    if (cursorLocked && !firstFrame) {
      yaw += static_cast<float>(mouseX - lastMouseX) * MOUSE_SENS;
      pitch += static_cast<float>(mouseY - lastMouseY) * MOUSE_SENS;
    }

    firstFrame = false;
    lastMouseX = mouseX;
    lastMouseY = mouseY;

    pitch = glm::clamp(pitch, glm::radians(-89.0F), glm::radians(89.0F));
    forward = glm::normalize(glm::vec3(std::sin(yaw) * std::cos(pitch), -std::sin(pitch),
                                       -std::cos(yaw) * std::cos(pitch)));
  }

  /**
   * @brief Applies jump impulse and gravity.
   *
   * @param deltaT Frame time in seconds.
   * @param jumpPressed True while Space is held; jump starts only on the press edge.
   */
  void updateVerticalMotion(float deltaT, bool jumpPressed) {
    const bool jumpStarted = jumpPressed && !jumpWasDown;
    jumpWasDown = jumpPressed;

    if (jumpStarted && grounded) {
      verticalVelocity = JUMP_SPEED;
      grounded = false;
    }

    if (!grounded) {
      verticalVelocity += GRAVITY * deltaT;
      camPos.y += verticalVelocity * deltaT;
      if (camPos.y <= EYE_HEIGHT) {
        camPos.y = EYE_HEIGHT;
        verticalVelocity = 0.0F;
        grounded = true;
      }
    }
  }

  /**
   * @brief Moves horizontally relative to yaw and resolves wall collisions with sliding.
   *
   * @tparam SceneObjects Container type whose elements expose collidable and collider fields.
   * @param deltaT Frame time in seconds.
   * @param moveInput Input axes from getSixAxis; x is strafe, z is forward/back.
   * @param scene Scene objects used for player collision checks.
   */
  template <typename SceneObjects>
  void updatePosition(float deltaT, const glm::vec3 &moveInput, SceneObjects &scene) {
    glm::vec3 walkDir = glm::normalize(glm::vec3(std::sin(yaw), 0.0F, -std::cos(yaw)));
    glm::vec3 right = glm::normalize(glm::cross(walkDir, glm::vec3(0.0F, 1.0F, 0.0F)));
    glm::vec3 desiredMove =
        walkDir * MOVE_SPEED * deltaT * (-moveInput.z) + right * MOVE_SPEED * deltaT * moveInput.x;

    glm::vec3 newPos = camPos;
    glm::vec3 fullPos(camPos.x + desiredMove.x, camPos.y, camPos.z + desiredMove.z);

    if (!collides(fullPos, scene)) {
      newPos = fullPos;
    } else {
      glm::vec3 tryX(camPos.x + desiredMove.x, camPos.y, camPos.z);
      if (!collides(tryX, scene)) {
        newPos.x = tryX.x;
      }

      glm::vec3 tryZ(newPos.x, camPos.y, camPos.z + desiredMove.z);
      if (!collides(tryZ, scene)) {
        newPos.z = tryZ.z;
      }
    }

    camPos = newPos;
  }

  /**
   * @brief Tests the player's AABB at a camera position against collidable scene objects.
   *
   * @tparam SceneObjects Container type whose elements expose collidable and collider fields.
   * @param testPos Candidate camera world position.
   * @param scene Scene objects used for collision checks.
   * @return True if the player collider overlaps any collidable scene object.
   */
  template <typename SceneObjects>
  bool collides(glm::vec3 testPos, SceneObjects &scene) const {
    Collider playerCollider;
    playerCollider.initAABB(-PLAYER_RADIUS, 0.0F, -PLAYER_RADIUS, PLAYER_RADIUS, EYE_HEIGHT,
                            PLAYER_RADIUS);
    playerCollider.setWorldMatrix(
        glm::translate(glm::mat4(1), glm::vec3(testPos.x, testPos.y - EYE_HEIGHT, testPos.z)));

    for (auto &obj : scene) {
      if (!obj.collidable) {
        continue;
      }
      if (playerCollider.collidesWith(obj.collider)) {
        return true;
      }
    }
    return false;
  }
};
