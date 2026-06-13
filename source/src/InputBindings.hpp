#pragma once

#include <array>
#include <string>

#include <GLFW/glfw3.h>

namespace input_bindings {

constexpr int MoveForward = GLFW_KEY_W;
constexpr int MoveBackward = GLFW_KEY_S;
constexpr int MoveLeft = GLFW_KEY_A;
constexpr int MoveRight = GLFW_KEY_D;

constexpr int Jump = GLFW_KEY_SPACE;
constexpr int Interact = GLFW_KEY_E;
constexpr int ToggleCursor = GLFW_KEY_TAB;
// Esc ends an open NPC conversation; it has no other role (not a quit key).
constexpr int LeaveDialogue = GLFW_KEY_ESCAPE;

// Dialogue choices, in display order ("[1] ...", "[2] ...", "[3] ...").
constexpr std::array<int, 3> DialogueChoices = {GLFW_KEY_1, GLFW_KEY_2, GLFW_KEY_3};

inline constexpr const char *MoveLabel = "WASD";
inline constexpr const char *InteractLabel = "E";
inline constexpr const char *ToggleCursorLabel = "Tab";
inline constexpr const char *LeaveDialogueLabel = "Esc";

inline std::string interactPrompt(const char *action) {
  return std::string("[") + InteractLabel + "] " + action;
}

} // namespace input_bindings
