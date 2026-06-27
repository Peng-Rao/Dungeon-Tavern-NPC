#pragma once

/**
 * @file DialogueSystem.hpp
 * @brief Branching NPC dialogue loaded from assets/dialogue/dialogues.json.
 */

#include <array>
#include <fstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <json.hpp>

#include "InputBindings.hpp"
#include "imgui.h"

/**
 * @brief Branching NPC dialogue driven by assets/dialogue/dialogues.json.
 *
 * Each NPC (keyed by its scene "npcId") owns a tree of nodes; every node has
 * text and up to three choices mapped to keys 1/2/3. A choice either jumps to
 * another node, ends the conversation (empty "next"), or fires an action —
 * currently "shop", which the app consumes to open the merchant screen.
 *
 * Header-only: the only consumer is DungeonApp.cpp.
 */
class DialogueSystem {
public:
  /** @brief Loads every NPC's dialogue tree. Missing file = nobody talks. */
  void load(const std::string &path) {
    std::ifstream file(path);
    if (!file.is_open()) {
      return;
    }
    nlohmann::json dialogueJson;
    file >> dialogueJson;
    for (const auto &[npcId, treeJson] : dialogueJson.items()) {
      Tree tree;
      tree.name = treeJson.value("name", npcId);
      tree.start = treeJson.value("start", "root");
      for (const auto &[nodeId, nodeJson] : treeJson["nodes"].items()) {
        Node node;
        node.text = nodeJson.value("text", "");
        for (const auto &choiceJson : nodeJson.value("choices", nlohmann::json::array())) {
          node.choices.push_back({choiceJson.value("label", ""), choiceJson.value("next", ""),
                                  choiceJson.value("action", "")});
        }
        tree.nodes[nodeId] = std::move(node);
      }
      trees[npcId] = std::move(tree);
    }
  }

  /**
   * @brief Advances dialogue state by one frame.
   *
   * @param npcId The npcId currently aimed at ("" when none).
   * @param interactPressed True only on the frame the interact key was pressed.
   * @param choicePressed Press edges for the choice keys 1/2/3.
   */
  void update(const std::string &npcId, bool interactPressed,
              const std::array<bool, 3> &choicePressed) {
    if (interactPressed) {
      if (open) {
        close();
        return;
      }
      if (!npcId.empty() && trees.count(npcId) != 0) {
        open = true;
        activeNpc = npcId;
        nodeId = trees[npcId].start;
      }
    }
    if (!open) {
      return;
    }
    // Walking or looking away ends the conversation.
    if (npcId != activeNpc) {
      close();
      return;
    }
    const Node *node = currentNode();
    if (node == nullptr) {
      close();
      return;
    }
    for (int i = 0; i < (int)node->choices.size() && i < 3; i++) {
      if (!choicePressed[i]) {
        continue;
      }
      const Choice &choice = node->choices[i];
      if (choice.action == "shop") {
        shopRequest = true;
        close();
      } else if (choice.next.empty()) {
        close();
      } else {
        nodeId = choice.next;
      }
      break;
    }
  }

  /** @brief Crosshair prompt for an NPC in view. */
  std::string promptFor(const std::string & /*interactionTarget*/) const {
    return input_bindings::interactPrompt("Talk");
  }

  /** @brief Is a conversation currently on screen? */
  bool isOpen() const { return open; }

  /** @brief Ends an open conversation (e.g. the player pressed Esc). No-op when closed. */
  void leave() {
    if (open) {
      close();
    }
  }

  /** @brief npcId of the conversation partner ("" when no dialogue is open). */
  const std::string &activeNpcId() const { return activeNpc; }

  /** @brief True exactly once after a "shop" choice was picked. */
  bool consumeShopRequest() {
    bool requested = shopRequest;
    shopRequest = false;
    return requested;
  }

  /** @brief Draws the dialogue window with the current node and its choices. */
  void draw() {
    if (!open) {
      return;
    }
    const Tree &tree = trees.at(activeNpc);
    const Node *node = currentNode();
    if (node == nullptr) {
      return;
    }

    const ImGuiIO &io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5F, io.DisplaySize.y - 60.0F),
                            ImGuiCond_Always, ImVec2(0.5F, 1.0F));
    ImGui::SetNextWindowSize(ImVec2(620.0F, 0.0F), ImGuiCond_Always);
    ImGui::Begin("##dialogue", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoFocusOnAppearing);
    ImGui::TextColored(ImVec4(1.0F, 0.82F, 0.45F, 1.0F), "%s", tree.name.c_str());
    ImGui::Separator();
    ImGui::TextWrapped("%s", node->text.c_str());
    ImGui::Spacing();
    for (int i = 0; i < (int)node->choices.size() && i < 3; i++) {
      ImGui::Text("[%d] %s", i + 1, node->choices[i].label.c_str());
    }
    ImGui::Spacing();
    ImGui::TextDisabled("1-3: choose | %s: leave", input_bindings::LeaveDialogueLabel);
    ImGui::End();
  }

private:
  // The dialogue data model is a small directed graph per NPC, mirroring the
  // JSON 1:1 so loading is a plain copy with no extra bookkeeping.

  /** @brief One selectable option within a node. */
  struct Choice {
    std::string label;  ///< text shown to the player.
    std::string next;   ///< node to jump to (empty = end the conversation).
    std::string action; ///< optional app action (only "shop" today).
  };
  /** @brief A single line the NPC says plus the choices offered after it. */
  struct Node {
    std::string text;            ///< the NPC's line.
    std::vector<Choice> choices; ///< up to three options (keys 1/2/3).
  };
  /** @brief One NPC's whole conversation graph. */
  struct Tree {
    std::string name;  ///< display name shown in the dialogue window.
    std::string start; ///< id of the starting node.
    std::unordered_map<std::string, Node> nodes; ///< all nodes keyed by id.
  };

  /** @brief The node currently shown, or nullptr if none/invalid. */
  const Node *currentNode() const {
    auto tree = trees.find(activeNpc);
    if (tree == trees.end()) {
      return nullptr;
    }
    auto node = tree->second.nodes.find(nodeId);
    return node == tree->second.nodes.end() ? nullptr : &node->second;
  }

  /** @brief Clears all conversation state (back to closed). */
  void close() {
    open = false;
    activeNpc.clear();
    nodeId.clear();
  }

  std::unordered_map<std::string, Tree> trees; ///< every NPC's tree, keyed by npcId.
  std::string activeNpc;       ///< who we're talking to now ("" when closed).
  std::string nodeId;          ///< node currently shown within activeNpc's tree.
  bool open = false;           ///< is a conversation on screen?
  /// One-shot flag: set when a "shop" choice is picked, cleared by
  /// consumeShopRequest(). A flag (rather than a direct call) keeps this class
  /// free of any dependency on ShopSystem — the app polls it and decides.
  bool shopRequest = false;
};
