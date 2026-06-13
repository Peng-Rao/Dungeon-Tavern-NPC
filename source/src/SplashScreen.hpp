#pragma once

#include "InputBindings.hpp"
#include "imgui.h"

/**
 * @brief Splash / start menu shown over the live scene: Start, Controls, Quit.
 *
 * Active on boot and whenever the player presses Esc in game. While active the
 * app leaves the cursor free and suspends player input; the tavern keeps
 * simmering behind the menu (flames flicker, the merchant patrols) as a
 * backdrop. The app consumes the start/quit requests raised by the buttons.
 *
 * Header-only: the only consumer is DungeonApp.cpp.
 */
class SplashScreen {
public:
  bool isActive() const { return active; }

  void setActive(bool on) {
    active = on;
    showControls = false;
  }

  /** @brief True exactly once after the player clicked Start. */
  bool consumeStartRequest() {
    bool requested = startRequest;
    startRequest = false;
    return requested;
  }

  /** @brief True exactly once after the player clicked Quit. */
  bool consumeQuitRequest() {
    bool requested = quitRequest;
    quitRequest = false;
    return requested;
  }

  /** @brief Draws the centered menu card with title and buttons. */
  void draw() {
    if (!active) {
      return;
    }
    const ImGuiIO &io = ImGui::GetIO();

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.07F, 0.04F, 0.02F, 0.93F));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.72F, 0.52F, 0.18F, 0.8F));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.28F, 0.15F, 0.06F, 1.0F));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.45F, 0.24F, 0.09F, 1.0F));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.58F, 0.32F, 0.11F, 1.0F));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(36.0F, 28.0F));

    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5F, io.DisplaySize.y * 0.45F),
                            ImGuiCond_Always, ImVec2(0.5F, 0.5F));
    ImGui::Begin("##splash", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);

    centeredTitle("DUNGEON TAVERN", 2.1F, ImVec4(0.91F, 0.72F, 0.29F, 1.0F));
    centeredTitle("~  N  P  C  ~", 1.2F, ImVec4(0.91F, 0.72F, 0.29F, 0.75F));
    ImGui::Spacing();
    centeredTitle("The Rusty Flagon awaits beyond the gate.", 1.0F,
                  ImVec4(0.96F, 0.91F, 0.76F, 0.85F));
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    const float buttonWidth = 240.0F;
    if (menuButton("Start", buttonWidth)) {
      startRequest = true;
    }
    if (menuButton(showControls ? "Controls  [-]" : "Controls  [+]", buttonWidth)) {
      showControls = !showControls;
    }
    if (showControls) {
      ImGui::Spacing();
      if (ImGui::BeginTable("controls", 2, ImGuiTableFlags_SizingFixedFit)) {
        controlRow(input_bindings::MoveLabel, "Move");
        controlRow("Mouse", "Look around");
        controlRow("Space", "Jump");
        controlRow(input_bindings::InteractLabel, "Interact: talk, doors, flames");
        controlRow("1-3", "Dialogue choices");
        controlRow(input_bindings::LeaveDialogueLabel, "Leave conversation");
        controlRow(input_bindings::ToggleCursorLabel, "Release / capture cursor");
        ImGui::EndTable();
      }
      ImGui::Spacing();
    }
    if (menuButton("Quit", buttonWidth)) {
      quitRequest = true;
    }

    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(5);
  }

private:
  static void centeredTitle(const char *text, float scale, ImVec4 color) {
    ImGui::SetWindowFontScale(scale);
    float width = ImGui::CalcTextSize(text).x;
    ImGui::SetCursorPosX((ImGui::GetWindowSize().x - width) * 0.5F);
    ImGui::TextColored(color, "%s", text);
    ImGui::SetWindowFontScale(1.0F);
  }

  static bool menuButton(const char *label, float width) {
    ImGui::SetCursorPosX((ImGui::GetWindowSize().x - width) * 0.5F);
    return ImGui::Button(label, ImVec2(width, 38.0F));
  }

  static void controlRow(const char *key, const char *what) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextColored(ImVec4(0.91F, 0.72F, 0.29F, 1.0F), "%s", key);
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(what);
  }

  bool active = true;
  bool showControls = false;
  bool startRequest = false;
  bool quitRequest = false;
};
