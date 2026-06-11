#pragma once

#include <string>

#include "InputBindings.hpp"
#include "imgui.h"

/**
 * @brief Handles simple NPC interaction state and renders the dialogue panel.
 *
 * Header-only: declaration and implementation live together; the only consumer
 * is DungeonApp.cpp.
 */
class DialogueSystem {
public:
  /**
   * @brief Advances dialogue state from the current interaction target and input edge.
   *
   * @param interactionTarget Tag of the object currently available for interaction.
   * @param interactPressed True only on the frame the interact key was pressed.
   */
  void update(const std::string &interactionTarget, bool interactPressed) {
    if (interactPressed) {
      if (open) {
        open = false;
      } else if (interactionTarget == "npc") {
        open = true;
      }
    }

    if (open && interactionTarget != "npc") {
      open = false;
    }
  }

  /**
   * @brief Returns the on-screen interaction prompt for the current target.
   *
   * @param interactionTarget Tag of the object currently available for interaction.
   * @return Prompt text shown near the crosshair.
   */
  std::string promptFor(const std::string &interactionTarget) const {
    return input_bindings::interactPrompt(interactionTarget == "npc" ? "Talk" : "Interact");
  }

  /**
   * @brief Draws the dialogue window when a conversation is open.
   */
  void draw() {
    if (!open) {
      return;
    }

    constexpr float windowWidth = 560.0F;
    constexpr float windowHeight = 0.0F;
    constexpr float windowPosX = 360.0F;
    constexpr float windowPosY = 500.0F;
    constexpr ImGuiWindowFlags windowFlags = ImGuiWindowFlags_AlwaysAutoResize;
    constexpr const char *windowName = "Tavern Keeper";
    constexpr const char *windowText = "Welcome";

    ImGui::SetNextWindowPos(ImVec2(windowPosX, windowPosY), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(windowWidth, windowHeight), ImGuiCond_FirstUseEver);
    ImGui::Begin(windowName, &open, windowFlags);
    ImGui::TextColored(ImVec4(1.0F, 0.82F, 0.45F, 1.0F), "Grum Barleyfist");
    ImGui::Separator();
    ImGui::TextWrapped(windowText);
    ImGui::Spacing();
    ImGui::TextDisabled("Press %s to close", input_bindings::InteractLabel);
    ImGui::End();
  }

private:
  /** @brief True while the NPC dialogue window is visible. */
  bool open = false;
};
