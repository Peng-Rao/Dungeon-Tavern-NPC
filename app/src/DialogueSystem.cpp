#include "DialogueSystem.hpp"

#include "imgui.h"

void DialogueSystem::update(const std::string &interactionTarget, bool interactPressed) {
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

const char *DialogueSystem::promptFor(const std::string &interactionTarget) const {
  return interactionTarget == "npc" ? "[E] Talk" : "[E] Interact";
}

void DialogueSystem::draw() {
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
  ImGui::TextDisabled("Press E to close");
  ImGui::End();
}
