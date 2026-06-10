#include "FirstPersonController.hpp"

#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

#include "SceneTypes.hpp"

FirstPersonController::State FirstPersonController::update(
    float deltaT, const glm::vec3 &moveInput, double mouseX, double mouseY, bool cursorLocked,
    bool jumpPressed, std::vector<SceneObject> &scene) {
  updateLook(mouseX, mouseY, cursorLocked);
  updateVerticalMotion(deltaT, jumpPressed);
  updatePosition(deltaT, moveInput, scene);
  return {camPos, forward};
}

void FirstPersonController::resetMouseTracking() {
  firstFrame = true;
}

void FirstPersonController::updateLook(double mouseX, double mouseY, bool cursorLocked) {
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

void FirstPersonController::updateVerticalMotion(float deltaT, bool jumpPressed) {
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

void FirstPersonController::updatePosition(float deltaT, const glm::vec3 &moveInput,
                                           std::vector<SceneObject> &scene) {
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

bool FirstPersonController::collides(glm::vec3 testPos, std::vector<SceneObject> &scene) const {
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
