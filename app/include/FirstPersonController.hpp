#pragma once

#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "modules/Colliders.hpp"

class FirstPersonController {
public:
  static constexpr float EYE_HEIGHT = 1.8f;

  struct State {
    glm::vec3 position;
    glm::vec3 forward;
  };

  template <typename SceneObjects>
  State update(float deltaT, const glm::vec3 &moveInput, double mouseX, double mouseY,
               bool cursorLocked, SceneObjects &scene) {
    updateLook(mouseX, mouseY, cursorLocked);
    updatePosition(deltaT, moveInput, scene);
    return {camPos, forward};
  }

  void resetMouseTracking() {
    firstFrame = true;
  }

private:
  static constexpr float MOVE_SPEED = 4.0f;
  static constexpr float MOUSE_SENS = 0.005f;
  static constexpr float PLAYER_RADIUS = 0.3f;

  glm::vec3 camPos = glm::vec3(-6.5f, EYE_HEIGHT, 0.0f);
  glm::vec3 forward = glm::vec3(1.0f, 0.0f, 0.0f);
  float yaw = glm::radians(90.0f);
  float pitch = 0.0f;
  double lastMouseX = 0.0;
  double lastMouseY = 0.0;
  bool firstFrame = true;

  void updateLook(double mouseX, double mouseY, bool cursorLocked) {
    if (cursorLocked && !firstFrame) {
      yaw += static_cast<float>(mouseX - lastMouseX) * MOUSE_SENS;
      pitch += static_cast<float>(mouseY - lastMouseY) * MOUSE_SENS;
    }

    firstFrame = false;
    lastMouseX = mouseX;
    lastMouseY = mouseY;

    pitch = glm::clamp(pitch, glm::radians(-89.0f), glm::radians(89.0f));
    forward = glm::normalize(glm::vec3(std::sin(yaw) * std::cos(pitch), -std::sin(pitch),
                                       -std::cos(yaw) * std::cos(pitch)));
  }

  template <typename SceneObjects>
  void updatePosition(float deltaT, const glm::vec3 &moveInput, SceneObjects &scene) {
    glm::vec3 walkDir = glm::normalize(glm::vec3(std::sin(yaw), 0.0f, -std::cos(yaw)));
    glm::vec3 right = glm::normalize(glm::cross(walkDir, glm::vec3(0, 1, 0)));
    glm::vec3 desiredMove = walkDir * MOVE_SPEED * deltaT * (-moveInput.z) +
                            right * MOVE_SPEED * deltaT * moveInput.x;

    glm::vec3 newPos = camPos;
    glm::vec3 fullPos(camPos.x + desiredMove.x, EYE_HEIGHT, camPos.z + desiredMove.z);

    if (!collides(fullPos, scene)) {
      newPos = fullPos;
    } else {
      glm::vec3 tryX(camPos.x + desiredMove.x, EYE_HEIGHT, camPos.z);
      if (!collides(tryX, scene)) {
        newPos.x = tryX.x;
      }

      glm::vec3 tryZ(newPos.x, EYE_HEIGHT, camPos.z + desiredMove.z);
      if (!collides(tryZ, scene)) {
        newPos.z = tryZ.z;
      }
    }

    newPos.y = EYE_HEIGHT;
    camPos = newPos;
  }

  template <typename SceneObjects>
  bool collides(glm::vec3 testPos, SceneObjects &scene) const {
    Collider playerCollider;
    playerCollider.initAABB(-PLAYER_RADIUS, 0.0f, -PLAYER_RADIUS, PLAYER_RADIUS, EYE_HEIGHT,
                            PLAYER_RADIUS);
    playerCollider.setWorldMatrix(
        glm::translate(glm::mat4(1), glm::vec3(testPos.x, 0.0f, testPos.z)));

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
