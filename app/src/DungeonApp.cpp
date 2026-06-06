#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <json.hpp>

#define STARTER_IMPLEMENTATION
#include "modules/Starter.hpp"
#include "modules/Colliders.hpp"

#include "FirstPersonController.hpp"

#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"
#include "imgui.h"

// Light types — must match #define values in BlinnPhong.frag
constexpr int LIGHT_POINT       = 0;
constexpr int LIGHT_SPOT        = 1;
constexpr int LIGHT_DIRECTIONAL = 2;
constexpr int MAX_LIGHTS        = 12; // fixed engine budget, independent of scene content

// ---- Shadow cube maps ----
// A point light shines in every direction, so to capture what it can "see" we
// render the scene into the six faces of a cube map centred on the light, then
// sample that cube in the main shader to test whether a point is blocked. This
// is the number of lights that cast shadows (the first lit candles/torches).
// Must match MAX_SHADOW_CUBES in BlinnPhong.frag.
constexpr int NUM_SHADOW_CUBES  = MAX_LIGHTS;
constexpr int SHADOW_RES        = 1024;  // per-face resolution
constexpr float SHADOW_NEAR     = 0.05f; // near plane of each face's frustum

struct Light {
  alignas(16) glm::vec4 pos;    // xyz = world position,  w = type (LIGHT_*)
  alignas(16) glm::vec4 dir;    // xyz = direction,        w = intensity
  alignas(16) glm::vec4 color;  // rgb = color,            a = range (0 = infinite)
  alignas(16) glm::vec4 cones;  // x = cos(inner), y = cos(outer); z = shadow cube
                                 // index (-1 = no shadow)
};

// Pushed per cube face during the shadow pass (80 bytes, well under the 128-byte
// guaranteed push-constant budget). Layout must match PushConstants in the
// ShadowCube shaders.
struct ShadowPushConstants {
  glm::mat4 lightVP;
  glm::vec4 lightPos;
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
  float specExp = 32.0f;          // Blinn-Phong specular exponent (material shininess)
  glm::vec3 emissive{0.0f};       // self-illumination (glows regardless of lights)

  // ---- Flame state (only meaningful for candles/torches) ----
  // We keep each flame's light *with the object that owns it* instead of in a
  // separate global list. That way "is this candle lit?" is just a bool on the
  // object, and lighting/snuffing one at runtime never disturbs the others —
  // there are no shared indices to keep in sync. Each frame we rebuild the GPU
  // light array from whichever flames happen to be lit.
  bool  isFlame = false;          // can this object emit light? (candle/torch)
  bool  lit = false;              // is it currently burning?
  Light light{};                  // the light it emits while lit
  float baseIntensity = 0.0f;     // resting brightness, before flicker
  glm::vec3 baseEmissive{0.0f};   // resting glow, before flicker
  float flamePhase = 0.0f;        // per-flame offset so they don't flicker in sync

  // Some flames have a separate "lit" mesh (e.g. torch_lit shows a flame, torch
  // doesn't). When one exists we keep both meshes loaded and draw whichever
  // matches `lit`. `model` always holds the unlit mesh; `litModel` the lit one.
  // They share the same texture and uniforms, so swapping is just a matter of
  // binding a different vertex/index buffer at draw time. Flames without a lit
  // variant (e.g. candle_triple) keep hasLitVariant=false and never swap mesh.
  Model litModel;
  bool  hasLitVariant = false;

  int shadowCubeIndex = -1;       // index into shadowCubes if this flame casts shadows
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
  float animTime = 0.0f;            // seconds since start, drives the flicker

