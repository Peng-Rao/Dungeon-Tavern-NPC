#pragma once

/**
 * @file InputBindings.hpp
 * @brief Single source of truth for keyboard controls and their HUD labels.
 */

#include <array>
#include <string>

#include <GLFW/glfw3.h>

/**
 * @brief Keyboard key codes and matching on-screen labels.
 *
 * Every system reads the key codes from here instead of hard-coding GLFW_KEY_*
 * at the call site, and the HUD prints the matching `*Label`, so the prompt the
 * player sees ("[E] Talk") can never drift from the key that triggers it.
 */
namespace input_bindings {

constexpr int MoveForward = GLFW_KEY_W;  ///< Walk forward.
constexpr int MoveBackward = GLFW_KEY_S; ///< Walk backward.
constexpr int MoveLeft = GLFW_KEY_A;     ///< Strafe left.
constexpr int MoveRight = GLFW_KEY_D;    ///< Strafe right.

constexpr int Jump = GLFW_KEY_SPACE;     ///< Jump.
constexpr int Interact = GLFW_KEY_E;     ///< Talk / open door / toggle flame.
constexpr int PickUpTorch = GLFW_KEY_J;  ///< Lift a wall torch into your hand (or put it back).
/// Switch between the first-person (perspective) camera and an overhead
/// orthographic (parallel-projection) camera that the mouse orbits.
constexpr int ToggleCamera = GLFW_KEY_C;
/// Overhead camera zoom is driven by the mouse wheel (a GLFW scroll callback),
/// not a key, so there is no key code here -- only the HUD label below.
constexpr int ToggleCursor = GLFW_KEY_TAB; ///< Release / capture the mouse cursor.
constexpr int LeaveDialogue = GLFW_KEY_ESCAPE; ///< End an open NPC conversation (not a quit key).

/// Dialogue choices, in display order ("[1] ...", "[2] ...", "[3] ...").
constexpr std::array<int, 3> DialogueChoices = {GLFW_KEY_1, GLFW_KEY_2, GLFW_KEY_3};

/// @name HUD labels for the keys above (kept beside the bindings on purpose).
/// @{
inline constexpr const char *MoveLabel = "WASD";
inline constexpr const char *InteractLabel = "E";
inline constexpr const char *PickUpTorchLabel = "J";
inline constexpr const char *ToggleCameraLabel = "C";
inline constexpr const char *ZoomLabel = "Scroll";
inline constexpr const char *ToggleCursorLabel = "Tab";
inline constexpr const char *LeaveDialogueLabel = "Esc";
/// @}

/**
 * @brief Builds the standard interaction prompt, e.g. "[E] Talk".
 * @param action Verb shown after the key, e.g. "Talk".
 * @return The formatted prompt string.
 */
inline std::string interactPrompt(const char *action) {
  return std::string("[") + InteractLabel + "] " + action;
}

/**
 * @brief Builds the torch-pickup prompt, e.g. "[J] Pick up".
 * @param action Verb shown after the key, e.g. "Pick up".
 * @return The formatted prompt string.
 */
inline std::string pickupPrompt(const char *action) {
  return std::string("[") + PickUpTorchLabel + "] " + action;
}

} // namespace input_bindings
