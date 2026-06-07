#include "SceneLoader.hpp"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include <glm/gtc/matrix_transform.hpp>
#include <json.hpp>

namespace scene_loader {
namespace {

bool isCollidableTag(const std::string &tag) {
  return tag == "wall" || tag == "structure" || tag == "furniture" || tag == "prop" ||
         tag == "door";
}

bool isLightSourceTag(const std::string &tag) {
  return tag == "light_source";
}

bool isDoorTag(const std::string &tag) {
  return tag == "door";
}

bool hasSuffix(const std::string &value, const std::string &suffix) {
  return value.size() >= suffix.size() &&
         value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string litVariantPath(const std::string &modelPath) {
  if (hasSuffix(modelPath, "_lit.gltf")) {
    return modelPath;
  }

  const std::size_t ext = modelPath.rfind(".gltf");
  if (ext == std::string::npos) {
    return modelPath;
  }

  std::string candidate = modelPath.substr(0, ext) + "_lit" + modelPath.substr(ext);
  return std::filesystem::exists(candidate) ? candidate : modelPath;
}

std::string unlitVariantPath(const std::string &modelPath) {
  if (!hasSuffix(modelPath, "_lit.gltf")) {
    return modelPath;
  }

  std::string candidate = modelPath;
  candidate.replace(candidate.size() - std::string("_lit.gltf").size(),
                    std::string("_lit.gltf").size(), ".gltf");
  return std::filesystem::exists(candidate) ? candidate : modelPath;
}

bool isInitiallyLitLightSource(const std::string &modelPath) {
  return modelPath.find("_lit") != std::string::npos ||
         modelPath.find("candle_triple") != std::string::npos;
}

Light makeLightForModel(const std::string &modelPath, const glm::vec3 &position,
                        int lightPointType) {
  const bool isTorch = modelPath.find("torch") != std::string::npos;
  Light light{};
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
  return light;
}

glm::vec3 defaultLightEmissive(const std::string &modelPath) {
  return modelPath.find("torch") != std::string::npos ? glm::vec3(1.0f, 0.28f, 0.05f)
                                                       : glm::vec3(0.8f, 0.34f, 0.08f);
}

void appendLitModelLight(const std::string &modelPath, const glm::vec3 &position,
                         std::vector<Light> &sceneLights, int lightPointType) {
  if (modelPath.find("_lit") == std::string::npos) {
    return;
  }

  sceneLights.push_back(makeLightForModel(modelPath, position, lightPointType));
}

void configureInteractiveLight(SceneObject &sceneObject, const std::string &modelPath,
                               std::vector<Light> &sceneLights, int lightPointType) {
  if (!isLightSourceTag(sceneObject.tag)) {
    appendLitModelLight(modelPath, sceneObject.pos, sceneLights, lightPointType);
    return;
  }

  sceneObject.togglableLight = true;
  sceneObject.lightActive = isInitiallyLitLightSource(modelPath);
  sceneObject.litModelPath = litVariantPath(modelPath);
  sceneObject.unlitModelPath = unlitVariantPath(modelPath);
  sceneObject.unlitEmissive = sceneObject.emissive;
  sceneObject.litEmissive =
      sceneObject.emissive == glm::vec3(0.0f) ? defaultLightEmissive(modelPath)
                                              : sceneObject.emissive;

  sceneObject.litLight = makeLightForModel(modelPath, sceneObject.pos, lightPointType);
  sceneLights.push_back(sceneObject.litLight);
  sceneObject.lightIndex = static_cast<int>(sceneLights.size()) - 1;
  if (!sceneObject.lightActive) {
    sceneLights[sceneObject.lightIndex].dir.w = 0.0f;
  }
  sceneObject.emissive =
      sceneObject.lightActive ? sceneObject.litEmissive : sceneObject.unlitEmissive;
}

} // namespace

void loadSceneFromJson(const std::string &scenePath, std::vector<SceneObject> &scene,
                       std::vector<Light> &sceneLights, const ModelResolver &resolveModel,
                       const TextureResolver &resolveTexture, int lightPointType) {
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

    sceneObject.togglableDoor = isDoorTag(sceneObject.tag);
    sceneObject.doorOpen = jsonObject.value("open", false);
    sceneObject.openModelPath = jsonObject.value("openModel", modelPath);
    sceneObject.closedModelPath = jsonObject.value("closedModel", modelPath);

    const std::string activeModelPath =
        sceneObject.togglableDoor && sceneObject.doorOpen ? sceneObject.openModelPath
                                                          : sceneObject.closedModelPath;

    sceneObject.modelPath = activeModelPath;
    sceneObject.model = resolveModel(activeModelPath);
    sceneObject.texture = resolveTexture(activeModelPath);
    sceneObject.collidable =
        isCollidableTag(sceneObject.tag) && !(sceneObject.togglableDoor && sceneObject.doorOpen);
    if (sceneObject.collidable) {
      sceneObject.collider.fitOOBB(sceneObject.model);
      glm::mat4 worldMatrix =
          glm::translate(glm::mat4(1), sceneObject.pos) *
          glm::rotate(glm::mat4(1), glm::radians(sceneObject.yaw), glm::vec3(0, 1, 0)) *
          glm::scale(glm::mat4(1), glm::vec3(sceneObject.scale)) * sceneObject.model->Wm;
      sceneObject.collider.setWorldMatrix(worldMatrix);
    }

    configureInteractiveLight(sceneObject, activeModelPath, sceneLights, lightPointType);
  }
}

} // namespace scene_loader
