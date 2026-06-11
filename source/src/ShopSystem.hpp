#pragma once

#include <string>
#include <vector>

#include "InputBindings.hpp"
#include "imgui.h"

/**
 * @brief The merchant's buy/sell screen.
 *
 * Gold, merchant stock and the player's inventory live here as plain members,
 * so the state persists for the whole session: close the shop, wander off,
 * come back — your purchases and remaining gold are exactly as you left them.
 * Selling pays half the buy price, rounded down.
 *
 * The app opens/closes the shop (and manages cursor capture around it); the X
 * button and "Leave shop" raise a close request the app consumes.
 *
 * Header-only: the only consumer is DungeonApp.cpp.
 */
class ShopSystem {
public:
  void setOpen(bool on) { open = on; }
  bool isOpen() const { return open; }

  /** @brief True exactly once after the player asked to close from inside the window. */
  bool consumeCloseRequest() {
    bool requested = closeRequest;
    closeRequest = false;
    return requested;
  }

  /** @brief Draws the shop window: gold, wares table with Buy/Sell, leave button. */
  void draw() {
    if (!open) {
      return;
    }
    const ImGuiIO &io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5F, io.DisplaySize.y * 0.5F),
                            ImGuiCond_Appearing, ImVec2(0.5F, 0.5F));
    bool stillOpen = true;
    ImGui::Begin("Sable Vex — Wares", &stillOpen, ImGuiWindowFlags_AlwaysAutoResize);

    ImGui::TextColored(ImVec4(1.0F, 0.85F, 0.35F, 1.0F), "Your gold: %d", gold);
    ImGui::Separator();

    if (ImGui::BeginTable("wares", 6,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit |
                              ImGuiTableFlags_NoHostExtendX)) {
      ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_WidthFixed, 170.0F);
      ImGui::TableSetupColumn("Price");
      ImGui::TableSetupColumn("Stock");
      ImGui::TableSetupColumn("##buy");
      ImGui::TableSetupColumn("Owned");
      ImGui::TableSetupColumn("##sell");
      ImGui::TableHeadersRow();

      for (int i = 0; i < (int)items.size(); i++) {
        Item &item = items[i];
        ImGui::PushID(i);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(item.name.c_str());
        ImGui::TableNextColumn();
        ImGui::Text("%d g", item.price);
        ImGui::TableNextColumn();
        ImGui::Text("%d", item.stock);
        ImGui::TableNextColumn();
        ImGui::BeginDisabled(item.stock <= 0 || gold < item.price);
        if (ImGui::SmallButton("Buy")) {
          gold -= item.price;
          item.stock--;
          item.owned++;
        }
        ImGui::EndDisabled();
        ImGui::TableNextColumn();
        ImGui::Text("%d", item.owned);
        ImGui::TableNextColumn();
        ImGui::BeginDisabled(item.owned <= 0);
        if (ImGui::SmallButton("Sell")) {
          gold += item.price / 2;
          item.stock++;
          item.owned--;
        }
        ImGui::EndDisabled();
        ImGui::PopID();
      }
      ImGui::EndTable();
    }

    ImGui::TextDisabled("Selling pays half price. %s also leaves.", input_bindings::InteractLabel);
    if (ImGui::Button("Leave shop")) {
      closeRequest = true;
    }
    ImGui::End();

    if (!stillOpen) { // window's X button
      closeRequest = true;
    }
  }

private:
  struct Item {
    std::string name;
    int price;
    int stock; // merchant's
    int owned; // player's
  };

  bool open = false;
  bool closeRequest = false;
  int gold = 60;
  std::vector<Item> items = {
      {"Mug of Ale", 4, 12, 0},        {"Bread Loaf", 3, 8, 1},
      {"Torch", 8, 6, 0},              {"Healing Draught", 20, 3, 0},
      {"Dungeon Map Fragment", 35, 1, 0},
  };
};
