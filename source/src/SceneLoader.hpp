#pragma once

#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <json.hpp>

namespace scene_loader {

inline bool isCollidableTag(const std::string &tag) {
  return tag == "wall" || tag == "structure" || tag == "furniture" || tag == "prop";
}

// Loads scene.json into `scene`. Templated so it stays independent of the
// concrete SceneObject/Light types (defined in DungeonApp.cpp); it is only
// instantiated from localInit, where those types are complete. Models and
// textures are resolved through callbacks that hit the app's cache.
//
// Flames (candles/torches) are handled here too: a flame may ship as a paired
// lit/unlit model, so both meshes are resolved, and a point light is set up *on
// the object itself* (no global light list) so toggling one never disturbs the
// others. Each frame the app rebuilds the GPU light array from the lit flames.
template <typename SceneObjects, typename ModelResolver, typename TextureResolver>
void loadSceneFromJson(const std::string &scenePath, SceneObjects &scene,
                       ModelResolver resolveModel, TextureResolver resolveTexture,
                       int lightPointType) {
  nlohmann::json sceneJson;
  {
    std::ifstream file(scenePath);
    if (!file.is_open()) {
      throw std::runtime_error("Cannot open " + scenePath);
    }
    file >> sceneJson;
  }

  const auto &objects = sceneJson["objects"];
  scene.resize(objects.size());
  for (std::size_t i = 0; i < objects.size(); i++) {
    const auto &jsonObject = objects[i];
    const auto &position = jsonObject["pos"];
    const std::string modelPath = jsonObject["model"].get<std::string>();
    auto &sceneObject = scene[i];

    sceneObject.pos =
        glm::vec3(position[0].get<float>(), position[1].get<float>(), position[2].get<float>());
    sceneObject.yaw = jsonObject.value("yaw", 0.0f);
    sceneObject.restYaw = sceneObject.yaw; // facing to return to when idle
    sceneObject.scale = jsonObject.value("scale", 1.0f);
    sceneObject.tag = jsonObject.value("tag", "");
    sceneObject.npcId = jsonObject.value("npcId", "");
    if (jsonObject.contains("patrol")) {
      for (const auto &waypoint : jsonObject["patrol"]) {
        sceneObject.patrolPoints.emplace_back(waypoint[0].get<float>(), waypoint[1].get<float>(),
                                              waypoint[2].get<float>());
      }
      sceneObject.patrolSpeed = jsonObject.value("patrolSpeed", 1.0f);
      sceneObject.patrolPause = jsonObject.value("patrolPause", 1.5f);
    }
    sceneObject.specExp = jsonObject.value("specExp", 32.0f);
    if (jsonObject.contains("emissive")) {
      const auto &emissive = jsonObject["emissive"];
      sceneObject.emissive =
          glm::vec3(emissive[0].get<float>(), emissive[1].get<float>(),
                    emissive[2].get<float>());
    }

    // Decide up front whether this is a flame (candle/torch): that changes how we
    // load its mesh, since a flame may come as a paired lit/unlit model and we
    // want both loaded so we can swap them when it's toggled. meshPath tracks
    // which path's cached texture belongs to sceneObject.model.
    const std::string &tag = sceneObject.tag;
    const bool isTorch  = modelPath.find("torch")  != std::string::npos;
    const bool isCandle = modelPath.find("candle") != std::string::npos;
    const bool isFlame  = (tag == "light_source") && (isTorch || isCandle);

    std::string meshPath = modelPath;
    if (isFlame) {
      // unlit = name with "_lit" stripped; lit = "_lit" inserted before .gltf.
      std::string unlitPath = modelPath;
      const std::size_t litPos = unlitPath.find("_lit");
      if (litPos != std::string::npos) unlitPath.erase(litPos, 4);
      std::string litPath = unlitPath;
      const std::size_t ext = litPath.rfind(".gltf");
      if (ext != std::string::npos) litPath.insert(ext, "_lit");

      // Only swap meshes if the pair actually exists on disk (candle_triple, for
      // instance, has no _lit version) — otherwise just keep the one mesh.
      sceneObject.hasLitVariant =
          std::filesystem::exists(litPath) && std::filesystem::exists(unlitPath);
      if (sceneObject.hasLitVariant) {
        sceneObject.model = resolveModel(unlitPath);
        sceneObject.litModel = resolveModel(litPath);
        meshPath = unlitPath;
      } else {
        sceneObject.model = resolveModel(modelPath);
      }
    } else {
      sceneObject.model = resolveModel(modelPath);
    }

    // Embedded base colour (NPC / KayKit characters) is cached under meshPath;
    // dungeon props have none -> nullptr -> the shared Tdungeon is used later.
    sceneObject.texture = resolveTexture(meshPath);

    // Hinged door: yaw animates between the open/closed poses at runtime; the
    // scene's "yaw" is the starting pose, and the logical state follows
    // whichever pose it starts closer to.
    sceneObject.isDoor = (tag == "door");
    if (sceneObject.isDoor) {
      sceneObject.openYaw = jsonObject.value("openYaw", sceneObject.yaw);
      sceneObject.closedYaw = jsonObject.value("closedYaw", sceneObject.yaw);
      sceneObject.doorOpen = std::abs(sceneObject.yaw - sceneObject.openYaw) <
                             std::abs(sceneObject.yaw - sceneObject.closedYaw);
    }

    // Doors collide too (a closed gate must block the way); the app refreshes
    // their collider world matrix whenever the leaf comes to rest.
    sceneObject.collidable = isCollidableTag(tag) || sceneObject.isDoor;
    if (sceneObject.collidable) {
      if (jsonObject.contains("colliderBoxes")) {
        // Explicit collider boxes (model local space, [minX,minY,minZ,maxX,maxY,maxZ]
        // per box) instead of one box around the whole mesh — used for pieces with
        // walkable openings, like the doorway to the corridor.
        using ColliderT = std::remove_reference_t<decltype(sceneObject.collider)>;
        std::vector<ColliderT *> parts;
        for (const auto &box : jsonObject["colliderBoxes"]) {
          auto part = std::make_unique<ColliderT>();
          part->initOOBB(box[0].get<float>(), box[1].get<float>(), box[2].get<float>(),
                         box[3].get<float>(), box[4].get<float>(), box[5].get<float>());
          parts.push_back(part.get());
          sceneObject.colliderParts.push_back(std::move(part));
        }
        sceneObject.collider.initBVH(parts);
      } else {
        sceneObject.collider.fitOOBB(sceneObject.model);
      }
      glm::mat4 worldMatrix =
          glm::translate(glm::mat4(1), sceneObject.pos) *
          glm::rotate(glm::mat4(1), glm::radians(sceneObject.yaw), glm::vec3(0, 1, 0)) *
          glm::scale(glm::mat4(1), glm::vec3(sceneObject.scale)) * sceneObject.model->Wm;
      sceneObject.collider.setWorldMatrix(worldMatrix);
    }

    // Set up the flame's point light. It starts burning if the scene used the
    // "_lit" variant; otherwise it starts dark and the player lights it with E.
    // The light is identical either way, so toggling is just flipping `lit`.
    if (isFlame) {
      sceneObject.isFlame = true;
      auto &light = sceneObject.light;
      light = {};
      if (isTorch) {
        light.pos   = glm::vec4(sceneObject.pos + glm::vec3(0, 0.3f, 0), lightPointType);
        light.dir   = glm::vec4(0, 0, 0, 1.0f);             // intensity in w
        light.color = glm::vec4(1.0f, 0.28f, 0.05f, 3.3f);  // saturated orange, range 3.3 m
      } else { // candle
        // Lift the light to roughly flame height (top of the candle) so the glow
        // radiates from where the fire is, not from the base.
        light.pos   = glm::vec4(sceneObject.pos + glm::vec3(0, 0.35f, 0), lightPointType);
        light.dir   = glm::vec4(0, 0, 0, 0.6f);
        light.color = glm::vec4(1.0f, 0.42f, 0.1f, 1.8f);   // amber, range 1.8 m
      }
      light.cones = glm::vec4(0.0f, 0.0f, -1.0f, 0.0f);     // z = -1: no shadow map by default

      sceneObject.baseIntensity = light.dir.w;              // resting intensity, before flicker
      sceneObject.baseEmissive  = sceneObject.emissive;     // glow shown while lit
      // Phase from the object index (cheap pseudo-random) so candles side by side
      // waver out of step instead of pulsing like one big light.
      sceneObject.flamePhase    = static_cast<float>(i) * 1.37f;
      sceneObject.lit           = (modelPath.find("_lit") != std::string::npos);
    }
  }
}

} // namespace scene_loader
