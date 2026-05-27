#include <filesystem>

#define STARTER_IMPLEMENTATION
#include "modules/Starter.hpp"

#include "modules/GltfScene.hpp"

#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"
#include "imgui.h"

struct GlobalUniformBufferObject {
  alignas(16) glm::vec3 lightDir;
  alignas(16) glm::vec4 lightColor;
  alignas(16) glm::vec3 eyePos;
};

class DungeonTavernNPC : public BaseProject {
protected:
  DescriptorSetLayout DSLmaterial;
  DescriptorSetLayout DSLglobal;

  VertexDescriptor VD;
  RenderPass RP;
  Pipeline P;

  GltfScene Tavern;

  DescriptorSet DSglobal;

  float Ar;
  glm::mat4 ViewPrj;
  glm::vec3 cameraPos;
  glm::mat4 tavernModelMatrix = glm::mat4(1.0f);

  bool imguiContextReady = false;
  bool imguiVulkanReady = false;
  bool showDebugPanel = true;
  float lastDeltaTime = 0.0f;
  float lastFps = 0.0f;

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

    if (showDebugPanel) {
      ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f), ImGuiCond_FirstUseEver);
      ImGui::SetNextWindowSize(ImVec2(280.0f, 0.0f), ImGuiCond_FirstUseEver);
      ImGui::Begin("Dungeon Tavern NPC", &showDebugPanel, ImGuiWindowFlags_AlwaysAutoResize);
      ImGui::Text("Frame %.3f ms (%.1f FPS)", lastDeltaTime * 1000.0f, lastFps);
      ImGui::Separator();
      ImGui::Text("Camera");
      ImGui::Text("x %.2f  y %.2f  z %.2f", cameraPos.x, cameraPos.y, cameraPos.z);
      ImGui::Separator();
      ImGui::Text("Tavern");
      ImGui::Text("%zu vertices, %zu indices", Tavern.vertices(), Tavern.indices());
      ImGui::Text("%zu material batches", Tavern.drawableBatchCount());
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

  void localInit() {
    DSLglobal.init(this, {{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_ALL_GRAPHICS,
                           sizeof(GlobalUniformBufferObject), 1}});

    DSLmaterial.init(this,
                     {{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       sizeof(GltfSceneUniform), 1},
                      {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, 1}});

    VD.init(
        this, {{0, sizeof(GltfSceneVertex), VK_VERTEX_INPUT_RATE_VERTEX}},
        {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(GltfSceneVertex, pos), sizeof(glm::vec3),
          POSITION},
         {0, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(GltfSceneVertex, norm), sizeof(glm::vec3),
          NORMAL},
         {0, 2, VK_FORMAT_R32G32_SFLOAT, offsetof(GltfSceneVertex, UV), sizeof(glm::vec2), UV},
         {0, 3, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(GltfSceneVertex, tan),
          sizeof(glm::vec4), TANGENT}});

    RP.init(this);
    RP.properties[0].clearValue = {0.08f, 0.10f, 0.16f, 1.0f};

    P.init(this, &VD, "shaders/mesh/GltfScene.vert.spv", "shaders/mesh/GltfScene.frag.spv",
           {&DSLglobal, &DSLmaterial});
    P.setCullMode(VK_CULL_MODE_NONE);

    Tavern.init(this, &VD, "assets/models/tavern/scene.gltf");
    const size_t batchCount = Tavern.drawableBatchCount();

    DPSZs.uniformBlocksInPool = 1 + static_cast<int>(batchCount);
    DPSZs.texturesInPool = static_cast<int>(batchCount);
    DPSZs.setsInPool = 1 + static_cast<int>(batchCount);

    initImGuiContext();

    submitCommandBuffer("main", 0, populateCommandBufferAccess, this);
  }

  void pipelinesAndDescriptorSetsInit() {
    RP.create();
    P.create(&RP);
    initImGuiVulkanBackend();

    DSglobal.init(this, &DSLglobal, {});
    Tavern.initDescriptorSets(&DSLmaterial);
  }

  void pipelinesAndDescriptorSetsCleanup() {
    shutdownImGuiVulkanBackend();
    Tavern.cleanupDescriptorSets();
    DSglobal.cleanup();
    P.cleanup();
    RP.cleanup();
  }

  void localCleanup() {
    Tavern.cleanup();
    DSLmaterial.cleanup();
    DSLglobal.cleanup();
    VD.cleanup();
    P.destroy();
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

    P.bind(commandBuffer);
    DSglobal.bind(commandBuffer, P, 0, currentImage);
    Tavern.draw(commandBuffer, P, 1, currentImage);

    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);

    RP.end(commandBuffer);
  }

  void updateUniformBuffer(uint32_t currentImage) {
    if (glfwGetKey(window, GLFW_KEY_ESCAPE)) {
      glfwSetWindowShouldClose(window, GL_TRUE);
    }

    float deltaT = GameLogic();

    const glm::vec3 lightDir = glm::normalize(glm::vec3(-0.35f, -1.0f, -0.25f));

    GlobalUniformBufferObject gubo{};
    gubo.lightDir = lightDir;
    gubo.lightColor = glm::vec4(1.0f, 0.93f, 0.82f, 1.0f);
    gubo.eyePos = cameraPos;
    DSglobal.map(currentImage, &gubo, 0);

    const float tavernScale = 4.2f / Tavern.radius();
    tavernModelMatrix = glm::scale(glm::mat4(1.0f), glm::vec3(tavernScale)) *
                        glm::translate(glm::mat4(1.0f), -Tavern.center());
    Tavern.updateUniforms(currentImage, ViewPrj, tavernModelMatrix);

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
    const float FOVy = glm::radians(45.0f);
    const float nearPlane = 0.1f;
    const float farPlane = 150.0f;
    const float ROT_SPEED = glm::radians(120.0f);
    const float MOVE_SPEED = 4.0f;

    float deltaT;
    glm::vec3 m = glm::vec3(0.0f), r = glm::vec3(0.0f);
    bool fire = false;
    getSixAxis(deltaT, m, r, fire);

    static glm::vec3 camPos = glm::vec3(0.0f, 1.3f, 8.0f);
    static float Yaw = glm::radians(0.0f);
    static float Pitch = 0.0f;

    Yaw += ROT_SPEED * deltaT * r.y;
    Pitch += ROT_SPEED * deltaT * r.x;
    Pitch = glm::clamp(Pitch, glm::radians(-89.0f), glm::radians(89.0f));

    glm::vec3 forward =
        glm::normalize(glm::vec3(sin(Yaw) * cos(Pitch), -sin(Pitch), -cos(Yaw) * cos(Pitch)));
    glm::vec3 walkForward = glm::normalize(glm::vec3(sin(Yaw), 0.0f, -cos(Yaw)));
    glm::vec3 right = glm::normalize(glm::cross(walkForward, glm::vec3(0, 1, 0)));

    camPos += walkForward * MOVE_SPEED * deltaT * (-m.z);
    camPos += right * MOVE_SPEED * deltaT * m.x;

    if (glfwGetKey(window, GLFW_KEY_SPACE))
      camPos += glm::vec3(0, 1, 0) * MOVE_SPEED * deltaT;
    if (glfwGetKey(window, GLFW_KEY_TAB))
      camPos -= glm::vec3(0, 1, 0) * MOVE_SPEED * deltaT;

    cameraPos = camPos;

    glm::mat4 Prj = glm::perspective(FOVy, Ar, nearPlane, farPlane);
    Prj[1][1] *= -1;

    glm::mat4 View = glm::lookAt(camPos, camPos + forward, glm::vec3(0, 1, 0));
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
