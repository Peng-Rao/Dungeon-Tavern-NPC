#pragma once

#include <cstddef>
#include <fstream>
#include <stdexcept>
#include <string>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <json.hpp>

namespace scene_loader {

inline bool isCollidableTag(const std::string &tag) {
  return tag == "wall" || tag == "structure" || tag == "furniture" || tag == "prop";
}

template <typename LightVector>
void appendLitModelLight(const std::string &modelPath, const glm::vec3 &position,
                         LightVector &sceneLights, int lightPointType) {
  if (modelPath.find("_lit") == std::string::npos) {
    return;
  }

  const bool isTorch = modelPath.find("torch") != std::string::npos;
  typename LightVector::value_type light{};
  if (isTorch) {
    light.pos   = glm::vec4(position + glm::vec3(0, 0.3f, 0), lightPointType);
    light.dir   = glm::vec4(0, 0, 0, 1.0f);
    light.color = glm::vec4(1.0f, 0.28f, 0.05f, 3.3f);
  } else {
    light.pos   = glm::vec4(position + glm::vec3(0, 0.15f, 0), lightPointType);
    light.dir   = glm::vec4(0, 0, 0, 0.6f);
    light.color = glm::vec4(1.0f, 0.42f, 0.1f, 1.8f);
  }
  light.cones = glm::vec4(0.0f);
  sceneLights.push_back(light);
}

template <typename SceneObjects, typename LightVector, typename ModelResolver,
          typename TextureResolver>
void loadSceneFromJson(const std::string &scenePath, SceneObjects &scene, LightVector &sceneLights,
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
    sceneObject.scale = jsonObject.value("scale", 1.0f);
    sceneObject.tag = jsonObject.value("tag", "");
    sceneObject.specExp = jsonObject.value("specExp", 32.0f);
    if (jsonObject.contains("emissive")) {
      const auto &emissive = jsonObject["emissive"];
      sceneObject.emissive =
          glm::vec3(emissive[0].get<float>(), emissive[1].get<float>(),
                    emissive[2].get<float>());
    }

    sceneObject.model = resolveModel(modelPath);
    sceneObject.texture = resolveTexture(modelPath);
    sceneObject.collidable = isCollidableTag(sceneObject.tag);
    if (sceneObject.collidable) {
      sceneObject.collider.fitOOBB(sceneObject.model);
      glm::mat4 worldMatrix =
          glm::translate(glm::mat4(1), sceneObject.pos) *
          glm::rotate(glm::mat4(1), glm::radians(sceneObject.yaw), glm::vec3(0, 1, 0)) *
          glm::scale(glm::mat4(1), glm::vec3(sceneObject.scale)) * sceneObject.model->Wm;
      sceneObject.collider.setWorldMatrix(worldMatrix);
    }

    appendLitModelLight(modelPath, sceneObject.pos, sceneLights, lightPointType);
  }
}

} // namespace scene_loader
