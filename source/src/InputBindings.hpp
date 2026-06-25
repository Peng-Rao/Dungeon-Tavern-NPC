#pragma once

#include <array>
#include <string>

#include <GLFW/glfw3.h>

// Single source of truth for keyboard controls. Every system reads the key
// codes from here instead of hard-coding GLFW_KEY_* at the call site, and the
// on-screen HUD prints the *Label below so the prompt the player sees ("[E]
// Talk") can never drift away from the key that actually triggers the action.
namespace input_bindings {

constexpr int MoveForward = GLFW_KEY_W;
constexpr int MoveBackward = GLFW_KEY_S;
constexpr int MoveLeft = GLFW_KEY_A;
constexpr int MoveRight = GLFW_KEY_D;

constexpr int Jump = GLFW_KEY_SPACE;
constexpr int Interact = GLFW_KEY_E;
// Lift a wall torch into your hand (or put a carried one back).
constexpr int PickUpTorch = GLFW_KEY_J;
constexpr int ToggleCursor = GLFW_KEY_TAB;
// Esc ends an open NPC conversation; it has no other role (not a quit key).
constexpr int LeaveDialogue = GLFW_KEY_ESCAPE;

// Dialogue choices, in display order ("[1] ...", "[2] ...", "[3] ...").
constexpr std::array<int, 3> DialogueChoices = {GLFW_KEY_1, GLFW_KEY_2, GLFW_KEY_3};

// Human-readable names for the keys above, shown in HUD prompts. Kept next to
// the bindings so changing a key is a reminder to change its label too.
inline constexpr const char *MoveLabel = "WASD";
inline constexpr const char *InteractLabel = "E";
inline constexpr const char *PickUpTorchLabel = "J";
inline constexpr const char *ToggleCursorLabel = "Tab";
inline constexpr const char *LeaveDialogueLabel = "Esc";

// Builds the standard interaction prompt, e.g. interactPrompt("Talk") -> "[E] Talk".
// One helper means every contextual prompt is formatted identically.
inline std::string interactPrompt(const char *action) {
  return std::string("[") + InteractLabel + "] " + action;
}

// Same idea for the torch pickup key, e.g. pickupPrompt("Pick up") -> "[J] Pick up".
inline std::string pickupPrompt(const char *action) {
  return std::string("[") + PickUpTorchLabel + "] " + action;
}

} // namespace input_bindings
