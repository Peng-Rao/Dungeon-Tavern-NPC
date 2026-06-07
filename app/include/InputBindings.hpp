#pragma once

#include <string>

#include <GLFW/glfw3.h>

namespace input_bindings {

constexpr int MoveForward = GLFW_KEY_W;
constexpr int MoveBackward = GLFW_KEY_S;
constexpr int MoveLeft = GLFW_KEY_A;
constexpr int MoveRight = GLFW_KEY_D;

constexpr int Jump = GLFW_KEY_SPACE;
constexpr int Interact = GLFW_KEY_E;
constexpr int ToggleCursor = GLFW_KEY_F1;
constexpr int Quit = GLFW_KEY_ESCAPE;

inline constexpr const char *MoveLabel = "WASD";
inline constexpr const char *InteractLabel = "E";
inline constexpr const char *ToggleCursorLabel = "F1";

inline std::string interactPrompt(const char *action) {
  return std::string("[") + InteractLabel + "] " + action;
}

} // namespace input_bindings
