#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#define STARTER_IMPLEMENTATION
#include "DialogueSystem.hpp"
#include "FirstPersonController.hpp"
#include "SceneLoader.hpp"
#include "SceneTypes.hpp"

#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"
#include "imgui.h"

class DungeonTavernNPC : public BaseProject {
protected:
  DescriptorSetLayout DSLlocalTextured;
  DescriptorSetLayout DSLglobal;

  Texture Tdungeon;

  VertexDescriptor VDsimple;
  RenderPass RP;
  Pipeline Psimple;

  DescriptorSet DSglobal;
  std::vector<SceneObject> scene;
  std::vector<Light> sceneLights;
  std::unordered_map<std::string, std::unique_ptr<Model>> modelCache;
  std::unordered_map<std::string, std::unique_ptr<Texture>> textureCache;

  float Ar;
  glm::mat4 ViewPrj;
  glm::vec3 cameraPos;

  bool imguiContextReady = false;
  bool imguiVulkanReady = false;
  bool showDebugPanel = true;
  float lastDeltaTime = 0.0f;
  float lastFps = 0.0f;

  bool cursorLocked = true;
  FirstPersonController firstPersonController;
  DialogueSystem dialogueSystem;
  glm::vec3 camForward{};
  std::string interactionTarget;
  int interactionObjectIndex = -1;

  static void checkImGuiVkResult(VkResult result) {
    if (result == VK_SUCCESS) {
      return;
    }
    PrintVkError(result);
    throw std::runtime_error("Dear ImGui Vulkan backend error");
  }

  void setWindowParameters() {
    windowWidth = 1280;
    windowHeight = 720;
    windowTitle = "Dungeon Tavern NPC";
    windowResizable = GLFW_TRUE;
    Ar = (float)windowWidth / (float)windowHeight;
  }

  void onWindowResize(int w, int h) {
    Ar = (float)w / (float)h;
    RP.width = w;
    RP.height = h;
  }

  void initImGuiContext() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

    ImGui::StyleColorsDark();
    ImGuiStyle &style = ImGui::GetStyle();
    style.WindowRounding = 4.0f;
    style.FrameRounding = 3.0f;
    style.GrabRounding = 3.0f;

