#pragma once

#include <functional>
#include <string>
#include <vector>

#include "SceneTypes.hpp"

namespace scene_loader {

using ModelResolver = std::function<Model *(const std::string &modelPath)>;
using TextureResolver = std::function<Texture *(const std::string &modelPath)>;

void loadSceneFromJson(const std::string &scenePath, std::vector<SceneObject> &scene,
                       std::vector<Light> &sceneLights, const ModelResolver &resolveModel,
                       const TextureResolver &resolveTexture, int lightPointType);

} // namespace scene_loader
