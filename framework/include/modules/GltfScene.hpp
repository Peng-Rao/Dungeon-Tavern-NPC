#pragma once

#include "Starter.hpp"

#include <filesystem>
#include <memory>

struct GltfSceneVertex {
  glm::vec3 pos;
  glm::vec3 norm;
  glm::vec2 UV;
  glm::vec4 tan;
};

struct GltfSceneUniform {
  alignas(16) glm::mat4 mvpMat;
  alignas(16) glm::mat4 mMat;
  alignas(16) glm::mat4 nMat;
  alignas(16) glm::vec4 baseColorFactor;
};

class GltfScene {
  struct Batch {
    std::string name;
    glm::vec4 baseColorFactor = glm::vec4(1.0f);
    Texture *baseColorTexture = nullptr;
    Model model;
    DescriptorSet descriptorSet;
    bool hasGeometry = false;
    bool modelInitialized = false;
    bool descriptorSetInitialized = false;
  };

  BaseProject *BP = nullptr;
  VertexDescriptor *VD = nullptr;
  std::vector<Batch> batches;
  std::vector<std::unique_ptr<Texture>> textures;
  std::unique_ptr<Texture> fallbackTexture;
  glm::vec3 boundsMin = glm::vec3(0.0f);
  glm::vec3 boundsMax = glm::vec3(0.0f);
  bool hasBounds = false;
  size_t vertexCount = 0;
  size_t indexCount = 0;

  static int numComponents(int type) {
    switch (type) {
    case TINYGLTF_TYPE_SCALAR:
      return 1;
    case TINYGLTF_TYPE_VEC2:
      return 2;
    case TINYGLTF_TYPE_VEC3:
      return 3;
    case TINYGLTF_TYPE_VEC4:
      return 4;
    default:
      throw std::runtime_error("unsupported glTF accessor type");
    }
  }

  static int componentSize(int componentType) {
    switch (componentType) {
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
    case TINYGLTF_COMPONENT_TYPE_BYTE:
      return 1;
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
    case TINYGLTF_COMPONENT_TYPE_SHORT:
      return 2;
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
    case TINYGLTF_COMPONENT_TYPE_FLOAT:
      return 4;
    default:
      throw std::runtime_error("unsupported glTF component type");
    }
  }

  static size_t byteStride(const tinygltf::Accessor &accessor,
                           const tinygltf::BufferView &bufferView) {
    if (bufferView.byteStride != 0) {
      return bufferView.byteStride;
    }
    return componentSize(accessor.componentType) * numComponents(accessor.type);
  }

  static const unsigned char *accessorData(const tinygltf::Model &model,
                                           const tinygltf::Accessor &accessor,
                                           const tinygltf::BufferView &bufferView,
                                           size_t elementIndex) {
    const tinygltf::Buffer &buffer = model.buffers[bufferView.buffer];
    return buffer.data.data() + bufferView.byteOffset + accessor.byteOffset +
           byteStride(accessor, bufferView) * elementIndex;
  }

  static glm::vec2 readVec2(const tinygltf::Model &model, int accessorIndex, size_t index) {
    const tinygltf::Accessor &accessor = model.accessors[accessorIndex];
    const tinygltf::BufferView &bufferView = model.bufferViews[accessor.bufferView];
    if (accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT ||
        accessor.type != TINYGLTF_TYPE_VEC2) {
      throw std::runtime_error("expected glTF vec2 float accessor");
    }
    const float *value =
        reinterpret_cast<const float *>(accessorData(model, accessor, bufferView, index));
    return glm::vec2(value[0], value[1]);
  }

  static glm::vec3 readVec3(const tinygltf::Model &model, int accessorIndex, size_t index) {
    const tinygltf::Accessor &accessor = model.accessors[accessorIndex];
    const tinygltf::BufferView &bufferView = model.bufferViews[accessor.bufferView];
    if (accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT ||
        accessor.type != TINYGLTF_TYPE_VEC3) {
      throw std::runtime_error("expected glTF vec3 float accessor");
    }
    const float *value =
        reinterpret_cast<const float *>(accessorData(model, accessor, bufferView, index));
    return glm::vec3(value[0], value[1], value[2]);
  }