    ImGui_ImplGlfw_InitForVulkan(window, true);
    imguiContextReady = true;
  }

  void initImGuiVulkanBackend() {
    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.ApiVersion = VK_API_VERSION_1_0;
    initInfo.Instance = instance;
    initInfo.PhysicalDevice = physicalDevice;
    initInfo.Device = device;
    initInfo.QueueFamily = findQueueFamilies(physicalDevice).graphicsFamily.value();
    initInfo.Queue = graphicsQueue;
    initInfo.DescriptorPoolSize = 8;
    initInfo.RenderPass = RP.renderPass;
    initInfo.MinImageCount = static_cast<uint32_t>(swapChainImages.size());
    initInfo.ImageCount = static_cast<uint32_t>(swapChainImages.size());
    initInfo.MSAASamples = msaaSamples;
    initInfo.CheckVkResultFn = checkImGuiVkResult;

    if (!ImGui_ImplVulkan_Init(&initInfo)) {
      throw std::runtime_error("failed to initialize Dear ImGui Vulkan backend");
    }
    imguiVulkanReady = true;
  }

  void buildImGuiFrame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    if (cursorLocked) {
      ImDrawList *dl = ImGui::GetForegroundDrawList();
      ImVec2 center(ImGui::GetIO().DisplaySize.x * 0.5f,
                    ImGui::GetIO().DisplaySize.y * 0.5f);
      const float sz = 8.0f;
      ImU32 col = IM_COL32(255, 255, 255, 180);
      dl->AddLine(ImVec2(center.x - sz, center.y),
                  ImVec2(center.x + sz, center.y), col, 2.0f);
      dl->AddLine(ImVec2(center.x, center.y - sz),
                  ImVec2(center.x, center.y + sz), col, 2.0f);

      if (!interactionTarget.empty()) {
        std::string prompt = promptForInteraction();
        ImVec2 tsz = ImGui::CalcTextSize(prompt.c_str());
        dl->AddText(ImVec2(center.x - tsz.x * 0.5f, center.y + 30.0f),
                    IM_COL32(255, 255, 200, 220), prompt.c_str());
      }
    }

    dialogueSystem.draw();

    if (showDebugPanel) {
      ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f), ImGuiCond_FirstUseEver);
      ImGui::SetNextWindowSize(ImVec2(280.0f, 0.0f), ImGuiCond_FirstUseEver);
      ImGui::Begin("Dungeon Tavern NPC", &showDebugPanel, ImGuiWindowFlags_AlwaysAutoResize);
      ImGui::Text("Frame %.3f ms (%.1f FPS)", lastDeltaTime * 1000.0f, lastFps);
      ImGui::Separator();
      ImGui::Text("Camera");
      ImGui::Text("x %.2f  y %.2f  z %.2f", cameraPos.x, cameraPos.y, cameraPos.z);
      ImGui::Separator();
      ImGui::TextDisabled("WASD: move | Mouse: look");
      ImGui::TextDisabled("E: interact/toggle lights | F1: cursor");
      ImGui::End();
    }

    ImGui::Render();
  }

  void shutdownImGuiVulkanBackend() {
    if (!imguiVulkanReady) {
      return;
    }
    ImGui_ImplVulkan_Shutdown();
    imguiVulkanReady = false;
  }

  void shutdownImGuiContext() {
    if (!imguiContextReady) {
      return;
    }
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    imguiContextReady = false;
  }

  Model *getCachedModel(const std::string &modelPath) {
    auto cached = modelCache.find(modelPath);
    if (cached != modelCache.end()) {
      return cached->second.get();
    }

    auto model = std::make_unique<Model>();
    model->init(this, &VDsimple, modelPath.c_str(), GLTF);

    if (model->hasBaseColorTexture) {
      auto texture = std::make_unique<Texture>();
      std::vector<void *> pixels = {model->baseColorPixels.data()};
      texture->initPixels(this, model->baseColorWidth, model->baseColorHeight, 4, 1, pixels);
      textureCache[modelPath] = std::move(texture);
    }

    Model *result = model.get();
    modelCache[modelPath] = std::move(model);
    return result;
  }

  std::string promptForInteraction() const {
    if (interactionObjectIndex >= 0 &&
        interactionObjectIndex < static_cast<int>(scene.size())) {
      const SceneObject &obj = scene[interactionObjectIndex];
      if (obj.togglableLight) {
        return obj.lightActive ? "[E] Turn off" : "[E] Turn on";
      }
    }

    return dialogueSystem.promptFor(interactionTarget);
  }

  void setLightSourceActive(SceneObject &obj, bool active) {
    if (!obj.togglableLight || obj.lightIndex < 0 ||
        obj.lightIndex >= static_cast<int>(sceneLights.size())) {
      return;
    }

    obj.lightActive = active;
    sceneLights[obj.lightIndex] = obj.litLight;
    if (!active) {
      sceneLights[obj.lightIndex].dir.w = 0.0f;
    }
    obj.emissive = active ? obj.litEmissive : obj.unlitEmissive;

    const std::string &targetModelPath = active ? obj.litModelPath : obj.unlitModelPath;
    if (!targetModelPath.empty() && targetModelPath != obj.modelPath) {
      obj.model = getCachedModel(targetModelPath);
      auto cachedTexture = textureCache.find(targetModelPath);
      obj.texture = cachedTexture != textureCache.end() ? cachedTexture->second.get() : nullptr;
      obj.modelPath = targetModelPath;
    }
  }

  void setApplicationIcon() {
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc *pixels = stbi_load("assets/icon/dungeon-tavern-npc-icon.png",
                                &width, &height, &channels, STBI_rgb_alpha);
    if (pixels == nullptr) {
      std::cerr << "Warning: failed to load application icon" << std::endl;
      return;
    }

    GLFWimage icon{};
    icon.width = width;
    icon.height = height;
    icon.pixels = pixels;
    glfwSetWindowIcon(window, 1, &icon);
    stbi_image_free(pixels);
  }

  void localInit() {
    DSLlocalTextured.init(this,
                          {{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_ALL_GRAPHICS,
                            sizeof(UniformBufferObject), 1},
                           {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                            VK_SHADER_STAGE_FRAGMENT_BIT, 0, 1}});

    DSLglobal.init(this, {{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_ALL_GRAPHICS,
                           sizeof(GlobalUniformBufferObject), 1}});

    Tdungeon.init(this, "assets/textures/dungeon/dungeon_texture.png");
    setApplicationIcon();

    VDsimple.init(
        this, {{0, sizeof(VertexSimple), VK_VERTEX_INPUT_RATE_VERTEX}},
        {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VertexSimple, pos), sizeof(glm::vec3), POSITION},
         {0, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VertexSimple, norm), sizeof(glm::vec3), NORMAL},
         {0, 2, VK_FORMAT_R32G32_SFLOAT, offsetof(VertexSimple, UV), sizeof(glm::vec2), UV}});

    RP.init(this);
    RP.properties[0].clearValue = {0.01f, 0.01f, 0.02f, 1.0f};

    Psimple.init(this, &VDsimple, "shaders/mesh/MeshSimple.vert.spv",
                 "shaders/mesh/BlinnPhong.frag.spv", {&DSLglobal, &DSLlocalTextured});

    scene_loader::loadSceneFromJson(
        "assets/scene.json", scene, sceneLights,
        [this](const std::string &modelPath) { return getCachedModel(modelPath); },
        [this](const std::string &modelPath) {
          auto cachedTexture = textureCache.find(modelPath);
          return cachedTexture != textureCache.end() ? cachedTexture->second.get() : nullptr;
        },
        LIGHT_POINT);

    const int objCount = static_cast<int>(scene.size());
    DPSZs.uniformBlocksInPool = 1 + objCount;
    DPSZs.texturesInPool = objCount;
    DPSZs.setsInPool = 1 + objCount;

    initImGuiContext();

    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    if (glfwRawMouseMotionSupported())
      glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);

    submitCommandBuffer("main", 0, populateCommandBufferAccess, this);
  }

  void pipelinesAndDescriptorSetsInit() {
    RP.create();
    Psimple.create(&RP);
    initImGuiVulkanBackend();

    DSglobal.init(this, &DSLglobal, {});
    for (auto &obj : scene) {
      VkDescriptorImageInfo textureInfo =
          obj.texture != nullptr ? obj.texture->getViewAndSampler() : Tdungeon.getViewAndSampler();
      obj.DS.init(this, &DSLlocalTextured, {textureInfo});
    }
  }

  void pipelinesAndDescriptorSetsCleanup() {
    clearCommandBuffers();
    shutdownImGuiVulkanBackend();
    Psimple.cleanup();
    RP.cleanup();
    DSglobal.cleanup();
    for (auto &obj : scene) {
      obj.DS.cleanup();
    }
  }

  void localCleanup() {
    Tdungeon.cleanup();
    for (auto &cachedTexture : textureCache) {
      cachedTexture.second->cleanup();
    }
    for (auto &cachedModel : modelCache) {
      cachedModel.second->cleanup();
    }
    DSLlocalTextured.cleanup();
    DSLglobal.cleanup();
    VDsimple.cleanup();
    Psimple.destroy();
    RP.destroy();
    shutdownImGuiContext();
  }

  static void populateCommandBufferAccess(VkCommandBuffer commandBuffer, int currentImage,
                                          void *Params) {
    DungeonTavernNPC *T = (DungeonTavernNPC *)Params;
    T->populateCommandBuffer(commandBuffer, currentImage);
  }

  void populateCommandBuffer(VkCommandBuffer commandBuffer, int currentImage) {
    RP.begin(commandBuffer, currentImage);

    Psimple.bind(commandBuffer);
    DSglobal.bind(commandBuffer, Psimple, 0, currentImage);

    for (auto &obj : scene) {
      obj.model->bind(commandBuffer);
      obj.DS.bind(commandBuffer, Psimple, 1, currentImage);
      vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(obj.model->indices.size()), 1, 0, 0, 0);
    }

    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);

    RP.end(commandBuffer);
  }

  void updateUniformBuffer(uint32_t currentImage) {
    if (glfwGetKey(window, GLFW_KEY_ESCAPE)) {
      glfwSetWindowShouldClose(window, GL_TRUE);
    }

    float deltaT = GameLogic();

    GlobalUniformBufferObject gubo{};

    // Single-pass forward shading: upload every scene light. The shader loops
    // only over the active count, so cost scales with real lights, not the cap.
    int activeLights = (int)sceneLights.size();
    if (activeLights > MAX_LIGHTS)
      activeLights = MAX_LIGHTS;
    for (int i = 0; i < activeLights; i++)
      gubo.lights[i] = sceneLights[i];
    gubo.eyePos = glm::vec4(cameraPos, (float)activeLights);

    DSglobal.map(currentImage, &gubo, 0);

    for (auto &obj : scene) {
      UniformBufferObject ubo{};
      ubo.mMat = glm::translate(glm::mat4(1), obj.pos) *
                 glm::rotate(glm::mat4(1), glm::radians(obj.yaw), glm::vec3(0, 1, 0)) *
                 glm::scale(glm::mat4(1), glm::vec3(obj.scale)) *
                 obj.model->Wm;
      ubo.mvpMat = ViewPrj * ubo.mMat;
      ubo.nMat = glm::inverse(glm::transpose(ubo.mMat));
      ubo.matParams = glm::vec4(obj.specExp, obj.emissive.r, obj.emissive.g, obj.emissive.b);
      obj.DS.map(currentImage, &ubo, 0);
    }

    static float elapsedT = 0.0f;
    static int countedFrames = 0;
    countedFrames++;
    elapsedT += deltaT;
    if (elapsedT > 1.0f) {
      lastFps = (float)countedFrames / elapsedT;
      elapsedT = 0.0f;
      countedFrames = 0;
    }

    lastDeltaTime = deltaT;
    buildImGuiFrame();
    submitCommandBuffer("main", 0, populateCommandBufferAccess, this);
  }

  float GameLogic() {
    const float FOVy = glm::radians(60.0f);
    const float nearPlane = 0.1f;
    const float farPlane = 100.0f;
    const float INTERACT_DIST = 2.5f;
    const float INTERACT_DOT = 0.6f;

    float deltaT;
    glm::vec3 m(0.0f), r(0.0f);
    bool fire = false;
    getSixAxis(deltaT, m, r, fire);

    // F1 toggles cursor lock (for ImGui interaction)
    static bool f1Prev = false;
    bool f1Now = glfwGetKey(window, GLFW_KEY_F1) == GLFW_PRESS;
    if (f1Now && !f1Prev) {
      cursorLocked = !cursorLocked;
      glfwSetInputMode(window, GLFW_CURSOR,
                       cursorLocked ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
      firstPersonController.resetMouseTracking();
    }
    f1Prev = f1Now;

    double mouseX, mouseY;
    glfwGetCursorPos(window, &mouseX, &mouseY);
    bool jumpPressed = glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;
    FirstPersonController::State playerState =
        firstPersonController.update(deltaT, m, mouseX, mouseY, cursorLocked, jumpPressed, scene);
    cameraPos = playerState.position;
    camForward = playerState.forward;

    // Interaction detection — find closest interactable in view
    interactionTarget.clear();
    interactionObjectIndex = -1;
    float bestDist = INTERACT_DIST;
    glm::vec3 lookH = glm::normalize(glm::vec3(camForward.x, 0.0f, camForward.z));
    for (int i = 0; i < static_cast<int>(scene.size()); i++) {
      const auto &obj = scene[i];
      if (obj.tag != "prop" && obj.tag != "furniture" && obj.tag != "npc" &&
          !obj.togglableLight) continue;
      glm::vec3 toObj = obj.pos - cameraPos;
      toObj.y = 0.0f;
      float dist = glm::length(toObj);
      if (dist < 0.01f || dist > INTERACT_DIST) continue;
      float dot = glm::dot(glm::normalize(toObj), lookH);
      if (dot > INTERACT_DOT && dist < bestDist) {
        bestDist = dist;
        interactionTarget = obj.tag;
        interactionObjectIndex = i;
      }
    }

    static bool ePrev = false;
    bool eNow = glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS;
    bool interactPressed = eNow && !ePrev;
    if (interactPressed && interactionObjectIndex >= 0 &&
        scene[interactionObjectIndex].togglableLight) {
      setLightSourceActive(scene[interactionObjectIndex],
                           !scene[interactionObjectIndex].lightActive);
      dialogueSystem.update(interactionTarget, false);
    } else {
      dialogueSystem.update(interactionTarget, interactPressed);
    }
    ePrev = eNow;

    // View-Projection
    glm::mat4 Prj = glm::perspective(FOVy, Ar, nearPlane, farPlane);
    Prj[1][1] *= -1;
    glm::mat4 View = glm::lookAt(cameraPos, cameraPos + camForward, glm::vec3(0, 1, 0));
    ViewPrj = Prj * View;

    return deltaT;
  }
};

int main(int argc, char **argv) {
  const char *executablePath = argc > 0 ? argv[0] : nullptr;
  if (executablePath != nullptr) {
    const std::filesystem::path executable = std::filesystem::absolute(executablePath);
    const std::filesystem::path executableDir = executable.parent_path();
    if (!executableDir.empty()) {
      std::filesystem::current_path(executableDir);
    }
  }

  DungeonTavernNPC app;

  try {
    app.run();
  } catch (const std::exception &e) {
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
