#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <json.hpp>

#define STARTER_IMPLEMENTATION
#include "modules/Starter.hpp"
#include "modules/Colliders.hpp"

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
  Collider collider;
  bool collidable = false;
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

  bool cursorLocked = true;
  glm::vec3 camForward{};
  std::string interactionTarget;

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
        const char *prompt = "[E] Interact";
        ImVec2 tsz = ImGui::CalcTextSize(prompt);
        dl->AddText(ImVec2(center.x - tsz.x * 0.5f, center.y + 30.0f),
                    IM_COL32(255, 255, 200, 220), prompt);
      }
    }

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
      ImGui::TextDisabled("E: interact | F1: cursor");
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

      const auto &t = scene[i].tag;
      scene[i].collidable = (t == "wall" || t == "structure" || t == "furniture" || t == "prop");
      if (scene[i].collidable) {
        scene[i].collider.fitOOBB(&scene[i].model);
        glm::mat4 wm = glm::translate(glm::mat4(1), scene[i].pos) *
                        glm::rotate(glm::mat4(1), glm::radians(scene[i].yaw), glm::vec3(0, 1, 0));
        scene[i].collider.setWorldMatrix(wm);
      }
    }

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
      obj.DS.init(this, &DSLlocalTextured, {Tdungeon.getViewAndSampler()});
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
    const float FOVy = glm::radians(60.0f);
    const float nearPlane = 0.1f;
    const float farPlane = 100.0f;
    const float MOVE_SPEED = 4.0f;
    const float MOUSE_SENS = 0.005f;
    const float EYE_HEIGHT = 1.8f;
    const float PLAYER_RADIUS = 0.3f;
    const float INTERACT_DIST = 2.5f;
    const float INTERACT_DOT = 0.6f;

    float deltaT;
    glm::vec3 m(0.0f), r(0.0f);
    bool fire = false;
    getSixAxis(deltaT, m, r, fire);

    static glm::vec3 camPos = glm::vec3(-6.5f, EYE_HEIGHT, 0.0f);
    static float Yaw = glm::radians(90.0f);
    static float Pitch = 0.0f;
    static double lastMouseX = 0.0, lastMouseY = 0.0;
    static bool firstFrame = true;

    // F1 toggles cursor lock (for ImGui interaction)
    static bool f1Prev = false;
    bool f1Now = glfwGetKey(window, GLFW_KEY_F1) == GLFW_PRESS;
    if (f1Now && !f1Prev) {
      cursorLocked = !cursorLocked;
      glfwSetInputMode(window, GLFW_CURSOR,
                       cursorLocked ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
      firstFrame = true;
    }
    f1Prev = f1Now;

    // Mouse look (direct handling, independent of getSixAxis)
    double mouseX, mouseY;
    glfwGetCursorPos(window, &mouseX, &mouseY);
    if (cursorLocked && !firstFrame) {
      Yaw   += (float)(mouseX - lastMouseX) * MOUSE_SENS;
      Pitch += (float)(mouseY - lastMouseY) * MOUSE_SENS;
    }
    firstFrame = false;
    lastMouseX = mouseX;
    lastMouseY = mouseY;

    Pitch = glm::clamp(Pitch, glm::radians(-89.0f), glm::radians(89.0f));

    // Direction vectors
    glm::vec3 forward = glm::normalize(
        glm::vec3(sin(Yaw) * cos(Pitch), -sin(Pitch), -cos(Yaw) * cos(Pitch)));
    glm::vec3 walkDir = glm::normalize(glm::vec3(sin(Yaw), 0.0f, -cos(Yaw)));
    glm::vec3 right   = glm::normalize(glm::cross(walkDir, glm::vec3(0, 1, 0)));

    // WASD desired movement
    glm::vec3 desiredMove = walkDir * MOVE_SPEED * deltaT * (-m.z) +
                            right   * MOVE_SPEED * deltaT * m.x;

    // Collision test lambda: player AABB from floor to head height
    auto collides = [&](glm::vec3 testPos) {
      Collider pc;
      pc.initAABB(-PLAYER_RADIUS, 0.0f, -PLAYER_RADIUS,
                   PLAYER_RADIUS, EYE_HEIGHT, PLAYER_RADIUS);
      pc.setWorldMatrix(glm::translate(glm::mat4(1),
                        glm::vec3(testPos.x, 0.0f, testPos.z)));
      for (auto &obj : scene) {
        if (!obj.collidable) continue;
        if (pc.collidesWith(obj.collider)) return true;
      }
      return false;
    };

    // Try full movement, fall back to axis-separated (wall sliding)
    glm::vec3 newPos = camPos;
    glm::vec3 fullPos(camPos.x + desiredMove.x, EYE_HEIGHT, camPos.z + desiredMove.z);

    if (!collides(fullPos)) {
      newPos = fullPos;
    } else {
      glm::vec3 tryX(camPos.x + desiredMove.x, EYE_HEIGHT, camPos.z);
      if (!collides(tryX)) newPos.x = tryX.x;

      glm::vec3 tryZ(newPos.x, EYE_HEIGHT, camPos.z + desiredMove.z);
      if (!collides(tryZ)) newPos.z = tryZ.z;
    }

    newPos.y = EYE_HEIGHT;
    camPos = newPos;
    cameraPos = camPos;
    camForward = forward;

    // Interaction detection — find closest interactable in view
    interactionTarget.clear();
    float bestDist = INTERACT_DIST;
    glm::vec3 lookH = glm::normalize(glm::vec3(forward.x, 0.0f, forward.z));
    for (const auto &obj : scene) {
      if (obj.tag != "prop" && obj.tag != "furniture") continue;
      glm::vec3 toObj = obj.pos - camPos;
      toObj.y = 0.0f;
      float dist = glm::length(toObj);
      if (dist < 0.01f || dist > INTERACT_DIST) continue;
      float dot = glm::dot(glm::normalize(toObj), lookH);
      if (dot > INTERACT_DOT && dist < bestDist) {
        bestDist = dist;
        interactionTarget = obj.tag;
      }
    }

    // View-Projection
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