  static uint32_t readIndex(const tinygltf::Model &model, int accessorIndex, size_t index) {
    const tinygltf::Accessor &accessor = model.accessors[accessorIndex];
    const tinygltf::BufferView &bufferView = model.bufferViews[accessor.bufferView];
    const unsigned char *value = accessorData(model, accessor, bufferView, index);

    switch (accessor.componentType) {
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
      return *reinterpret_cast<const uint8_t *>(value);
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
      return *reinterpret_cast<const uint16_t *>(value);
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
      return *reinterpret_cast<const uint32_t *>(value);
    default:
      throw std::runtime_error("unsupported glTF index component type");
    }
  }

  static glm::mat4 nodeLocalTransform(const tinygltf::Node &node) {
    if (node.matrix.size() == 16) {
      glm::mat4 result(1.0f);
      for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
          result[c][r] = static_cast<float>(node.matrix[c * 4 + r]);
        }
      }
      return result;
    }

    glm::vec3 translation(0.0f);
    if (node.translation.size() == 3) {
      translation = glm::vec3(node.translation[0], node.translation[1], node.translation[2]);
    }

    glm::quat rotation(1.0f, 0.0f, 0.0f, 0.0f);
    if (node.rotation.size() == 4) {
      rotation = glm::quat(node.rotation[3], node.rotation[0], node.rotation[1], node.rotation[2]);
    }

    glm::vec3 scale(1.0f);
    if (node.scale.size() == 3) {
      scale = glm::vec3(node.scale[0], node.scale[1], node.scale[2]);
    }

    return glm::translate(glm::mat4(1.0f), translation) * glm::mat4(rotation) *
           glm::scale(glm::mat4(1.0f), scale);
  }

  void includePointInBounds(const glm::vec3 &point) {
    if (!hasBounds) {
      boundsMin = point;
      boundsMax = point;
      hasBounds = true;
      return;
    }
    boundsMin = glm::min(boundsMin, point);
    boundsMax = glm::max(boundsMax, point);
  }

  void appendPrimitive(const tinygltf::Model &sceneModel, const tinygltf::Primitive &primitive,
                       const glm::mat4 &world) {
    auto positionIt = primitive.attributes.find("POSITION");
    auto normalIt = primitive.attributes.find("NORMAL");
    if (positionIt == primitive.attributes.end() || normalIt == primitive.attributes.end()) {
      return;
    }

    const int materialIndex =
        (primitive.material >= 0 && primitive.material < static_cast<int>(batches.size()))
            ? primitive.material
            : 0;
    Batch &batch = batches[materialIndex];
    Model &dst = batch.model;
    const uint32_t vertexOffset =
        static_cast<uint32_t>(dst.vertices.size() / sizeof(GltfSceneVertex));

    const tinygltf::Accessor &positionAccessor = sceneModel.accessors[positionIt->second];
    const size_t primitiveVertexCount = positionAccessor.count;
    const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(world)));
    const auto uvIt = primitive.attributes.find("TEXCOORD_0");

    for (size_t i = 0; i < primitiveVertexCount; ++i) {
      GltfSceneVertex vertex{};
      const glm::vec3 localPos = readVec3(sceneModel, positionIt->second, i);
      const glm::vec3 localNormal = readVec3(sceneModel, normalIt->second, i);
      vertex.pos = glm::vec3(world * glm::vec4(localPos, 1.0f));
      vertex.norm = glm::normalize(normalMatrix * localNormal);
      vertex.UV = uvIt != primitive.attributes.end() ? readVec2(sceneModel, uvIt->second, i)
                                                     : glm::vec2(0.0f);
      vertex.tan = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);

      const unsigned char *bytes = reinterpret_cast<const unsigned char *>(&vertex);
      dst.vertices.insert(dst.vertices.end(), bytes, bytes + sizeof(GltfSceneVertex));
      includePointInBounds(vertex.pos);
    }

    if (primitive.indices >= 0) {
      const tinygltf::Accessor &indexAccessor = sceneModel.accessors[primitive.indices];
      for (size_t i = 0; i < indexAccessor.count; ++i) {
        dst.indices.push_back(vertexOffset + readIndex(sceneModel, primitive.indices, i));
      }
      indexCount += indexAccessor.count;
    } else {
      for (size_t i = 0; i < primitiveVertexCount; ++i) {
        dst.indices.push_back(vertexOffset + static_cast<uint32_t>(i));
      }
      indexCount += primitiveVertexCount;
    }

    batch.hasGeometry = true;
    vertexCount += primitiveVertexCount;
  }

  void appendMesh(const tinygltf::Model &sceneModel, int meshIndex, const glm::mat4 &world) {
    if (meshIndex < 0 || meshIndex >= static_cast<int>(sceneModel.meshes.size())) {
      return;
    }
    const tinygltf::Mesh &mesh = sceneModel.meshes[meshIndex];
    for (const tinygltf::Primitive &primitive : mesh.primitives) {
      if (primitive.mode != TINYGLTF_MODE_TRIANGLES && primitive.mode != -1) {
        continue;
      }
      appendPrimitive(sceneModel, primitive, world);
    }
  }

  void appendNode(const tinygltf::Model &sceneModel, int nodeIndex, const glm::mat4 &parentWorld) {
    if (nodeIndex < 0 || nodeIndex >= static_cast<int>(sceneModel.nodes.size())) {
      return;
    }
    const tinygltf::Node &node = sceneModel.nodes[nodeIndex];
    const glm::mat4 world = parentWorld * nodeLocalTransform(node);
    appendMesh(sceneModel, node.mesh, world);

    for (int child : node.children) {
      appendNode(sceneModel, child, world);
    }
  }

  void initFallbackTexture() {
    uint32_t whitePixel = 0xffffffffu;
    std::vector<void *> pixels = {&whitePixel};
    fallbackTexture = std::make_unique<Texture>();
    fallbackTexture->initPixels(BP, 1, 1, 4, 1, pixels, VK_FORMAT_R8G8B8A8_SRGB);
  }

