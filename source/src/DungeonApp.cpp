#include <array>
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// The framework headers (Starter.hpp + Colliders.hpp) are pulled in — once, in
// the correct order — through SceneTypes.hpp. The library *implementation* is
// compiled separately in Libs.cpp, so we must NOT define STARTER_IMPLEMENTATION
// here: the skeleton's Starter.hpp has no include guard and would otherwise be
// pulled into this TU twice (and re-run the stb/tinygltf implementations).
#include "SceneTypes.hpp"

#include "DayNightCycle.hpp"
#include "DialogueSystem.hpp"
#include "FirstPersonController.hpp"
#include "SceneLoader.hpp"
#include "ShopSystem.hpp"
#include "SplashScreen.hpp"
#include "InputBindings.hpp"

#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"
#include "imgui.h"

constexpr int SUN_SHADOW_RES    = 2048;        // sun shadow map covers the whole
                                               // scene, so it needs more resolution
constexpr int SPOT_SHADOW_RES   = 1024;        // per-spotlight 2D shadow map; each
                                               // covers only one torch's cone, so a
                                               // smaller map than the sun's suffices

// Exponential rate of the door swing (1/s): yaw closes this fraction of the
// remaining angle per second, so the leaf starts fast and settles gently.
constexpr float DOOR_SWING_RATE = 6.0f;

// Patrolling NPCs: exponential turn rate (1/s), and how close the player can
// get before the NPC stops walking and turns to face them.
constexpr float NPC_TURN_RATE = 8.0f;
constexpr float NPC_PERSONAL_SPACE = 1.3f;

// Uniforms for the skybox pipeline. mvpMat is the rotation-only view-projection
// (the cube tracks the camera so the sky never translates); sunDirDay carries
// the to-sun direction (xyz) and the day factor (w) so the sky can dim to night
// and place a sun/moon disc; sunColor is the current sun/moon tint.
struct SkyboxUBO {
  alignas(16) glm::mat4 mvpMat;
  alignas(16) glm::vec4 sunDirDay;
  alignas(16) glm::vec4 sunColor;
};

class DungeonTavernNPC : public BaseProject {
protected:
  DescriptorSetLayout DSLlocalTextured;
  DescriptorSetLayout DSLglobal;
  DescriptorSetLayout DSLskybox;

  Texture Tdungeon;
  Texture TenvMap; // sky cube map sampled by the skybox (and, later, for ambient)

  VertexDescriptor VDsimple;
  VertexDescriptor VDskybox; // position-only view of the cube's vertex buffer
  RenderPass RP;
  Pipeline Psimple;
  Pipeline Pskybox;

  DescriptorSet DSglobal;
  DescriptorSet DSskybox;
  Model *skyboxCube = nullptr; // the unit cube the sky is painted on (shared cache)

  // Directional sun shadow: a single 2D depth map rendered with an orthographic
  // projection from the sun, so its light is clipped by the walls and only
  // reaches the interior through windows and the open doorway.
  RenderPass RPsun;     // offscreen, depth-only
  Pipeline PsunShadow;  // writes scene depth from the sun's point of view

  // Spotlight shadows: the exact same 2D-shadow-map technique as the sun (and as
  // exercise E07), but with a *perspective* light frustum instead of an
  // orthographic one — because a spotlight is a positioned cone of light, not a
  // parallel beam. One offscreen depth map per shadow-casting torch.
  RenderPass RPspot[MAX_SHADOW_SPOTS]; // offscreen depth-only, one per spot slot
  Pipeline PspotShadow;                // shares the SunShadow shaders (push-const lightVP)
  struct SpotShadow {
    glm::mat4 lightVP{1.0f}; // world -> spot clip (depth pass + main-pass sampling)
    int emitterIndex = -1;   // the torch that owns this map (skipped so it can't self-shadow)
  };
  SpotShadow spotShadows[MAX_SHADOW_SPOTS];
  int activeSpotShadows = 0; // how many slots are in use this frame

  glm::vec3 sceneCenter{0.0f}; // centre of the scene's bounds (ortho frustum focus)
  float sceneRadius = 20.0f;   // half-extent of the scene's bounds (ortho size)
  std::vector<SceneObject> scene;
  std::unordered_map<std::string, std::unique_ptr<Model>> modelCache;
  // Textures are owned here keyed by *texture file path*, so models that share an
  // atlas (e.g. every dungeon prop -> dungeon_texture.png) share one Texture.
  std::unordered_map<std::string, std::unique_ptr<Texture>> textureCache;
  // Non-owning: which shared Texture each model path uses (nullptr -> Tdungeon).
  std::unordered_map<std::string, Texture *> modelTextureCache;

  float animTime = 0.0f;            // seconds since start, drives the flicker

  float Ar;
  glm::mat4 ViewPrj;
  glm::mat4 SkyViewPrj; // ViewPrj with the camera translation stripped (for the skybox)
  glm::mat4 sunLightVP{1.0f}; // world->light clip for the sun (depth pass + sampling)
  glm::vec3 cameraPos;

  bool imguiContextReady = false;
  bool imguiVulkanReady = false;
  bool showDebugPanel = true;
  float lastDeltaTime = 0.0f;
  float lastFps = 0.0f;

  bool cursorLocked = false; // freed for the splash menu; Start captures it
  FirstPersonController firstPersonController;
  DialogueSystem dialogueSystem;
  ShopSystem shopSystem;
  SplashScreen splashScreen;
  std::vector<Texture> shopIconTextures; // item preview PNGs shown in the shop table
  bool shopIconsRegistered = false;      // ImGui descriptor registration is lazy
  glm::vec3 camForward{};
  std::string interactionTarget;
  std::string targetNpcId; // npcId of the NPC currently aimed at ("" when none)
  std::string shopNpcId;   // npcId of the NPC whose shop is open ("" when closed)

  // Day/night cycle (pure logic, owns no Vulkan resources). It advances on its
  // own every frame — continuous and automatic, with no user control — and the
  // resulting State drives the directional sun light, and later the skybox tint
  // and the sun's shadow map.
  DayNightCycle dayNight;
  DayNightCycle::State sunState;