  // ---- Shadow cube map resources ----
  // One cube map per shadow-casting light. The colour image is a 6-layer,
  // cube-compatible R32_SFLOAT image storing distance-to-light. We keep a 2D
  // view per face (to render into each face one at a time) and a cube view (to
  // sample the finished result in the main shader). All cubes share one small
  // depth buffer, since the six faces are rendered one after another.
  struct ShadowCube {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    VkImageView cubeView = VK_NULL_HANDLE;     // sampled in the main shader
    VkImageView faceViews[6] = {};             // render targets, one per face
    VkFramebuffer framebuffers[6] = {};
    int objectIndex = -1;                      // which scene flame this follows
  };
  ShadowCube shadowCubes[NUM_SHADOW_CUBES];
  VkImage shadowDepthImage = VK_NULL_HANDLE;
  VkDeviceMemory shadowDepthMem = VK_NULL_HANDLE;
  VkImageView shadowDepthView = VK_NULL_HANDLE;
  VkRenderPass shadowRenderPass = VK_NULL_HANDLE;
  RenderPass shadowRPShim;          // minimal shim so Pipeline::create can read sizes/handle
  AttachmentProperties shadowShimProps{};
  Pipeline PshadowCube;
  TextureSampler shadowSampler;

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
        const char *prompt = interactionTarget.c_str();
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

  // ---------------------------------------------------------------------------
  // Shadow cube maps
  // ---------------------------------------------------------------------------
  // The framework's RenderPass/FrameBufferAttachment helpers only ever create
  // single-layer 2D images, so a cube map (6 layers) is built here with raw
  // Vulkan, reusing BaseProject's public createImage/createImageView helpers.
  // Everything is sized to SHADOW_RES and lives for the whole program (it does
  // not depend on the swap chain), so it is created once in localInit and torn
  // down in localCleanup.

  // The six view*projection matrices that point a 90-degree-FOV camera down each
  // cube axis from the light. This is the canonical Vulkan omnidirectional-shadow
  // recipe: a plain perspective (no Y flip) times an explicit per-face rotation,
  // with the light moved to the origin by a separate translation. The rotations
  // (note the 180-degree roll on most faces) are exactly what makes the rendered
  // image line up with how a samplerCube reads back a direction in Vulkan —
  // doing it with lookAt + a hand-rolled Y flip is the classic way to get subtly
  // wrong faces and light that leaks through occluders.
  void buildCubeFaceMatrices(glm::vec3 p, float farPlane, glm::mat4 out[6]) {
    glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, SHADOW_NEAR, farPlane);
    glm::mat4 toLight = glm::translate(glm::mat4(1.0f), -p); // put the light at the origin

    glm::mat4 rot[6];
    rot[0] = glm::rotate(glm::rotate(glm::mat4(1), glm::radians( 90.0f), glm::vec3(0,1,0)),
                         glm::radians(180.0f), glm::vec3(1,0,0)); // +X
    rot[1] = glm::rotate(glm::rotate(glm::mat4(1), glm::radians(-90.0f), glm::vec3(0,1,0)),
                         glm::radians(180.0f), glm::vec3(1,0,0)); // -X
    rot[2] = glm::rotate(glm::mat4(1), glm::radians(-90.0f), glm::vec3(1,0,0)); // +Y
    rot[3] = glm::rotate(glm::mat4(1), glm::radians( 90.0f), glm::vec3(1,0,0)); // -Y
    rot[4] = glm::rotate(glm::mat4(1), glm::radians(180.0f), glm::vec3(1,0,0)); // +Z
    rot[5] = glm::rotate(glm::mat4(1), glm::radians(180.0f), glm::vec3(0,0,1)); // -Z

