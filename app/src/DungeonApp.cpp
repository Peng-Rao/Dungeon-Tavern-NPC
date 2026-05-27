#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <json.hpp>

#define STARTER_IMPLEMENTATION
#include "modules/Starter.hpp"

#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"
#include "imgui.h"

// Light types — must match #define values in BlinnPhong.frag
constexpr int LIGHT_POINT       = 0;
constexpr int LIGHT_SPOT        = 1;
constexpr int LIGHT_DIRECTIONAL = 2;
constexpr int MAX_LIGHTS        = 8;

struct Light {
  alignas(16) glm::vec4 pos;    // xyz = world position,  w = type (LIGHT_*)
  alignas(16) glm::vec4 dir;    // xyz = direction,        w = intensity
  alignas(16) glm::vec4 color;  // rgb = color,            a = range (0 = infinite)
  alignas(16) glm::vec4 cones;  // x  = cos(innerAngle),   y = cos(outerAngle)
};

struct GlobalUniformBufferObject {
  alignas(16) glm::vec4 eyePos;           // xyz = eye position, w = active light count
  Light lights[MAX_LIGHTS];
};

struct UniformBufferObject {
  alignas(16) glm::mat4 mvpMat;
  alignas(16) glm::mat4 mMat;
  alignas(16) glm::mat4 nMat;
  // x = specular exponent, yzw = emissive RGB color
  alignas(16) glm::vec4 matParams;
};

struct VertexSimple {
  glm::vec3 pos;
  glm::vec3 norm;
  glm::vec2 UV;
};

struct SceneObject {
  Model model;
  DescriptorSet DS;
  glm::vec3 pos;
  float yaw;
  std::string tag;
};

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
    DSLlocalTextured.init(this,
                          {{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_ALL_GRAPHICS,
                            sizeof(UniformBufferObject), 1},
                           {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                            VK_SHADER_STAGE_FRAGMENT_BIT, 0, 1}});

    DSLglobal.init(this, {{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_ALL_GRAPHICS,
                           sizeof(GlobalUniformBufferObject), 1}});

    Tdungeon.init(this, "assets/textures/dungeon/dungeon_texture.png");

    VDsimple.init(
        this, {{0, sizeof(VertexSimple), VK_VERTEX_INPUT_RATE_VERTEX}},
        {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VertexSimple, pos), sizeof(glm::vec3), POSITION},
         {0, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VertexSimple, norm), sizeof(glm::vec3), NORMAL},
         {0, 2, VK_FORMAT_R32G32_SFLOAT, offsetof(VertexSimple, UV), sizeof(glm::vec2), UV}});

    RP.init(this);
    RP.properties[0].clearValue = {0.01f, 0.01f, 0.02f, 1.0f};

    Psimple.init(this, &VDsimple, "shaders/mesh/MeshSimple.vert.spv",
                 "shaders/mesh/BlinnPhong.frag.spv", {&DSLglobal, &DSLlocalTextured});

    // ---- Scene definition from JSON ----
    nlohmann::json sceneJson;
    {
      std::ifstream f("assets/scene.json");
      if (!f.is_open()) {
        throw std::runtime_error("Cannot open assets/scene.json");
      }
      f >> sceneJson;
    }

    const auto &objects = sceneJson["objects"];
    scene.resize(objects.size());
    for (size_t i = 0; i < objects.size(); i++) {
      const auto &obj = objects[i];
      const auto &p   = obj["pos"];
      scene[i].pos = glm::vec3(p[0].get<float>(), p[1].get<float>(), p[2].get<float>());
      scene[i].yaw = obj.value("yaw", 0.0f);
      scene[i].tag = obj.value("tag", "");
      scene[i].model.init(this, &VDsimple, obj["model"].get<std::string>().c_str(), GLTF);
    }

    const int objCount = static_cast<int>(scene.size());
    DPSZs.uniformBlocksInPool = 1 + objCount;
    DPSZs.texturesInPool = objCount;
    DPSZs.setsInPool = 1 + objCount;

    initImGuiContext();

    submitCommandBuffer("main", 0, populateCommandBufferAccess, this);
  }

  void pipelinesAndDescriptorSetsInit() {
    RP.create();
    Psimple.create(&RP);
    initImGuiVulkanBackend();

    DSglobal.init(this, &DSLglobal, {});
    for (auto &obj : scene) {
      obj.DS.init(this, &DSLlocalTextured, {Tdungeon.getViewAndSampler()});
    }
  }

  void pipelinesAndDescriptorSetsCleanup() {
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
    for (auto &obj : scene) {
      obj.model.cleanup();
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
      obj.model.bind(commandBuffer);
      obj.DS.bind(commandBuffer, Psimple, 1, currentImage);
      vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(obj.model.indices.size()), 1, 0, 0, 0);
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
    gubo.eyePos = glm::vec4(cameraPos, 3.0f); // w = number of active lights

    // Left wall torch — spotlight pointing into the room and slightly downward
    gubo.lights[0].pos   = glm::vec4(-3.5f, 2.0f, 0.0f, LIGHT_SPOT);
    gubo.lights[0].dir   = glm::vec4(glm::normalize(glm::vec3(1.0f, -0.3f, 0.0f)), 3.0f);
    gubo.lights[0].color = glm::vec4(1.0f, 0.6f, 0.2f, 0.0f); // warm orange, infinite range
    gubo.lights[0].cones = glm::vec4(glm::cos(glm::radians(30.0f)),
                                     glm::cos(glm::radians(60.0f)), 0.0f, 0.0f);

    // Right wall torch — spotlight pointing into the room and slightly downward
    gubo.lights[1].pos   = glm::vec4(3.5f, 2.0f, 0.0f, LIGHT_SPOT);
    gubo.lights[1].dir   = glm::vec4(glm::normalize(glm::vec3(-1.0f, -0.3f, 0.0f)), 3.0f);
    gubo.lights[1].color = glm::vec4(1.0f, 0.6f, 0.2f, 0.0f); // warm orange, infinite range
    gubo.lights[1].cones = glm::vec4(glm::cos(glm::radians(30.0f)),
                                     glm::cos(glm::radians(60.0f)), 0.0f, 0.0f);

    // Table candle — point light, small range
    gubo.lights[2].pos   = glm::vec4(0.0f, 1.05f, 0.0f, LIGHT_POINT);
    gubo.lights[2].dir   = glm::vec4(0.0f, 0.0f, 0.0f, 1.5f); // intensity 1.5
    gubo.lights[2].color = glm::vec4(1.0f, 0.85f, 0.4f, 3.0f); // warm yellow, range 3 m
    gubo.lights[2].cones = glm::vec4(0.0f);

    DSglobal.map(currentImage, &gubo, 0);

    for (auto &obj : scene) {
      UniformBufferObject ubo{};
      ubo.mMat = glm::translate(glm::mat4(1), obj.pos) *
                 glm::rotate(glm::mat4(1), glm::radians(obj.yaw), glm::vec3(0, 1, 0));
      ubo.mvpMat = ViewPrj * ubo.mMat;
      ubo.nMat = glm::inverse(glm::transpose(ubo.mMat));
      ubo.matParams = glm::vec4(32.0f, 0.0f, 0.0f, 0.0f); // specExp=32, no emissive
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
