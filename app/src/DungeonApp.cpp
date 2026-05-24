#include <filesystem>

#define STARTER_IMPLEMENTATION
#include "modules/Starter.hpp"

#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"
#include "imgui.h"

struct UniformBufferObject {
  alignas(16) glm::mat4 mvpMat;
  alignas(16) glm::mat4 mMat;
  alignas(16) glm::mat4 nMat;
};

struct GlobalUniformBufferObject {
  alignas(16) glm::vec3 lightDir;
  alignas(16) glm::vec4 lightColor;
  alignas(16) glm::vec3 eyePos;
};

struct Vertex {
  glm::vec3 pos;
  glm::vec3 norm;
  glm::vec2 UV;
  glm::vec4 tan;
};

class DungeonTavernNPC : public BaseProject {
protected:
  DescriptorSetLayout DSLlocal;
  DescriptorSetLayout DSLglobal;

  VertexDescriptor VD;
  RenderPass RP;
  Pipeline P;

  Model MCube;

  DescriptorSet DSglobal;
  DescriptorSet DSlocalCube;

  float Ar;
  glm::mat4 ViewPrj;
  glm::vec3 cameraPos;

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
    DSLlocal.init(this, {{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT,
                          sizeof(UniformBufferObject), 1}});

    DSLglobal.init(this, {{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_ALL_GRAPHICS,
                           sizeof(GlobalUniformBufferObject), 1}});

    VD.init(
        this, {{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX}},
        {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos), sizeof(glm::vec3), POSITION},
         {0, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, norm), sizeof(glm::vec3), NORMAL},
         {0, 2, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, UV), sizeof(glm::vec2), UV},
         {0, 3, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, tan), sizeof(glm::vec4), TANGENT}});

    RP.init(this);
    RP.properties[0].clearValue = {0.08f, 0.10f, 0.16f, 1.0f};

    P.init(this, &VD, "shaders/mesh/MeshTBN.vert.spv", "shaders/mesh/SimpleLambert.frag.spv",
           {&DSLglobal, &DSLlocal});

    MCube.init(this, &VD, "assets/models/primitives/Cube.gltf", GLTF);

    DPSZs.uniformBlocksInPool = 4;
    DPSZs.texturesInPool = 0;
    DPSZs.setsInPool = 4;

    initImGuiContext();

    submitCommandBuffer("main", 0, populateCommandBufferAccess, this);
  }

  void pipelinesAndDescriptorSetsInit() {
    RP.create();
    P.create(&RP);
    initImGuiVulkanBackend();

    DSglobal.init(this, &DSLglobal, {});
    DSlocalCube.init(this, &DSLlocal, {});
  }

  void pipelinesAndDescriptorSetsCleanup() {
    shutdownImGuiVulkanBackend();
    P.cleanup();
    RP.cleanup();
    DSglobal.cleanup();
    DSlocalCube.cleanup();
  }

  void localCleanup() {
    MCube.cleanup();
    DSLlocal.cleanup();
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

    MCube.bind(commandBuffer);
    DSlocalCube.bind(commandBuffer, P, 1, currentImage);
    vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(MCube.indices.size()), 1, 0, 0, 0);

    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);

    RP.end(commandBuffer);
  }

  void updateUniformBuffer(uint32_t currentImage) {
    if (glfwGetKey(window, GLFW_KEY_ESCAPE)) {
      glfwSetWindowShouldClose(window, GL_TRUE);
    }

    float deltaT = GameLogic();

    static float lightRotationAngle = 0.0f;
    lightRotationAngle += 10.0f * deltaT;

    const glm::mat4 lightView =
        glm::rotate(glm::mat4(1), glm::radians(lightRotationAngle), glm::vec3(0, 1, 0)) *
        glm::rotate(glm::mat4(1), glm::radians(-45.0f), glm::vec3(1, 0, 0));
    const glm::vec3 lightDir = glm::vec3(lightView * glm::vec4(0, 0, -1, 0));

    GlobalUniformBufferObject gubo{};
    gubo.lightDir = lightDir;
    gubo.lightColor = glm::vec4(1.0f, 0.97f, 0.92f, 1.0f);
    gubo.eyePos = cameraPos;
    DSglobal.map(currentImage, &gubo, 0);

    static float spin = 0.0f;
    spin += 30.0f * deltaT;

    UniformBufferObject ubo{};
    ubo.mMat = glm::rotate(glm::mat4(1), glm::radians(spin), glm::vec3(0, 1, 0)) *
               glm::scale(glm::mat4(1), glm::vec3(1.0f));
    ubo.mvpMat = ViewPrj * ubo.mMat;
    ubo.nMat = glm::inverse(glm::transpose(ubo.mMat));
    DSlocalCube.map(currentImage, &ubo, 0);

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
    const float farPlane = 100.0f;
    const float ROT_SPEED = glm::radians(120.0f);
    const float MOVE_SPEED = 5.0f;

    float deltaT;
    glm::vec3 m = glm::vec3(0.0f), r = glm::vec3(0.0f);
    bool fire = false;
    getSixAxis(deltaT, m, r, fire);

    static glm::vec3 camPos = glm::vec3(0.0f, 1.0f, 4.0f);
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
