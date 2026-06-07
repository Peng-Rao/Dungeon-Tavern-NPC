#pragma once

#include <string>

/**
 * @brief Handles simple NPC interaction state and renders the dialogue panel.
 */
class DialogueSystem {
public:
  /**
   * @brief Advances dialogue state from the current interaction target and input edge.
   *
   * @param interactionTarget Tag of the object currently available for interaction.
   * @param interactPressed True only on the frame the interact key was pressed.
   */
  void update(const std::string &interactionTarget, bool interactPressed);

  /**
   * @brief Returns the on-screen interaction prompt for the current target.
   *
   * @param interactionTarget Tag of the object currently available for interaction.
   * @return Static prompt text shown near the crosshair.
   */
  const char *promptFor(const std::string &interactionTarget) const;

  /**
   * @brief Draws the dialogue window when a conversation is open.
   */
  void draw();

private:
  /** @brief True while the NPC dialogue window is visible. */
  bool open = false;
};