  // Exterior ground plane: a large procedural quad at y=0 that keeps the tavern
  // from appearing to float. Generated via initMesh (not loaded from a file).
  std::unique_ptr<Model> groundModel;
  Texture Tground;

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
    initInfo.DescriptorPoolSize = 16; // font + one per shop item icon, with headroom
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

    // The icon descriptor sets need the ImGui Vulkan backend, which comes up
    // after localInit — register them on the first frame instead.
    if (!shopIconsRegistered) {
      for (int i = 0; i < (int)shopIconTextures.size(); i++) {
        shopSystem.setIcon(i, (ImTextureID)ImGui_ImplVulkan_AddTexture(
                                  shopIconTextures[i].sampler->textureSampler,
                                  shopIconTextures[i].textureImageView,
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
      }
      shopIconsRegistered = true;
    }

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
    shopSystem.draw();
    splashScreen.draw();

    if (showDebugPanel && !splashScreen.isActive()) {
      ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f), ImGuiCond_FirstUseEver);
      ImGui::SetNextWindowSize(ImVec2(280.0f, 0.0f), ImGuiCond_FirstUseEver);
      ImGui::Begin("Dungeon Tavern NPC", &showDebugPanel, ImGuiWindowFlags_AlwaysAutoResize);
      ImGui::Text("Frame %.3f ms (%.1f FPS)", lastDeltaTime * 1000.0f, lastFps);
      ImGui::Separator();
      ImGui::Text("Camera");
      ImGui::Text("x %.2f  y %.2f  z %.2f", cameraPos.x, cameraPos.y, cameraPos.z);
      ImGui::Separator();
      ImGui::TextDisabled("WASD: move | Mouse: look");
      ImGui::TextDisabled("E: interact | %s: cursor", input_bindings::ToggleCursorLabel);
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

  // Render the whole scene's depth from the sun's point of view into the 2D
  // shadow map, using the orthographic sun matrix. Runs before the main pass so
  // the result is ready to sample there. Always recorded (even at night, when
  // the result is simply not applied) so the depth image is never left in an
  // undefined layout for the sampler.
  void renderSunShadow(VkCommandBuffer cb, int currentImage) {
    RPsun.begin(cb, 0);
    PsunShadow.bind(cb);
    vkCmdPushConstants(cb, PsunShadow.pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                       sizeof(sunLightVP), &sunLightVP);
    for (auto &obj : scene) {
      if (obj.tag == "ground") continue; // ground receives sun shadow, never casts it
      Model *mesh = (obj.lit && obj.hasLitVariant) ? obj.litModel : obj.model;
      obj.DS.bind(cb, PsunShadow, 0, currentImage); // set 0 = object UBO (for mMat)
      mesh->bind(cb);
      vkCmdDrawIndexed(cb, static_cast<uint32_t>(mesh->indices.size()), 1, 0, 0, 0);
    }
    RPsun.end(cb);
  }

  // Build the world->clip matrix for one spotlight's shadow map. Same recipe as
  // the sun's, but PERSPECTIVE instead of orthographic: a spotlight is a point of
  // light with a direction (a cone), so its shadow frustum converges like a real
  // camera, whereas the sun's parallel rays use an orthographic box. The FOV is
  // the cone's full outer angle (acos of cones.y, the cos of the outer half
  // angle) plus a small margin so the cone's soft edge isn't clipped.
  glm::mat4 computeSpotLightVP(const Light &L) const {
    glm::vec3 p    = glm::vec3(L.pos);
    glm::vec3 axis = glm::normalize(glm::vec3(L.dir));
    float outerCos = glm::clamp(L.cones.y, -1.0f, 1.0f);
    float fov      = glm::min(2.0f * std::acos(outerCos) + glm::radians(8.0f), glm::radians(160.0f));
    float farP     = (L.color.a > 0.0f) ? L.color.a : 10.0f;
    glm::vec3 up   = (std::abs(axis.y) > 0.99f) ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
    glm::mat4 view = glm::lookAt(p, p + axis, up);
    glm::mat4 proj = glm::perspective(fov, 1.0f, 0.05f, farP);
    // Vulkan clip-space Y flip, applied as a scale on the projection (the course
    // convention), exactly like the sun's matrix and the main camera.
    proj = glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, -1.0f, 1.0f)) * proj;
    return proj * view;
  }

  // Render scene depth from each active spotlight into its 2D shadow map, before
  // the main pass samples them. One pass per slot, mirroring renderSunShadow; the
  // emitter torch is skipped so it cannot shadow itself.
  void renderSpotShadows(VkCommandBuffer cb, int currentImage) {
    for (int s = 0; s < activeSpotShadows; s++) {
      RPspot[s].begin(cb, 0);
      PspotShadow.bind(cb);
      vkCmdPushConstants(cb, PspotShadow.pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                         sizeof(glm::mat4), &spotShadows[s].lightVP);
      for (int o = 0; o < (int)scene.size(); o++) {
        if (o == spotShadows[s].emitterIndex) continue; // the torch never shadows itself
        if (scene[o].tag == "ground") continue;         // ground receives, never casts
        SceneObject &obj = scene[o];
        Model *mesh = (obj.lit && obj.hasLitVariant) ? obj.litModel : obj.model;
        obj.DS.bind(cb, PspotShadow, 0, currentImage); // set 0 = object UBO (for mMat)
        mesh->bind(cb);
        vkCmdDrawIndexed(cb, static_cast<uint32_t>(mesh->indices.size()), 1, 0, 0, 0);
      }
      RPspot[s].end(cb);
    }
  }

  // Resolve the texture file a model uses by reading images[0].uri from its glTF
  // (relative to the model's folder). Returns "" if the model declares no
  // external image (e.g. a data: URI), in which case the shared Tdungeon is used.
  std::string texturePathForModel(const std::string &modelPath) const {
    std::ifstream file(modelPath);
    if (!file.is_open()) return "";
    nlohmann::json gltf;
    try {
      file >> gltf;
    } catch (const std::exception &) {
      return "";
    }
    if (!gltf.contains("images") || gltf["images"].empty()) return "";
    const auto &image = gltf["images"][0];
    if (!image.contains("uri")) return "";
    const std::string uri = image["uri"].get<std::string>();
    if (uri.empty() || uri.rfind("data:", 0) == 0) return "";  // skip embedded data URIs
    return (std::filesystem::path(modelPath).parent_path() / uri).string();
  }

  // Load (or reuse) the texture file at `texturePath`. Keyed by texture path, so
  // every model sharing an atlas resolves to the same Texture.
  Texture *getCachedTexture(const std::string &texturePath) {
    auto cached = textureCache.find(texturePath);
    if (cached != textureCache.end()) {
      return cached->second.get();
    }
    auto texture = std::make_unique<Texture>();
    texture->init(this, texturePath);  // official file loader (stb), default SRGB
    Texture *result = texture.get();
    textureCache[texturePath] = std::move(texture);
    return result;
  }

  Model *getCachedModel(const std::string &modelPath) {
    auto cached = modelCache.find(modelPath);
    if (cached != modelCache.end()) {
      return cached->second.get();
    }

    auto model = std::make_unique<Model>();
    model->init(this, &VDsimple, modelPath.c_str(), GLTF);

    // Resolve the model's texture once (per distinct model) and record which
    // shared Texture it maps to, for the scene loader's texture callback.
    const std::string texturePath = texturePathForModel(modelPath);
    if (!texturePath.empty()) {
      modelTextureCache[modelPath] = getCachedTexture(texturePath);
    }

    Model *result = model.get();
    modelCache[modelPath] = std::move(model);
    return result;
  }

  // Object world matrix shared by rendering and colliders.
  static glm::mat4 objectWorld(const SceneObject &obj, float yawDeg) {
    return glm::translate(glm::mat4(1), obj.pos) *
           glm::rotate(glm::mat4(1), glm::radians(yawDeg), glm::vec3(0, 1, 0)) *
           glm::scale(glm::mat4(1), glm::vec3(obj.scale)) * obj.model->Wm;
  }

  static void refreshColliderWorld(SceneObject &obj) {
    obj.collider.setWorldMatrix(objectWorld(obj, obj.yaw));
  }

  // Shop open/close owns cursor capture: browsing wares needs the mouse free,
  // and play needs it locked again afterwards.
  void setShopOpen(bool on) {
    shopSystem.setOpen(on);
    if (!on) {
      shopNpcId.clear();
    }
    cursorLocked = !on;
    glfwSetInputMode(window, GLFW_CURSOR,
                     cursorLocked ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    firstPersonController.resetMouseTracking();
  }

  // Shortest-arc exponential turn toward a heading (degrees).
  static void turnNpcToward(SceneObject &obj, float desiredYaw, float deltaT) {
    float diff = desiredYaw - obj.yaw;
    while (diff > 180.0f) diff -= 360.0f;
    while (diff < -180.0f) diff += 360.0f;
    obj.yaw += diff * std::min(1.0f, NPC_TURN_RATE * deltaT);
  }

  // Start a door swing — unless the player stands where the leaf would come
  // to rest, in which case the door stays put rather than closing on them.
  void tryToggleDoor(SceneObject &door) {
    const float targetYaw = door.doorOpen ? door.closedYaw : door.openYaw;

    Collider pose;
    pose.fitOOBB(door.model);
    pose.setWorldMatrix(objectWorld(door, targetYaw));

    Collider player;
    const float radius = 0.35f; // controller radius plus a little margin
    player.initAABB(-radius, 0.0f, -radius, radius, FirstPersonController::EYE_HEIGHT, radius);
    player.setWorldMatrix(glm::translate(
        glm::mat4(1),
        glm::vec3(cameraPos.x, cameraPos.y - FirstPersonController::EYE_HEIGHT, cameraPos.z)));

    if (player.collidesWith(pose)) return;
    door.doorOpen = !door.doorOpen;
  }

  // The crosshair prompt for whatever we're aiming at: the NPC goes through the
  // dialogue system ("[E] Talk"); flames and doors already carry their own
  // "[E] ..." action text in interactionTarget.
  std::string promptForInteraction() const {
    if (interactionTarget == "npc") {
      return dialogueSystem.promptFor(interactionTarget);
    }
    return interactionTarget;
  }

  // Loads the window/taskbar icon. stb (bundled by the framework) decodes the
  // PNG; we free the pixels right after GLFW copies them.
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
    // PERF: the framework picks the GPU's MAXIMUM MSAA level (often 8x) and the
    // pipeline forces per-sample shading, so the fragment shader runs once per
    // sample — at 8x that is 8x the per-pixel cost, which on its own can drag the
    // framerate down regardless of how light the geometry is. msaaSamples is a
    // public framework field, so we can cap it here (before the render pass and
    // pipelines are built) without editing the framework itself. 4x still looks
    // clean; drop to 2x (or VK_SAMPLE_COUNT_1_BIT) if more speed is needed.
    if (msaaSamples > VK_SAMPLE_COUNT_4_BIT) {
      msaaSamples = VK_SAMPLE_COUNT_4_BIT;
    }

    DSLlocalTextured.init(this,
                          {{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_ALL_GRAPHICS,
                            sizeof(UniformBufferObject), 1},
                           {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                            VK_SHADER_STAGE_FRAGMENT_BIT, 0, 1}});

    DSLglobal.init(this, {{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_ALL_GRAPHICS,
                           sizeof(GlobalUniformBufferObject), 1},
                          // binding 1: the sun's 2D shadow map (E07-style directional
                          // depth map). The 4th field is this binding's start offset
                          // into the image-info vector passed to DescriptorSet::init.
                          {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, 1},
                          // binding 2: the per-spotlight 2D shadow maps, packed right
                          // after the sun map (offset 1) in the image-info vector.
                          {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT,
                           1, MAX_SHADOW_SPOTS}});

    // Skybox set: binding 0 the per-frame UBO (mvp + sun params, read in both
    // stages), binding 1 the sky cube map.
    DSLskybox.init(this, {{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_ALL_GRAPHICS,
                           sizeof(SkyboxUBO), 1},
                          {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, 1}});

    Tdungeon.init(this, "assets/textures/dungeon/dungeon_texture.png");
    Tground.init(this, "assets/textures/ground/ground.png");
    // Sky cube map (6 faces). Order expected by initCubic: +X,-X,+Y,-Y,+Z,-Z.
    TenvMap.initCubic(this, {"assets/textures/skybox/px.png", "assets/textures/skybox/nx.png",
                             "assets/textures/skybox/py.png", "assets/textures/skybox/ny.png",
                             "assets/textures/skybox/pz.png", "assets/textures/skybox/nz.png"});
    setApplicationIcon();

    VDsimple.init(
        this, {{0, sizeof(VertexSimple), VK_VERTEX_INPUT_RATE_VERTEX}},
        {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VertexSimple, pos), sizeof(glm::vec3), POSITION},
         {0, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VertexSimple, norm), sizeof(glm::vec3), NORMAL},
         {0, 2, VK_FORMAT_R32G32_SFLOAT, offsetof(VertexSimple, UV), sizeof(glm::vec2), UV}});

    // The skybox reads only positions, but from the same cube buffer (stride is
    // still a full VertexSimple). A second, leaner vertex format on the same
    // mesh — exactly the "different formats for different needs" idea.
    VDskybox.init(this, {{0, sizeof(VertexSimple), VK_VERTEX_INPUT_RATE_VERTEX}},
                  {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VertexSimple, pos),
                    sizeof(glm::vec3), POSITION}});

    RP.init(this);
    RP.properties[0].clearValue = {0.01f, 0.01f, 0.02f, 1.0f};

    Psimple.init(this, &VDsimple, "shaders/mesh/MeshSimple.vert.spv",
                 "shaders/mesh/BlinnPhong.frag.spv", {&DSLglobal, &DSLlocalTextured});

    // Skybox pipeline: we are inside the cube (cull front), and depth-test with
    // LESS_OR_EQUAL so the sky (pushed to the far plane) survives only where no
    // opaque geometry already wrote depth.
    Pskybox.init(this, &VDskybox, "shaders/skybox/Skybox.vert.spv",
                 "shaders/skybox/Skybox.frag.spv", {&DSLskybox});
    Pskybox.setCompareOp(VK_COMPARE_OP_LESS_OR_EQUAL);
    Pskybox.setCullMode(VK_CULL_MODE_FRONT_BIT);

    // Offscreen depth-only pass for the sun's shadow map. Fixed size (it covers
    // the whole scene, not the window), so it is independent of the swap chain.
    RPsun.init(this, SUN_SHADOW_RES, SUN_SHADOW_RES, 1,
               RenderPass::getStandardAttchmentsProperties(AT_DEPTH_ONLY, this),
               RenderPass::getStandardDependencies(ATDEP_DEPTH_TRANS), true);

    // The depth pass only needs each object's model matrix (set 0 = its UBO) and
    // the sun's view-projection (a push constant). Cull NONE because dungeon
    // walls can be single-sided planes — culling could drop them from the depth
    // map and leak light through them.
    // A depth-only pass needs only the vertex position, so it uses the
    // position-only vertex layout (VDskybox) rather than the full pos/norm/uv
    // one — otherwise the unused normal/UV attributes trip a validation warning.
    PsunShadow.init(this, &VDskybox, "shaders/mesh/SunShadow.vert.spv",
                    "shaders/mesh/SunShadow.frag.spv", {&DSLlocalTextured},
                    {{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4)}});
    PsunShadow.setCullMode(VK_CULL_MODE_NONE);

    // One offscreen depth-only pass per spotlight shadow slot — same setup as the
    // sun's, just smaller and several of them. The pipeline reuses the SunShadow
    // shaders (a pushed lightVP * the object's mMat); it is created against
    // RPspot[0] so its baked viewport matches the spot map resolution.
    for (int s = 0; s < MAX_SHADOW_SPOTS; s++) {
      RPspot[s].init(this, SPOT_SHADOW_RES, SPOT_SHADOW_RES, 1,
                     RenderPass::getStandardAttchmentsProperties(AT_DEPTH_ONLY, this),
                     RenderPass::getStandardDependencies(ATDEP_DEPTH_TRANS), true);
    }
    PspotShadow.init(this, &VDskybox, "shaders/mesh/SunShadow.vert.spv",
                     "shaders/mesh/SunShadow.frag.spv", {&DSLlocalTextured},
                     {{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4)}});
    PspotShadow.setCullMode(VK_CULL_MODE_NONE);

    scene_loader::loadSceneFromJson(
        "assets/scenes/scene.json", scene,
        [this](const std::string &modelPath) { return getCachedModel(modelPath); },
        [this](const std::string &modelPath) -> Texture * {
          auto it = modelTextureCache.find(modelPath);
          return it != modelTextureCache.end() ? it->second : nullptr;
        },
        LIGHT_POINT, LIGHT_SPOT);

    dialogueSystem.load("assets/dialogue/dialogues.json");

    // Splash backdrop view before the controller takes over — matches the
    // first-person spawn at the far end of the corridor, facing the gate.
    cameraPos = glm::vec3(-18.0f, FirstPersonController::EYE_HEIGHT, 0.0f);
    camForward = glm::vec3(1.0f, 0.0f, 0.0f);

    // Shop item preview images (rendered from the KayKit models in Blender).
    shopIconTextures.resize(shopSystem.itemCount());
    for (int i = 0; i < shopSystem.itemCount(); i++) {
      shopIconTextures[i].init(this, shopSystem.iconFile(i));
    }

    // World-space bounding sphere per object (model AABB through its world
    // matrix), so the shadow pass can cheaply cull objects a light can't reach.
    // Doors get a hinge-centred sphere instead, wide enough for every swing
    // pose, since their yaw animates at runtime.
    for (auto &obj : scene) {
      Collider bounds;
      bounds.fitAABB(obj.model);
      if (obj.isDoor) {
        bounds.setWorldMatrix(glm::mat4(1));
        AABBextents e = bounds.getExtents();
        float reach = std::sqrt(std::max(e.xMin * e.xMin, e.xMax * e.xMax) +
                                std::max(e.zMin * e.zMin, e.zMax * e.zMax)) *
                      obj.scale;
        float halfH = 0.5f * (e.yMax - e.yMin) * obj.scale;
        obj.boundsCenter = obj.pos + glm::vec3(0.0f, (e.yMin * obj.scale) + halfH, 0.0f);
        obj.boundsRadius = std::sqrt(reach * reach + halfH * halfH);
        continue;
      }
      bounds.setWorldMatrix(objectWorld(obj, obj.yaw));
      AABBextents e = bounds.getExtents();
      obj.boundsCenter = 0.5f * glm::vec3(e.xMin + e.xMax, e.yMin + e.yMax, e.zMin + e.zMax);
      obj.boundsRadius =
          0.5f * glm::length(glm::vec3(e.xMax - e.xMin, e.yMax - e.yMin, e.zMax - e.zMin));
    }

    // The cube the sky is painted on (shares the model cache; position-only at
    // draw time via VDskybox).
    skyboxCube = getCachedModel("assets/models/primitives/Cube.gltf");

    // Overall scene bounds from the per-object spheres, so the sun's orthographic
    // shadow frustum can be sized to cover everything.
    {
      glm::vec3 lo(1e9f), hi(-1e9f);
      for (const auto &obj : scene) {
        lo = glm::min(lo, obj.boundsCenter - glm::vec3(obj.boundsRadius));
        hi = glm::max(hi, obj.boundsCenter + glm::vec3(obj.boundsRadius));
      }
      sceneCenter = 0.5f * (lo + hi);
      sceneRadius = 0.5f * glm::length(hi - lo);
    }

    // Large exterior ground plane: a single quad at y=0, big enough that its
    // edge disappears at the horizon (far plane = 100, so H = 150 is safe), with
    // tiled UVs so the texture repeats instead of stretching. It is inserted
    // *after* sceneCenter/sceneRadius so it does not inflate the sun frustum, but
    // it participates in every other pass (illumination + shadow reception).
    {
      constexpr float H = 150.0f;  // half-extent of the quad
      constexpr float T = 75.0f;   // UV repeat count (one tile every 2 world units)

      // CCW winding when viewed from above (+Y), matching the scene's convention.
      // v0(-H,-H) v1(+H,-H) v2(+H,+H) v3(-H,+H) in the XZ plane, all at y=0.
      const std::array<VertexSimple, 4> verts = {{
          {{-H, 0.0f, -H}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
          {{ H, 0.0f, -H}, {0.0f, 1.0f, 0.0f}, {T,   0.0f}},
          {{ H, 0.0f,  H}, {0.0f, 1.0f, 0.0f}, {T,   T   }},
          {{-H, 0.0f,  H}, {0.0f, 1.0f, 0.0f}, {0.0f, T  }},
      }};
      const std::array<uint32_t, 6> idx = {0, 2, 1, 0, 3, 2};

      groundModel = std::make_unique<Model>();
      const auto *vBytes = reinterpret_cast<const unsigned char *>(verts.data());
      groundModel->vertices.assign(vBytes, vBytes + sizeof(verts));
      groundModel->indices.assign(idx.begin(), idx.end());
      groundModel->initMesh(this, &VDsimple, false);

      SceneObject ground{};
      ground.model   = groundModel.get();
      ground.texture = &Tground;
      ground.pos     = glm::vec3(sceneCenter.x, 0.0f, sceneCenter.z);
      ground.yaw     = 0.0f;
      ground.scale   = 1.0f;
      ground.tag     = "ground";
      ground.collidable = false;
      ground.specExp    = 1024.0f; // rough stone: specular peak so narrow it's invisible
      ground.boundsCenter = ground.pos;
      ground.boundsRadius = H * 1.5f;
      scene.push_back(std::move(ground));
    }

    const int objCount = static_cast<int>(scene.size());
    // +1 across the board for the skybox set (its own UBO + cube sampler).
    DPSZs.uniformBlocksInPool = 1 + objCount + 1;
    // objCount object albedos +1 (skybox cube) +1 (sun shadow) +MAX_SHADOW_SPOTS (spot maps).
    DPSZs.texturesInPool = objCount + 1 + 1 + MAX_SHADOW_SPOTS;
    DPSZs.setsInPool = 1 + objCount + 1;

    initImGuiContext();

    // Boot into the splash menu with a free cursor; Start captures it.
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    if (glfwRawMouseMotionSupported())
      glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);

    submitCommandBuffer("main", 0, populateCommandBufferAccess, this);
  }

  void pipelinesAndDescriptorSetsInit() {
    RP.create();
    RPsun.create();
    for (int s = 0; s < MAX_SHADOW_SPOTS; s++) RPspot[s].create();
    Psimple.create(&RP);
    Pskybox.create(&RP);
    PsunShadow.create(&RPsun);
    PspotShadow.create(&RPspot[0]); // viewport baked at SPOT_SHADOW_RES; all RPspot match
    DSskybox.init(this, &DSLskybox, {TenvMap.getViewAndSampler()});
    initImGuiVulkanBackend();

    // The global set's image infos are, in order: binding 1 = the sun's 2D shadow
    // map, then binding 2 = the MAX_SHADOW_SPOTS spotlight maps (this exact order
    // matches the per-binding offsets declared in DSLglobal above).
    std::vector<VkDescriptorImageInfo> shadowInfos;
    shadowInfos.reserve(1 + MAX_SHADOW_SPOTS);
    shadowInfos.push_back(RPsun.attachments[0].getViewAndSampler());
    for (int s = 0; s < MAX_SHADOW_SPOTS; s++) {
      shadowInfos.push_back(RPspot[s].attachments[0].getViewAndSampler());
    }
    DSglobal.init(this, &DSLglobal, shadowInfos);
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
    Pskybox.cleanup();
    PsunShadow.cleanup();
    PspotShadow.cleanup();
    RP.cleanup();
    RPsun.cleanup();
    for (int s = 0; s < MAX_SHADOW_SPOTS; s++) RPspot[s].cleanup();
    DSglobal.cleanup();
    DSskybox.cleanup();
    for (auto &obj : scene) {
      obj.DS.cleanup();
    }
  }

  void localCleanup() {
    if (groundModel) {
      groundModel->cleanup();
      groundModel.reset();
    }
    Tground.cleanup();
    Tdungeon.cleanup();
    TenvMap.cleanup();
    for (auto &iconTexture : shopIconTextures) {
      iconTexture.cleanup();
    }
    for (auto &cachedTexture : textureCache) {
      cachedTexture.second->cleanup();
    }
    for (auto &cachedModel : modelCache) {
      cachedModel.second->cleanup();
    }
    DSLlocalTextured.cleanup();
    DSLglobal.cleanup();
    DSLskybox.cleanup();
    VDsimple.cleanup();
    VDskybox.cleanup();
    Psimple.destroy();
    Pskybox.destroy();
    PsunShadow.destroy();
    PspotShadow.destroy();
    RP.destroy();
    RPsun.destroy();
    for (int s = 0; s < MAX_SHADOW_SPOTS; s++) RPspot[s].destroy();
    shutdownImGuiContext();
  }

  static void populateCommandBufferAccess(VkCommandBuffer commandBuffer, int currentImage,
                                          void *Params) {
    DungeonTavernNPC *T = (DungeonTavernNPC *)Params;
    T->populateCommandBuffer(commandBuffer, currentImage);
  }

  void populateCommandBuffer(VkCommandBuffer commandBuffer, int currentImage) {
    // Fill the depth maps (sun + each active spotlight) before the main pass that
    // samples them. Each offscreen pass's dependencies make the freshly written
    // depth visible to that sampling.
    renderSunShadow(commandBuffer, currentImage);
    renderSpotShadows(commandBuffer, currentImage);

    RP.begin(commandBuffer, currentImage);

    Psimple.bind(commandBuffer);
    DSglobal.bind(commandBuffer, Psimple, 0, currentImage);

    for (auto &obj : scene) {
      // Draw the lit mesh only when the flame is actually burning and a lit
      // variant exists; otherwise the default (unlit) mesh. Both share the same
      // descriptor set, so only the bound vertex/index buffer changes.
      Model *mesh = (obj.lit && obj.hasLitVariant) ? obj.litModel : obj.model;
      mesh->bind(commandBuffer);
      obj.DS.bind(commandBuffer, Psimple, 1, currentImage);
      vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(mesh->indices.size()), 1, 0, 0, 0);
    }

    // Skybox last: it is pushed to the far plane and depth-tested LESS_OR_EQUAL,
    // so it fills only the background — i.e. it shows through the windows, the
    // doorway and the open gate, wherever no opaque geometry wrote depth.
    Pskybox.bind(commandBuffer);
    DSskybox.bind(commandBuffer, Pskybox, 0, currentImage);
    skyboxCube->bind(commandBuffer);
    vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(skyboxCube->indices.size()), 1, 0, 0, 0);

    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);

    RP.end(commandBuffer);
  }

  void updateUniformBuffer(uint32_t currentImage) {
    float deltaT = GameLogic();

    animTime += deltaT;
    sunState = dayNight.update(deltaT); // advance the continuous day/night cycle

    // Swing doors toward their target pose with an exponential ease (fast
    // start, gentle settle), snapping once the leaf is within half a degree.
    // A moving leaf does not collide — so a swinging door can never trap the
    // player mid-sweep; the collider comes back once it rests.
    for (auto &obj : scene) {
      if (!obj.isDoor) continue;
      float target = obj.doorOpen ? obj.openYaw : obj.closedYaw;
      float diff = target - obj.yaw;
      if (std::abs(diff) < 0.5f) {
        if (!obj.collidable) {
          obj.yaw = target;
          obj.collidable = true;
          refreshColliderWorld(obj);
        }
        continue;
      }
      obj.yaw += diff * std::min(1.0f, DOOR_SWING_RATE * deltaT);
      obj.collidable = false;
    }

    // NPCs turn to face the player while spoken to (dialogue or shop) or while
    // the player stands in their personal space. When not engaged, patrolling
    // NPCs walk their waypoint loop (facing the walk direction) and stationary
    // NPCs ease back to their authored resting facing.
    for (auto &obj : scene) {
      const bool isNpc = !obj.npcId.empty();
      if (!isNpc && obj.patrolPoints.empty()) continue;
      glm::vec3 toPlayer = cameraPos - obj.pos;
      toPlayer.y = 0.0f;
      float playerDist = glm::length(toPlayer);
      const bool talking =
          (dialogueSystem.isOpen() && dialogueSystem.activeNpcId() == obj.npcId) ||
          (shopSystem.isOpen() && obj.npcId == shopNpcId);
      if (talking || playerDist < NPC_PERSONAL_SPACE) {
        if (playerDist > 0.01f) {
          turnNpcToward(obj, glm::degrees(std::atan2(toPlayer.x, toPlayer.z)), deltaT);
        }
        continue;
      }
      // Not engaged: a stationary NPC returns to its authored facing.
      if (obj.patrolPoints.empty()) {
        turnNpcToward(obj, obj.restYaw, deltaT);
        continue;
      }
      if (obj.patrolWait > 0.0f) {
        obj.patrolWait -= deltaT;
        continue;
      }
      glm::vec3 toTarget = obj.patrolPoints[obj.patrolTarget] - obj.pos;
      toTarget.y = 0.0f;
      float dist = glm::length(toTarget);
      if (dist < 0.05f) {
        obj.patrolTarget = (obj.patrolTarget + 1) % (int)obj.patrolPoints.size();
        obj.patrolWait = obj.patrolPause;
        continue;
      }
      glm::vec3 dir = toTarget / dist;
      glm::vec3 step = dir * std::min(dist, obj.patrolSpeed * deltaT);
      obj.pos += step;
      obj.boundsCenter += step; // keep the shadow-cull sphere with the walker
      turnNpcToward(obj, glm::degrees(std::atan2(dir.x, dir.z)), deltaT);
    }

    GlobalUniformBufferObject gubo{};

    // Rebuild the GPU light list from scratch every frame: each lit flame adds
    // its light. Lighting or snuffing a candle just flips its `lit` flag, so
    // there are no stale slots to manage. Spotlight shadow slots are reassigned
    // here too, so lighting a torch makes it start casting and snuffing it frees
    // the slot for another.
    int activeLights = 0;
    activeSpotShadows = 0;
    for (int oi = 0; oi < (int)scene.size(); oi++) {
      auto &obj = scene[oi];
      if (!obj.isFlame) continue;

      if (!obj.lit) {
        obj.emissive = glm::vec3(0.0f); // snuffed: no glow, contributes no light
        continue;
      }

      // ---- Flame flicker ----
      // Wobble around the resting brightness with two sine waves at unrelated
      // frequencies (11 and 6.3): a single sine looks like a steady mechanical
      // pulse, but two detuned ones never line up the same way twice, so the eye
      // reads it as the random dance of a real flame. We only ever *dim* (factor
      // in [0.8, 1.0]) so the flame never flashes brighter than its design value.
      float wobble = 0.5f * std::sin(animTime * 11.0f + obj.flamePhase) +
                     0.5f * std::sin(animTime * 6.3f + obj.flamePhase * 2.1f);
      float factor = 0.9f + 0.1f * wobble; // [-1,1] -> [0.8,1.0]

      obj.emissive = obj.baseEmissive * factor;

      if (activeLights < MAX_LIGHTS) {
        Light L = obj.light;
        L.dir.w = obj.baseIntensity * factor;      // flickered intensity

        // A lit torch (spotlight) gets a 2D shadow map while a free slot remains.
        // cones.z carries that slot index to the fragment shader; -1 = this
        // spotlight casts no shadow this frame (slots exhausted).
        if (static_cast<int>(L.pos.w) == LIGHT_SPOT) {
          L.cones.z = -1.0f;
          if (activeSpotShadows < MAX_SHADOW_SPOTS) {
            int slot = activeSpotShadows++;
            glm::mat4 vp = computeSpotLightVP(L);
            spotShadows[slot].lightVP = vp;
            spotShadows[slot].emitterIndex = oi;
            gubo.spotLightVP[slot] = vp;
            L.cones.z = static_cast<float>(slot);
          }
        }

        gubo.lights[activeLights++] = L;

        // A spotlight alone leaves the torch's own wall and immediate surroundings
        // dark, but a real flame glows in every direction. So each torch also emits
        // a dim, short-range omnidirectional POINT "fill" co-located with the flame:
        // it lights the wall and nearby props (no cone, no shadow) while the
        // spotlight above does the directional pool that actually casts shadows.
        if (static_cast<int>(L.pos.w) == LIGHT_SPOT && activeLights < MAX_LIGHTS) {
          Light fill{};
          fill.pos   = glm::vec4(glm::vec3(L.pos), static_cast<float>(LIGHT_POINT));
          fill.dir   = glm::vec4(0.0f, 0.0f, 0.0f, L.dir.w * 0.35f); // dimmer than the spot
          fill.color = glm::vec4(glm::vec3(L.color), 1.8f);          // warm tint, ~1.8 m reach
          fill.cones = glm::vec4(0.0f);                              // point light: no cone/shadow
          gubo.lights[activeLights++] = fill;
        }
      }
    }

    // Orthographic view-projection from the sun, framed on the whole scene, used
    // both to render the depth map and to look it up in the main shader. The Y
    // axis is flipped in the ortho (top/bottom swapped) so the sampled UVs line
    // up with how the depth map was rasterised. Computed every frame so the
    // depth pass is always valid, even when the result is not applied at night.
    {
      glm::vec3 sDir = glm::normalize(sunState.sunDir);
      glm::vec3 up = (std::abs(sDir.y) > 0.99f) ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
      float dist = 2.0f * sceneRadius;
      glm::mat4 lightView = glm::lookAt(sceneCenter - sDir * dist, sceneCenter, up);
      glm::mat4 lightProj = glm::ortho(-sceneRadius, sceneRadius, -sceneRadius, sceneRadius,
                                       0.0f, 2.0f * dist);
      // Vulkan's clip-space Y is flipped relative to OpenGL. As with the main
      // camera, apply the flip as a scale on the projection (the course's
      // convention) rather than swapping the ortho's top/bottom.
      lightProj = glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, -1.0f, 1.0f)) * lightProj;
      sunLightVP = lightProj * lightView;
    }
    gubo.sunLightVP = sunLightVP;

    // The sun/moon: a single directional light driven by the day/night cycle.
    // Direction, colour and intensity all come from the cycle, so the scene
    // brightens at dawn, warms at dusk and dims to cool moonlight at night.
    // range (color.a) = 0 means infinite (a directional light never attenuates).
    // cones.z >= 0 tells the shader to clip this light with the sun shadow map;
    // we only enable it while the sun is above the horizon (its shadow would
    // come from below at night), but it stays a lit (dim) moon either way.
    if (activeLights < MAX_LIGHTS && sunState.intensity > 0.001f) {
      // Continuous shadow strength (0..1) instead of an on/off flag: it ramps up
      // as the sun clears the horizon and back down as it sets, so the shadowing
      // never switches abruptly. Paired with the intensity fade, this removes the
      // bright "flash" that the old boolean toggle produced at sunset.
      const float shadowStrength = glm::clamp(sunState.toSun.y / 0.12f, 0.0f, 1.0f);
      Light sun{};
      sun.pos = glm::vec4(0.0f, 0.0f, 0.0f, (float)LIGHT_DIRECTIONAL);
      sun.dir = glm::vec4(glm::normalize(sunState.sunDir), sunState.intensity);
      sun.color = glm::vec4(sunState.color, 0.0f);
      sun.cones = glm::vec4(0.0f, 0.0f, shadowStrength, 0.0f);
      gubo.lights[activeLights++] = sun;
    }

    gubo.eyePos = glm::vec4(cameraPos, (float)activeLights);

    DSglobal.map(currentImage, &gubo, 0);

    // Skybox: rotation-only view-projection (so it stays centred on the camera),
    // plus the sun direction/day factor/colour that tint it across the cycle.
    SkyboxUBO skyUbo{};
    skyUbo.mvpMat = SkyViewPrj;
    skyUbo.sunDirDay = glm::vec4(sunState.toSun, sunState.dayFactor);
    skyUbo.sunColor = glm::vec4(sunState.color, 1.0f);
    DSskybox.map(currentImage, &skyUbo, 0);

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

    // While the splash menu is up it owns all input: the player stands still,
    // nothing is interactable, and only the menu's Start/Quit requests matter.
    // The scene behind keeps animating as a live backdrop.
    if (splashScreen.isActive()) {
      interactionTarget.clear();
      targetNpcId.clear();
      if (splashScreen.consumeStartRequest()) {
        splashScreen.setActive(false);
        cursorLocked = true;
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        firstPersonController.resetMouseTracking();
      }
      if (splashScreen.consumeQuitRequest()) {
        glfwSetWindowShouldClose(window, GL_TRUE);
      }
      glm::mat4 Prj = glm::perspective(FOVy, Ar, nearPlane, farPlane);
      Prj[1][1] *= -1;
      glm::mat4 View = glm::lookAt(cameraPos, cameraPos + camForward, glm::vec3(0, 1, 0));
      ViewPrj = Prj * View;
      SkyViewPrj = Prj * glm::mat4(glm::mat3(View)); // drop translation: sky tracks the camera
      return deltaT;
    }

    // Tab releases / re-captures the mouse cursor (e.g. to click the shop UI).
    static bool cursorKeyPrev = false;
    bool cursorKeyNow = glfwGetKey(window, input_bindings::ToggleCursor) == GLFW_PRESS;
    if (cursorKeyNow && !cursorKeyPrev) {
      cursorLocked = !cursorLocked;
      glfwSetInputMode(window, GLFW_CURSOR,
                       cursorLocked ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
      firstPersonController.resetMouseTracking();
    }
    cursorKeyPrev = cursorKeyNow;

    double mouseX, mouseY;
    glfwGetCursorPos(window, &mouseX, &mouseY);
    bool jumpPressed = glfwGetKey(window, input_bindings::Jump) == GLFW_PRESS;
    FirstPersonController::State playerState =
        firstPersonController.update(deltaT, m, mouseX, mouseY, cursorLocked, jumpPressed, scene);
    cameraPos = playerState.position;
    camForward = playerState.forward;

    // Interaction detection — closest interactable in view: a flame (toggle) or
    // the NPC (talk). We approximate "looking at it" cheaply (close + roughly
    // aligned with the view via a dot test) instead of a real ray-vs-mesh test.
    // We remember the index so E acts on that exact one.
    interactionTarget.clear();
    targetNpcId.clear();
    int targetIdx = -1;
    float bestDist = INTERACT_DIST;
    glm::vec3 lookH = glm::normalize(glm::vec3(camForward.x, 0.0f, camForward.z));
    for (int i = 0; i < (int)scene.size(); i++) {
      const auto &o = scene[i];
      if (!o.isFlame && !o.isDoor && o.tag != "npc") continue;
      glm::vec3 aimPos = o.pos;
      if (o.isDoor) {
        // The door's pos is its hinge; aim at the middle of the leaf instead.
        float yawRad = glm::radians(o.yaw);
        aimPos += glm::vec3(std::cos(yawRad), 0.0f, -std::sin(yawRad));
      }
      glm::vec3 toObj = aimPos - cameraPos;
      toObj.y = 0.0f;
      float dist = glm::length(toObj);
      if (dist < 0.01f || dist > INTERACT_DIST) continue;
      float dot = glm::dot(glm::normalize(toObj), lookH);
      if (dot > INTERACT_DOT && dist < bestDist) {
        bestDist = dist;
        targetIdx = i;
      }
    }
    if (targetIdx >= 0) {
      // Flames/doors carry their own action prompt (feedback on what E will do);
      // the NPC uses its tag so the dialogue system maps it to "[E] Talk".
      const auto &o = scene[targetIdx];
      if (o.isFlame) {
        interactionTarget = input_bindings::interactPrompt(o.lit ? "Extinguish" : "Light");
      } else if (o.isDoor) {
        interactionTarget = input_bindings::interactPrompt(o.doorOpen ? "Close" : "Open");
      } else {
        interactionTarget = o.tag; // "npc"
        targetNpcId = o.npcId;
      }
    }

    // E acts on whatever we're looking at: toggle a flame or door, or let the
    // dialogue system handle the NPC. Edge-triggered so holding E doesn't
    // repeat. While the shop is open, E only closes the shop.
    static bool ePrev = false;
    bool eNow = glfwGetKey(window, input_bindings::Interact) == GLFW_PRESS;
    bool ePressed = eNow && !ePrev;
    ePrev = eNow;
    bool eForWorld = ePressed && !shopSystem.isOpen();
    if (ePressed && shopSystem.isOpen()) {
      setShopOpen(false);
    } else if (eForWorld && targetIdx >= 0) {
      if (scene[targetIdx].isFlame) {
        scene[targetIdx].lit = !scene[targetIdx].lit;
      } else if (scene[targetIdx].isDoor) {
        tryToggleDoor(scene[targetIdx]);
      }
    }

    // Dialogue: keys 1/2/3 pick the current node's choices (edge-triggered).
    static bool numPrev[3] = {false, false, false};
    std::array<bool, 3> numPressed{};
    for (int i = 0; i < 3; i++) {
      bool now = glfwGetKey(window, input_bindings::DialogueChoices[i]) == GLFW_PRESS;
      numPressed[i] = now && !numPrev[i];
      numPrev[i] = now;
    }
    dialogueSystem.update(targetNpcId, eForWorld, numPressed);

    // Esc ends an open conversation (no effect when no dialogue is open).
    static bool leaveDialoguePrev = false;
    bool leaveDialogueNow = glfwGetKey(window, input_bindings::LeaveDialogue) == GLFW_PRESS;
    if (leaveDialogueNow && !leaveDialoguePrev) {
      dialogueSystem.leave();
    }
    leaveDialoguePrev = leaveDialogueNow;

    if (dialogueSystem.consumeShopRequest()) {
      shopNpcId = targetNpcId; // remember whose shop this is, so they idle
      setShopOpen(true);
    }
    if (shopSystem.consumeCloseRequest()) {
      setShopOpen(false);
    }

    // View-Projection
    glm::mat4 Prj = glm::perspective(FOVy, Ar, nearPlane, farPlane);
    Prj[1][1] *= -1;
    glm::mat4 View = glm::lookAt(cameraPos, cameraPos + camForward, glm::vec3(0, 1, 0));
    ViewPrj = Prj * View;
    SkyViewPrj = Prj * glm::mat4(glm::mat3(View)); // drop translation: sky tracks the camera

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
