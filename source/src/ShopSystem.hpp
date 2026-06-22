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

  // Item preview icons: the app loads the PNGs (Vulkan textures must outlive
  // ImGui draws) and hands back one ImTextureID per item.
  int itemCount() const { return (int)items.size(); }
  const std::string &iconFile(int index) const { return items[index].iconFile; }
  void setIcon(int index, ImTextureID icon) { items[index].icon = icon; }

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

    if (ImGui::BeginTable("wares", 7,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit |
                              ImGuiTableFlags_NoHostExtendX)) {
      ImGui::TableSetupColumn("##icon", ImGuiTableColumnFlags_WidthFixed, ICON_SIZE);
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
        ImGui::TableNextRow(ImGuiTableRowFlags_None, ICON_SIZE + 4.0F);
        ImGui::TableNextColumn();
        if (item.icon != (ImTextureID)0) {
          ImGui::Image(item.icon, ImVec2(ICON_SIZE, ICON_SIZE));
        }
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(item.name.c_str());
        ImGui::TableNextColumn();
        ImGui::Text("%d g", item.price);
        ImGui::TableNextColumn();
        ImGui::Text("%d", item.stock);
        ImGui::TableNextColumn();
        // Buy: disabled (greyed out) unless the merchant has stock AND the
        // player can afford it, so the transaction below can never go negative.
        ImGui::BeginDisabled(item.stock <= 0 || gold < item.price);
        if (ImGui::SmallButton("Buy")) {
          gold -= item.price;
          item.stock--;   // gold and stock move from merchant to player,
          item.owned++;   // owned moves the other way — totals stay conserved
        }
        ImGui::EndDisabled();
        ImGui::TableNextColumn();
        ImGui::Text("%d", item.owned);
        ImGui::TableNextColumn();
        // Sell: only when the player actually owns one. Pays half the buy price
        // (rounded down by integer division), the classic shopkeeper's margin.
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
    std::string iconFile;
    ImTextureID icon = (ImTextureID)0;
  };

  static constexpr float ICON_SIZE = 40.0F;

  bool open = false;
  bool closeRequest = false;
  // Starting economy. 60 gold is enough to sample a few wares but not buy the
  // lot, so the buy/sell loop stays meaningful. The {name, price, stock, owned,
  // icon} rows below are the merchant's opening inventory; "owned" seeds what
  // the player already has (e.g. one Roast Dinner) and can sell straight away.
  int gold = 60;
  std::vector<Item> items = {
      {"Mug of Ale", 4, 12, 0, "assets/textures/shop/ale.png"},
      {"Roast Dinner", 6, 8, 1, "assets/textures/shop/roast.png"},
      {"Torch", 8, 6, 0, "assets/textures/shop/torch.png"},
      {"Healing Draught", 20, 3, 0, "assets/textures/shop/potion.png"},
      {"Explorer's Journal", 35, 1, 0, "assets/textures/shop/map.png"},
  };
};