    for (int f = 0; f < 6; f++)
      out[f] = proj * rot[f] * toLight;
  }

  void createShadowResources() {
    // Sampler used to read the finished cube maps. Linear gives slightly softer
    // shadow edges; clamp-to-edge is irrelevant for a cube (faces are seamless)
    // but harmless. No anisotropy/mips needed for a distance field.
    shadowSampler.init(this, VK_FILTER_LINEAR, VK_FILTER_LINEAR,
                       VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                       VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_MIPMAP_MODE_NEAREST,
                       VK_FALSE, 1.0f, 1.0f);

    // One depth buffer shared by every face: the six faces are rendered one
    // after another and each clears it, so they never need separate copies.
    createImage(SHADOW_RES, SHADOW_RES, 1, 1, VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_D32_SFLOAT,
                VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, 0,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, shadowDepthImage, shadowDepthMem);
    shadowDepthView = createImageView(shadowDepthImage, VK_FORMAT_D32_SFLOAT,
                                      VK_IMAGE_ASPECT_DEPTH_BIT, 1, VK_IMAGE_VIEW_TYPE_2D, 1);

    createShadowRenderPass();

    for (int c = 0; c < NUM_SHADOW_CUBES; c++) {
      ShadowCube &sc = shadowCubes[c];

      // Cube-compatible colour image: 6 array layers, one per face, storing the
      // distance-to-light written by the ShadowCube fragment shader.
      createImage(SHADOW_RES, SHADOW_RES, 1, 6, VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_R32_SFLOAT,
                  VK_IMAGE_TILING_OPTIMAL,
                  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                  VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                  sc.image, sc.mem);

      // Cube view for sampling in the main shader.
      VkImageViewCreateInfo cvi{};
      cvi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
      cvi.image = sc.image;
      cvi.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
      cvi.format = VK_FORMAT_R32_SFLOAT;
      cvi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6};
      if (vkCreateImageView(device, &cvi, nullptr, &sc.cubeView) != VK_SUCCESS)
        throw std::runtime_error("failed to create shadow cube view");

      // A 2D view per face (targeting one array layer) plus a framebuffer that
      // pairs it with the shared depth buffer. createImageView() can't offset
      // baseArrayLayer, so the per-face views are made by hand here.
      for (int f = 0; f < 6; f++) {
        VkImageViewCreateInfo fvi{};
        fvi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        fvi.image = sc.image;
        fvi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        fvi.format = VK_FORMAT_R32_SFLOAT;
        fvi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, (uint32_t)f, 1};
        if (vkCreateImageView(device, &fvi, nullptr, &sc.faceViews[f]) != VK_SUCCESS)
          throw std::runtime_error("failed to create shadow face view");

        VkImageView att[2] = {sc.faceViews[f], shadowDepthView};
        VkFramebufferCreateInfo fbi{};
        fbi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbi.renderPass = shadowRenderPass;
        fbi.attachmentCount = 2;
        fbi.pAttachments = att;
        fbi.width = SHADOW_RES;
        fbi.height = SHADOW_RES;
        fbi.layers = 1;
        if (vkCreateFramebuffer(device, &fbi, nullptr, &sc.framebuffers[f]) != VK_SUCCESS)
          throw std::runtime_error("failed to create shadow framebuffer");
      }
    }

    // Pipeline for the shadow pass. Pipeline::create() wants a framework
    // RenderPass, so we hand it a minimal shim exposing just the fields it reads
    // (size, the VkRenderPass handle, and the single colour attachment's sample
    // count). Cull NONE because dungeon walls may be single-sided planes — front
    // or back culling could drop them from the depth map and lose their shadow.
    shadowShimProps.samples = VK_SAMPLE_COUNT_1_BIT;
    shadowRPShim.BP = this;
    shadowRPShim.width = SHADOW_RES;
    shadowRPShim.height = SHADOW_RES;
    shadowRPShim.renderPass = shadowRenderPass;
    shadowRPShim.colorAttchementsCount = 1;
    shadowRPShim.firstColorAttIdx = 0;
    shadowRPShim.attachments.resize(1);
    shadowRPShim.attachments[0].properties = &shadowShimProps;

    PshadowCube.init(this, &VDsimple, "shaders/mesh/ShadowCube.vert.spv",
                     "shaders/mesh/ShadowCube.frag.spv", {&DSLlocalTextured},
                     {{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(ShadowPushConstants)}});
    PshadowCube.setCullMode(VK_CULL_MODE_NONE);
    PshadowCube.create(&shadowRPShim);

    // Which flame each cube follows is decided every frame (the cubes track the
    // currently-lit candles), so there is no fixed assignment here.

    // Put every cube into SHADER_READ_ONLY_OPTIMAL once, up front. The descriptor
    // declares the whole samplerCube array in that layout and the validation
    // layer checks ALL array elements at draw time, even ones the shader won't
    // index. A cube whose candle is unlit is never rendered, so without this it
    // would sit in UNDEFINED and trip a validation error. (The framework's
    // transitionImageLayout doesn't cover this transition, so we barrier directly.)
    VkCommandBuffer tcb = beginSingleTimeCommands();
    for (int c = 0; c < NUM_SHADOW_CUBES; c++) {
      VkImageMemoryBarrier b{};
      b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
      b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      b.image = shadowCubes[c].image;
      b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6};
      b.srcAccessMask = 0;
      b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
      vkCmdPipelineBarrier(tcb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    }
    endSingleTimeCommands(tcb);
  }

  void createShadowRenderPass() {
    // Colour attachment = the distance map (R32F). finalLayout READ_ONLY so the
    // main pass can sample it straight after. Depth is throwaway (DONT_CARE).
    VkAttachmentDescription color{};
    color.format = VK_FORMAT_R32_SFLOAT;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentDescription depth{};
    depth.format = VK_FORMAT_D32_SFLOAT;
    depth.samples = VK_SAMPLE_COUNT_1_BIT;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    // Make the previous frame's sampling finish before we overwrite a face, and
    // make this frame's writes visible to the main pass that samples the cube.
    VkSubpassDependency deps[2]{};
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass = 0;
    deps[0].srcStageMask =
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[0].dstStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[0].dstAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    deps[1].srcSubpass = 0;
    deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[1].srcAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    deps[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    VkAttachmentDescription atts[2] = {color, depth};
    VkRenderPassCreateInfo rpi{};
    rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpi.attachmentCount = 2;
    rpi.pAttachments = atts;
    rpi.subpassCount = 1;
    rpi.pSubpasses = &subpass;
    rpi.dependencyCount = 2;
    rpi.pDependencies = deps;
    if (vkCreateRenderPass(device, &rpi, nullptr, &shadowRenderPass) != VK_SUCCESS)
      throw std::runtime_error("failed to create shadow render pass");
  }

  // Record the six-face depth render for every shadow-casting light. Runs at the
  // very start of the frame's command buffer, before the main pass, so the cube
  // maps are ready to sample.
  void renderShadowMaps(VkCommandBuffer cb, int currentImage) {
    for (int c = 0; c < NUM_SHADOW_CUBES; c++) {
      ShadowCube &sc = shadowCubes[c];
      if (sc.objectIndex < 0) continue;

      glm::vec3 lpos = glm::vec3(scene[sc.objectIndex].light.pos);
      float range = scene[sc.objectIndex].light.color.a;
      float farPlane = (range > 0.0f) ? range : 20.0f;

      glm::mat4 faces[6];
      buildCubeFaceMatrices(lpos, farPlane, faces);

      for (int f = 0; f < 6; f++) {
        VkClearValue clears[2];
        // Clear colour = "very far": texels never written stay at a huge
        // distance, so empty directions read as "no occluder" (never in shadow).
        clears[0].color = {{1e9f, 0.0f, 0.0f, 0.0f}};
        clears[1].depthStencil = {1.0f, 0};

        VkRenderPassBeginInfo rpb{};
        rpb.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpb.renderPass = shadowRenderPass;
        rpb.framebuffer = sc.framebuffers[f];
        rpb.renderArea.offset = {0, 0};
        rpb.renderArea.extent = {(uint32_t)SHADOW_RES, (uint32_t)SHADOW_RES};
        rpb.clearValueCount = 2;
        rpb.pClearValues = clears;
        vkCmdBeginRenderPass(cb, &rpb, VK_SUBPASS_CONTENTS_INLINE);

        PshadowCube.bind(cb);

        ShadowPushConstants pc{faces[f], glm::vec4(lpos, 1.0f)};
        vkCmdPushConstants(cb, PshadowCube.pipelineLayout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(pc), &pc);

        for (int o = 0; o < (int)scene.size(); o++) {
          // Skip the candle that owns this light: the flame sits inside its own
          // wax/holder, so letting it write to the depth map makes the light
          // occlude itself in every direction (you just get a puddle of light at
          // the base). An emitter shouldn't cast its own shadow.
          if (o == sc.objectIndex) continue;

          SceneObject &obj = scene[o];
          Model &mesh = (obj.lit && obj.hasLitVariant) ? obj.litModel : obj.model;
          obj.DS.bind(cb, PshadowCube, 0, currentImage); // set 0 = object UBO (for mMat)
          mesh.bind(cb);
          vkCmdDrawIndexed(cb, static_cast<uint32_t>(mesh.indices.size()), 1, 0, 0, 0);
        }

        vkCmdEndRenderPass(cb);
      }
    }
  }

  void destroyShadowResources() {
    PshadowCube.cleanup();
    PshadowCube.destroy();
    shadowSampler.cleanup();
    for (int c = 0; c < NUM_SHADOW_CUBES; c++) {
      ShadowCube &sc = shadowCubes[c];
      for (int f = 0; f < 6; f++) {
        if (sc.framebuffers[f]) vkDestroyFramebuffer(device, sc.framebuffers[f], nullptr);
        if (sc.faceViews[f]) vkDestroyImageView(device, sc.faceViews[f], nullptr);
      }
      if (sc.cubeView) vkDestroyImageView(device, sc.cubeView, nullptr);
      if (sc.image) vkDestroyImage(device, sc.image, nullptr);
      if (sc.mem) vkFreeMemory(device, sc.mem, nullptr);
    }
    if (shadowDepthView) vkDestroyImageView(device, shadowDepthView, nullptr);
    if (shadowDepthImage) vkDestroyImage(device, shadowDepthImage, nullptr);
    if (shadowDepthMem) vkFreeMemory(device, shadowDepthMem, nullptr);
    if (shadowRenderPass) vkDestroyRenderPass(device, shadowRenderPass, nullptr);
  }

  void localInit() {
    // PERF: the framework picks the GPU's MAXIMUM MSAA level (often 8x) and the
    // pipeline forces per-sample shading, so the fragment shader runs once per
    // sample — at 8x that is 8x the per-pixel cost, which on its own can drag the
    // framerate down regardless of how light the geometry is. msaaSamples is a
    // public framework field, so we can cap it here (before the render pass and
    // pipelines are built) without editing the framework itself. 4x still looks
    // clean; drop to 2x (or VK_SAMPLE_COUNT_1_BIT) if more speed is needed.
    if (msaaSamples > VK_SAMPLE_COUNT_2_BIT) {
      msaaSamples = VK_SAMPLE_COUNT_2_BIT;
    }

    DSLlocalTextured.init(this,
                          {{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_ALL_GRAPHICS,
                            sizeof(UniformBufferObject), 1},
                           {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                            VK_SHADER_STAGE_FRAGMENT_BIT, 0, 1}});

    DSLglobal.init(this, {{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_ALL_GRAPHICS,
                           sizeof(GlobalUniformBufferObject), 1},
                          // binding 1: the shadow cube maps, sampled in the fragment shader
                          {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, NUM_SHADOW_CUBES}});

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
      const std::string modelPath = obj["model"].get<std::string>();
      scene[i].pos = glm::vec3(p[0].get<float>(), p[1].get<float>(), p[2].get<float>());
      scene[i].yaw = obj.value("yaw", 0.0f);
      scene[i].tag = obj.value("tag", "");
      scene[i].specExp = obj.value("specExp", 32.0f);
      if (obj.contains("emissive")) {
        const auto &e = obj["emissive"];
        scene[i].emissive = glm::vec3(e[0].get<float>(), e[1].get<float>(), e[2].get<float>());
      }
      // Decide up front whether this is a flame (candle/torch), because that
      // changes how we load its mesh: a flame may come as a paired lit/unlit
      // model and we want both loaded so we can swap them when it's toggled.
      const auto &t = scene[i].tag;
      bool isTorch  = modelPath.find("torch")  != std::string::npos;
      bool isCandle = modelPath.find("candle") != std::string::npos;
      bool isFlame  = (t == "light_source") && (isTorch || isCandle);

      if (isFlame) {
        // Work out both mesh names from whichever variant the scene listed.
        // unlit = name with "_lit" stripped; lit = "_lit" inserted before .gltf.
        std::string unlitPath = modelPath;
        size_t litPos = unlitPath.find("_lit");
        if (litPos != std::string::npos) unlitPath.erase(litPos, 4);
        std::string litPath = unlitPath;
        size_t ext = litPath.rfind(".gltf");
        if (ext != std::string::npos) litPath.insert(ext, "_lit");

        // Only swap meshes if the pair actually exists on disk (candle_triple,
        // for instance, has no _lit version) — otherwise just keep the one mesh.
        scene[i].hasLitVariant = std::filesystem::exists(litPath) &&
                                 std::filesystem::exists(unlitPath);
        if (scene[i].hasLitVariant) {
          scene[i].model.init(this, &VDsimple, unlitPath.c_str(), GLTF);
          scene[i].litModel.init(this, &VDsimple, litPath.c_str(), GLTF);
        } else {
          scene[i].model.init(this, &VDsimple, modelPath.c_str(), GLTF);
        }
      } else {
        scene[i].model.init(this, &VDsimple, modelPath.c_str(), GLTF);
      }

      scene[i].collidable = (t == "wall" || t == "structure" || t == "furniture" || t == "prop");
      if (scene[i].collidable) {
        scene[i].collider.fitOOBB(&scene[i].model);
        glm::mat4 wm = glm::translate(glm::mat4(1), scene[i].pos) *
                        glm::rotate(glm::mat4(1), glm::radians(scene[i].yaw), glm::vec3(0, 1, 0));
        scene[i].collider.setWorldMatrix(wm);
      }

      // Set up the flame's light. It starts burning if the scene used the "_lit"
      // variant; otherwise it starts dark and the player lights it with E. The
      // light is identical either way, so toggling is just flipping `lit`.
      if (isFlame) {
        scene[i].isFlame = true;
        Light L{};
        if (isTorch) {
          L.pos   = glm::vec4(scene[i].pos + glm::vec3(0, 0.3f, 0), LIGHT_POINT);
          L.dir   = glm::vec4(0, 0, 0, 1.0f);             // intensity
          L.color = glm::vec4(1.0f, 0.28f, 0.05f, 3.3f);  // saturated orange, range 3.3 m
        } else { // candle
          // Lift the light to roughly flame height (top of the candle) instead
          // of the candle's base, so the glow radiates from where the fire is.
          L.pos   = glm::vec4(scene[i].pos + glm::vec3(0, 0.35f, 0), LIGHT_POINT);
          L.dir   = glm::vec4(0, 0, 0, 0.6f);
          L.color = glm::vec4(1.0f, 0.42f, 0.1f, 1.8f);   // amber, range 1.8 m
        }
        L.cones = glm::vec4(0.0f, 0.0f, -1.0f, 0.0f); // z = -1: no shadow map by default

        scene[i].light         = L;
        scene[i].baseIntensity = L.dir.w;           // intensity lives in dir.w
        scene[i].baseEmissive  = scene[i].emissive; // glow shown while lit
        // Phase from the object index (cheap pseudo-random) so candles side by
        // side waver out of step instead of pulsing like one big light.
        scene[i].flamePhase    = (float)i * 1.37f;
        scene[i].lit           = (modelPath.find("_lit") != std::string::npos);
      }
    }

    const int objCount = static_cast<int>(scene.size());
    DPSZs.uniformBlocksInPool = 1 + objCount;
    // +NUM_SHADOW_CUBES: the global set now also holds the shadow cube samplers.
    DPSZs.texturesInPool = objCount + NUM_SHADOW_CUBES;
    DPSZs.setsInPool = 1 + objCount;

    // Build all the cube-map machinery now that the scene (and its flames) exist,
    // so we can pick which lights cast shadows.
    createShadowResources();

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

    // The global set's binding 1 is the array of shadow cubes; hand it one
    // {sampler, cube view, layout} per cube, in index order.
    std::vector<VkDescriptorImageInfo> shadowInfos;
    shadowInfos.reserve(NUM_SHADOW_CUBES);
    for (int c = 0; c < NUM_SHADOW_CUBES; c++) {
      shadowInfos.push_back({shadowSampler.getSampler(), shadowCubes[c].cubeView,
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
    }
    DSglobal.init(this, &DSLglobal, shadowInfos);
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
    destroyShadowResources();
    Tdungeon.cleanup();
    for (auto &obj : scene) {
      obj.model.cleanup();
      if (obj.hasLitVariant) {
        obj.litModel.cleanup();
      }
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
    // Fill the shadow cube maps from each casting light's point of view, before
    // the main pass that samples them. The render pass dependencies make the
    // freshly written depth visible to that sampling.
    renderShadowMaps(commandBuffer, currentImage);

    RP.begin(commandBuffer, currentImage);

    Psimple.bind(commandBuffer);
    DSglobal.bind(commandBuffer, Psimple, 0, currentImage);

    for (auto &obj : scene) {
      // Draw the lit mesh only when the flame is actually burning and a lit
      // variant exists; otherwise the default (unlit) mesh. Both share the same
      // descriptor set, so only the bound vertex/index buffer changes.
      Model &mesh = (obj.lit && obj.hasLitVariant) ? obj.litModel : obj.model;
      mesh.bind(commandBuffer);
      obj.DS.bind(commandBuffer, Psimple, 1, currentImage);
      vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(mesh.indices.size()), 1, 0, 0, 0);
    }

    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);

    RP.end(commandBuffer);
  }

  void updateUniformBuffer(uint32_t currentImage) {
    if (glfwGetKey(window, GLFW_KEY_ESCAPE)) {
      glfwSetWindowShouldClose(window, GL_TRUE);
    }

    float deltaT = GameLogic();

    animTime += deltaT;

    // Hand the shadow cubes to the currently-lit flames (the first NUM_SHADOW_CUBES
    // of them). Reassigned every frame, so lighting a candle makes it start
    // casting a shadow and snuffing it frees the cube for another — only lit
    // candles ever cast. Flames beyond the cube budget simply don't cast.
    for (auto &sc : shadowCubes) sc.objectIndex = -1;
    int cubesUsed = 0;
    for (int i = 0; i < (int)scene.size(); i++) {
      scene[i].shadowCubeIndex = -1;
      if (scene[i].isFlame && scene[i].lit && cubesUsed < NUM_SHADOW_CUBES) {
        shadowCubes[cubesUsed].objectIndex = i;
        scene[i].shadowCubeIndex = cubesUsed;
        cubesUsed++;
      }
    }

    GlobalUniformBufferObject gubo{};

    // Rebuild the GPU light list from scratch every frame: each lit flame adds
    // its light. Lighting or snuffing a candle just flips its `lit` flag, so
    // there are no stale slots to manage.
    int activeLights = 0;
    for (auto &obj : scene) {
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
        L.cones.z = (float)obj.shadowCubeIndex;    // -1, or which cube the shader samples
        gubo.lights[activeLights++] = L;
      }
    }
    gubo.eyePos = glm::vec4(cameraPos, (float)activeLights);

    DSglobal.map(currentImage, &gubo, 0);

    for (auto &obj : scene) {
      UniformBufferObject ubo{};
      ubo.mMat = glm::translate(glm::mat4(1), obj.pos) *
                 glm::rotate(glm::mat4(1), glm::radians(obj.yaw), glm::vec3(0, 1, 0));
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
    FirstPersonController::State playerState =
        firstPersonController.update(deltaT, m, mouseX, mouseY, cursorLocked, scene);
    cameraPos = playerState.position;
    camForward = playerState.forward;

    // Interaction detection — find the flame we're looking at. We approximate
    // "looking at it" cheaply: the object must be close, and the (horizontal)
    // direction to it must roughly line up with where we're facing (dot test),
    // rather than doing a real ray-vs-mesh test. Good enough for picking out a
    // candle on a table. We remember the index so E can act on that exact one.
    interactionTarget.clear();
    int targetFlame = -1;
    float bestDist = INTERACT_DIST;
    glm::vec3 lookH = glm::normalize(glm::vec3(camForward.x, 0.0f, camForward.z));
    for (int i = 0; i < (int)scene.size(); i++) {
      if (!scene[i].isFlame) continue;
      glm::vec3 toObj = scene[i].pos - cameraPos;
      toObj.y = 0.0f;
      float dist = glm::length(toObj);
      if (dist < 0.01f || dist > INTERACT_DIST) continue;
      float dot = glm::dot(glm::normalize(toObj), lookH);
      if (dot > INTERACT_DOT && dist < bestDist) {
        bestDist = dist;
        targetFlame = i;
      }
    }
    if (targetFlame >= 0) {
      // The prompt doubles as feedback on what the press will do.
      interactionTarget = scene[targetFlame].lit ? "[E] Extinguish" : "[E] Light";
    }

    // Toggle the targeted flame on E. Edge-triggered (was-up, now-down) so
    // holding the key doesn't flicker it on/off every frame.
    static bool ePrev = false;
    bool eNow = glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS;
    if (eNow && !ePrev && targetFlame >= 0) {
      scene[targetFlame].lit = !scene[targetFlame].lit;
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