public:
  void init(BaseProject *bp, VertexDescriptor *vd, const std::string &file) {
    BP = bp;
    VD = vd;
    vertexCount = 0;
    indexCount = 0;
    hasBounds = false;

    tinygltf::TinyGLTF loader;
    tinygltf::Model sceneModel;
    std::string warn;
    std::string err;

    std::cout << "Loading glTF scene: " << file << "\n";
    if (!loader.LoadASCIIFromFile(&sceneModel, &warn, &err, file.c_str())) {
      throw std::runtime_error(warn + err);
    }
    if (!warn.empty()) {
      std::cout << warn << "\n";
    }

    const std::filesystem::path baseDir = std::filesystem::path(file).parent_path();
    initFallbackTexture();

    textures.resize(sceneModel.images.size());
    for (size_t i = 0; i < sceneModel.images.size(); ++i) {
      const tinygltf::Image &image = sceneModel.images[i];
      if (image.uri.empty()) {
        continue;
      }
      textures[i] = std::make_unique<Texture>();
      textures[i]->init(BP, (baseDir / image.uri).string(), VK_FORMAT_R8G8B8A8_SRGB);
    }

    const size_t materialCount = std::max<size_t>(1, sceneModel.materials.size());
    batches.resize(materialCount);
    for (size_t i = 0; i < materialCount; ++i) {
      batches[i].name = (i < sceneModel.materials.size() && !sceneModel.materials[i].name.empty())
                            ? sceneModel.materials[i].name
                            : ("material_" + std::to_string(i));
      batches[i].baseColorTexture = fallbackTexture.get();

      if (i >= sceneModel.materials.size()) {
        continue;
      }

      const tinygltf::Material &material = sceneModel.materials[i];
      const auto factorIt = material.values.find("baseColorFactor");
      if (factorIt != material.values.end() && factorIt->second.number_array.size() == 4) {
        batches[i].baseColorFactor =
            glm::vec4(factorIt->second.number_array[0], factorIt->second.number_array[1],
                      factorIt->second.number_array[2], factorIt->second.number_array[3]);
      }

      const auto textureIt = material.values.find("baseColorTexture");
      if (textureIt != material.values.end()) {
        const int textureIndex = textureIt->second.TextureIndex();
        if (textureIndex >= 0 && textureIndex < static_cast<int>(sceneModel.textures.size())) {
          const int imageIndex = sceneModel.textures[textureIndex].source;
          if (imageIndex >= 0 && imageIndex < static_cast<int>(textures.size()) &&
              textures[imageIndex] != nullptr) {
            batches[i].baseColorTexture = textures[imageIndex].get();
          }
        }
      }
    }

    const int sceneIndex = sceneModel.defaultScene >= 0 ? sceneModel.defaultScene : 0;
    if (sceneIndex < 0 || sceneIndex >= static_cast<int>(sceneModel.scenes.size())) {
      throw std::runtime_error("glTF file does not contain a valid default scene");
    }

    for (int nodeIndex : sceneModel.scenes[sceneIndex].nodes) {
      appendNode(sceneModel, nodeIndex, glm::mat4(1.0f));
    }

    size_t drawableBatches = 0;
    for (Batch &batch : batches) {
      if (!batch.hasGeometry) {
        continue;
      }
      batch.model.initMesh(BP, VD, false);
      batch.modelInitialized = true;
      ++drawableBatches;
    }

    std::cout << "glTF scene loaded: " << drawableBatches << " material batches, " << vertexCount
              << " vertices, " << indexCount << " indices\n";
  }

  void initDescriptorSets(DescriptorSetLayout *materialLayout) {
    for (Batch &batch : batches) {
      if (!batch.hasGeometry) {
        continue;
      }
      batch.descriptorSet.init(BP, materialLayout, {batch.baseColorTexture->getViewAndSampler()});
      batch.descriptorSetInitialized = true;
    }
  }

  void cleanupDescriptorSets() {
    for (Batch &batch : batches) {
      if (batch.descriptorSetInitialized) {
        batch.descriptorSet.cleanup();
        batch.descriptorSetInitialized = false;
      }
    }
  }

  void updateUniforms(uint32_t currentImage, const glm::mat4 &viewProjection,
                      const glm::mat4 &modelMatrix) {
    GltfSceneUniform uniform{};
    uniform.mMat = modelMatrix;
    uniform.mvpMat = viewProjection * modelMatrix;
    uniform.nMat = glm::inverse(glm::transpose(modelMatrix));

    for (Batch &batch : batches) {
      if (!batch.descriptorSetInitialized) {
        continue;
      }
      uniform.baseColorFactor = batch.baseColorFactor;
      batch.descriptorSet.map(currentImage, &uniform, 0);
    }
  }

  void draw(VkCommandBuffer commandBuffer, Pipeline &pipeline, int setId, int currentImage) {
    for (Batch &batch : batches) {
      if (!batch.hasGeometry || !batch.descriptorSetInitialized) {
        continue;
      }
      batch.model.bind(commandBuffer);
      batch.descriptorSet.bind(commandBuffer, pipeline, setId, currentImage);
      vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(batch.model.indices.size()), 1, 0, 0,
                       0);
    }
  }

  void cleanup() {
    cleanupDescriptorSets();

    for (Batch &batch : batches) {
      if (batch.modelInitialized) {
        batch.model.cleanup();
        batch.modelInitialized = false;
      }
    }

    for (std::unique_ptr<Texture> &texture : textures) {
      if (texture != nullptr) {
        texture->cleanup();
      }
    }
    textures.clear();

    if (fallbackTexture != nullptr) {
      fallbackTexture->cleanup();
      fallbackTexture.reset();
    }

    batches.clear();
  }

  size_t drawableBatchCount() const {
    size_t result = 0;
    for (const Batch &batch : batches) {
      if (batch.hasGeometry) {
        ++result;
      }
    }
    return result;
  }

  size_t vertices() const {
    return vertexCount;
  }

  size_t indices() const {
    return indexCount;
  }

  glm::vec3 center() const {
    return hasBounds ? (boundsMin + boundsMax) * 0.5f : glm::vec3(0.0f);
  }

  float radius() const {
    if (!hasBounds) {
      return 1.0f;
    }
    return std::max(glm::length(boundsMax - center()), 0.001f);
  }
};
