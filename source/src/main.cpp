// This has been adapted from the Vulkan tutorial
#include <sstream>

#include <json.hpp>

#include "modules/Starter.hpp"

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"

struct UniformBufferObject {
	alignas(16) glm::mat4 mvpMat;
	alignas(16) glm::mat4 mMat;
	alignas(16) glm::mat4 nMat;
};

struct GlobalUniformBufferObject {
	alignas(16) glm::vec3 lightDir;
	alignas(16) glm::vec4 lightColor;
	alignas(16) glm::vec3 eyePos;
	alignas(16) glm::vec4 debugView;
};

struct SkyboxUBO {
	alignas(16) glm::mat4 mvpMat; 
};

struct Vertex {
	glm::vec3 pos;
	glm::vec3 norm;
	glm::vec2 UV;
	glm::vec4 tan;
};

// MAIN !
class DungeonTavernNPC : public BaseProject {
	protected:
	// Here you list all the Vulkan objects you need:

	DescriptorSetLayout DSLlocal;   
	DescriptorSetLayout DSLglobal;  
	DescriptorSetLayout DSLskybox;  

	VertexDescriptor VD;        // pos + norm + UV + tan  
	VertexDescriptor VDskybox;  // pos only               
	RenderPass RP;
	Pipeline P;        // PBR + IBL pipeline
	Pipeline PSkybox;  // Skybox pipeline

	Model MSphere, MCube, MSoftbal, MStatue;
	Texture Talbedo[4], TNorm[4], Tmetal[4], Troughness[4], Tao[4];

	Texture TirrMap;   // irradiance cubemap 
	Texture TenvMap;   // environment cubemap 

	// Descriptor sets
	DescriptorSet DSglobal;
	DescriptorSet DSlocalSphere, DSlocalCube, DSlocalSoftbal;
	DescriptorSet DSlocalStatue;
	DescriptorSet DSskybox;

	float Ar;
	glm::mat4 ViewPrj;
	glm::mat4 SkyViewPrj;
	glm::vec3 cameraPos;
	glm::vec4 debugView = glm::vec4(0.0);
	bool imguiContextReady = false;
	bool imguiVulkanReady = false;
	bool showDebugPanel = true;
	float lastDeltaTime = 0.0f;
	float lastFps = 0.0f;

	static void checkImGuiVkResult(VkResult result) {
		if(result == VK_SUCCESS) {
			return;
		}
		PrintVkError(result);
		throw std::runtime_error("Dear ImGui Vulkan backend error");
	}

	// Here you set the main application parameters
	void setWindowParameters() {
		windowWidth = 1920;
		windowHeight = 1080;
		windowTitle = "Dungeon Tavern NPC";
    	windowResizable = GLFW_TRUE;
		Ar = 16.0f / 9.0f;
	}

	void onWindowResize(int w, int h) {
		std::cout << "Window resized to: " << w << " x " << h << "\n";
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

		if(!ImGui_ImplVulkan_Init(&initInfo)) {
			throw std::runtime_error("failed to initialize Dear ImGui Vulkan backend");
		}
		imguiVulkanReady = true;
	}

	void buildImGuiFrame() {
		ImGui_ImplVulkan_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		if(showDebugPanel) {
			ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f), ImGuiCond_FirstUseEver);
			ImGui::SetNextWindowSize(ImVec2(340.0f, 0.0f), ImGuiCond_FirstUseEver);
			ImGui::Begin("Dungeon Tavern NPC", &showDebugPanel, ImGuiWindowFlags_AlwaysAutoResize);
			ImGui::Text("Frame %.3f ms (%.1f FPS)", lastDeltaTime * 1000.0f, lastFps);
			ImGui::Separator();
			ImGui::Text("Camera");
			ImGui::Text("x %.2f  y %.2f  z %.2f", cameraPos.x, cameraPos.y, cameraPos.z);
			ImGui::Separator();
			ImGui::Text("Texture debug");
			ImGui::SliderFloat("Mode", &debugView.z, 0.0f, 3.0f, "%.0f");
			if(ImGui::Button("Reset mode")) {
				debugView.z = 0.0f;
			}
			ImGui::SameLine();
			ImGui::TextDisabled("Press 3 to cycle");
			ImGui::End();
		}

		ImGui::Render();
	}

	void shutdownImGuiVulkanBackend() {
		if(!imguiVulkanReady) {
			return;
		}
		ImGui_ImplVulkan_Shutdown();
		imguiVulkanReady = false;
	}

	void shutdownImGuiContext() {
		if(!imguiContextReady) {
			return;
		}
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();
		imguiContextReady = false;
	}

	void localInit() {
		DSLlocal.init(this, {
					{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,          VK_SHADER_STAGE_VERTEX_BIT,   sizeof(UniformBufferObject), 1},
					{1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,  VK_SHADER_STAGE_FRAGMENT_BIT, 0,  4},  // albedo[4]
					{2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,  VK_SHADER_STAGE_FRAGMENT_BIT, 4,  4},  // normal[4]
					{3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,  VK_SHADER_STAGE_FRAGMENT_BIT, 8,  4},  // metallic[4]
					{4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,  VK_SHADER_STAGE_FRAGMENT_BIT, 12, 4},  // roughness[4]
					{5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,  VK_SHADER_STAGE_FRAGMENT_BIT, 16, 4}   // ao[4]
				  });

		DSLglobal.init(this, {
					{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         VK_SHADER_STAGE_ALL_GRAPHICS, sizeof(GlobalUniformBufferObject), 1},
					{1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0, 1},   // irrMap (samplerCube)
					{2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0, 1}    // envMap (samplerCube)
				  });

		DSLskybox.init(this, {
					{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         VK_SHADER_STAGE_VERTEX_BIT,   sizeof(SkyboxUBO), 1},
					{1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0, 1}    // envMap (samplerCube)
				  });

		VD.init(this, {
				  {0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX}
				}, {
				  {0, 0, VK_FORMAT_R32G32B32_SFLOAT,    offsetof(Vertex, pos),  sizeof(glm::vec3), POSITION},
				  {0, 1, VK_FORMAT_R32G32B32_SFLOAT,    offsetof(Vertex, norm), sizeof(glm::vec3), NORMAL},
				  {0, 2, VK_FORMAT_R32G32_SFLOAT,       offsetof(Vertex, UV),   sizeof(glm::vec2), UV},
				  {0, 3, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, tan),  sizeof(glm::vec4), TANGENT}
				});

		VDskybox.init(this, {
				  {0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX}
				}, {
				  {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos), sizeof(glm::vec3), POSITION}
				});

		RP.init(this);
		RP.properties[0].clearValue = {0.08f, 0.10f, 0.16f, 1.0f};

		P.init(this, &VD,
			   "shaders/MeshTBN.vert.spv",
			   "shaders/ibl/IBLExercise.frag.spv",
			   {&DSLglobal, &DSLlocal});

		PSkybox.init(this, &VDskybox,
					 "shaders/skybox/SkyboxExercise.vert.spv",
					 "shaders/skybox/SkyboxExercise.frag.spv",
					 {&DSLskybox});
		PSkybox.setCompareOp(VK_COMPARE_OP_LESS_OR_EQUAL);
		PSkybox.setCullMode(VK_CULL_MODE_FRONT_BIT);   // we are inside the cube

		MSphere .init(this, &VD, "assets/models/Sphere.gltf",   GLTF);
		MCube   .init(this, &VD, "assets/models/Cube.gltf",     GLTF);
		MSoftbal.init(this, &VD, "assets/models/Softball.gltf", GLTF);
		MStatue .init(this, &VD, "assets/models/Statue.gltf",   GLTF);

		Talbedo[0].init(this, "assets/textures/ice-field/ice_field_albedo.png");
		TNorm[0]  .init(this, "assets/textures/ice-field/ice_field_normal-ogl.png",  VK_FORMAT_R8G8B8A8_UNORM);
		Tmetal[0] .init(this, "assets/textures/ice-field/ice_field_metallic.png",    VK_FORMAT_R8G8B8A8_UNORM);
		Troughness[0].init(this, "assets/textures/ice-field/ice_field_roughness.png",VK_FORMAT_R8G8B8A8_UNORM);
		Tao[0]    .init(this, "assets/textures/ice-field/ice_field_ao.png",          VK_FORMAT_R8G8B8A8_UNORM);

		Talbedo[1].init(this, "assets/textures/rock-wall-mortar/rock-wall-mortar_albedo.png");
		TNorm[1]  .init(this, "assets/textures/rock-wall-mortar/rock-wall-mortar_normal-ogl.png",  VK_FORMAT_R8G8B8A8_UNORM);
		Tmetal[1] .init(this, "assets/textures/rock-wall-mortar/rock-wall-mortar_metallic.png",    VK_FORMAT_R8G8B8A8_UNORM);
		Troughness[1].init(this, "assets/textures/rock-wall-mortar/rock-wall-mortar_roughness.png",VK_FORMAT_R8G8B8A8_UNORM);
		Tao[1]    .init(this, "assets/textures/rock-wall-mortar/rock-wall-mortar_ao.png",          VK_FORMAT_R8G8B8A8_UNORM);

		Talbedo[2].init(this, "assets/textures/granite-tile/granite-tile_albedo.png");
		TNorm[2]  .init(this, "assets/textures/granite-tile/granite-tile_normal-ogl.png",  VK_FORMAT_R8G8B8A8_UNORM);
		Tmetal[2] .init(this, "assets/textures/granite-tile/granite-tile_metallic.png",    VK_FORMAT_R8G8B8A8_UNORM);
		Troughness[2].init(this, "assets/textures/granite-tile/granite-tile_roughness.png",VK_FORMAT_R8G8B8A8_UNORM);
		Tao[2]    .init(this, "assets/textures/granite-tile/granite-tile_ao.png",          VK_FORMAT_R8G8B8A8_UNORM);

		Talbedo[3].init(this, "assets/textures/clay-shingles1/clay-shingles1_albedo.png");
		TNorm[3]  .init(this, "assets/textures/clay-shingles1/clay-shingles1_normal-ogl.png",  VK_FORMAT_R8G8B8A8_UNORM);
		Tmetal[3] .init(this, "assets/textures/clay-shingles1/clay-shingles1_metallic.png",    VK_FORMAT_R8G8B8A8_UNORM);
		Troughness[3].init(this, "assets/textures/clay-shingles1/clay-shingles1_roughness.png",VK_FORMAT_R8G8B8A8_UNORM);
		Tao[3]    .init(this, "assets/textures/clay-shingles1/clay-shingles1_ao.png",          VK_FORMAT_R8G8B8A8_UNORM);
		
		TirrMap.initCubic(this, {
			"assets/textures/suburban_garden_png/irr_posx.png",
			"assets/textures/suburban_garden_png/irr_negx.png",
			"assets/textures/suburban_garden_png/irr_posy.png",
			"assets/textures/suburban_garden_png/irr_negy.png",
			"assets/textures/suburban_garden_png/irr_posz.png",
			"assets/textures/suburban_garden_png/irr_negz.png"
		}, VK_FORMAT_R8G8B8A8_UNORM);

		TenvMap.initCubic(this, {
			"assets/textures/suburban_garden_png/env_posx.png",
			"assets/textures/suburban_garden_png/env_negx.png",
			"assets/textures/suburban_garden_png/env_posy.png",
			"assets/textures/suburban_garden_png/env_negy.png",
			"assets/textures/suburban_garden_png/env_posz.png",
			"assets/textures/suburban_garden_png/env_negz.png"
		}, VK_FORMAT_R8G8B8A8_UNORM);

		DPSZs.uniformBlocksInPool = 25;
		DPSZs.texturesInPool      = 130;
		DPSZs.setsInPool          = 25;

		initImGuiContext();

		submitCommandBuffer("main", 0, populateCommandBufferAccess, this);
	}

	void pipelinesAndDescriptorSetsInit() {
		RP.create();

		P      .create(&RP);
		PSkybox.create(&RP);
		initImGuiVulkanBackend();

		DSglobal.init(this, &DSLglobal, {
			TirrMap.getViewAndSampler(),
			TenvMap.getViewAndSampler()
		});

		DSskybox.init(this, &DSLskybox, {
			TenvMap.getViewAndSampler()
		});

		auto makeLocal = [&](DescriptorSet &DS) {
			DS.init(this, &DSLlocal, {
				Talbedo[0].getViewAndSampler(), Talbedo[1].getViewAndSampler(),
				Talbedo[2].getViewAndSampler(), Talbedo[3].getViewAndSampler(),
				TNorm[0].getViewAndSampler(),   TNorm[1].getViewAndSampler(),
				TNorm[2].getViewAndSampler(),   TNorm[3].getViewAndSampler(),
				Tmetal[0].getViewAndSampler(),  Tmetal[1].getViewAndSampler(),
				Tmetal[2].getViewAndSampler(),  Tmetal[3].getViewAndSampler(),
				Troughness[0].getViewAndSampler(), Troughness[1].getViewAndSampler(),
				Troughness[2].getViewAndSampler(), Troughness[3].getViewAndSampler(),
				Tao[0].getViewAndSampler(),     Tao[1].getViewAndSampler(),
				Tao[2].getViewAndSampler(),     Tao[3].getViewAndSampler()
			});
		};
		makeLocal(DSlocalSphere);
		makeLocal(DSlocalCube);
		makeLocal(DSlocalSoftbal);
		makeLocal(DSlocalStatue);

	}

	void pipelinesAndDescriptorSetsCleanup() {
		shutdownImGuiVulkanBackend();

		P      .cleanup();
		PSkybox.cleanup();

		RP.cleanup();

		DSglobal      .cleanup();
		DSskybox      .cleanup();
		DSlocalSphere .cleanup();
		DSlocalCube   .cleanup();
		DSlocalSoftbal.cleanup();
		DSlocalStatue .cleanup();
	}

	void localCleanup() {
		MSphere .cleanup();
		MCube   .cleanup();
		MSoftbal.cleanup();
		MStatue .cleanup();

		for(int i = 0; i < 4; i++) {
			Talbedo[i]   .cleanup();
			TNorm[i]     .cleanup();
			Tmetal[i]    .cleanup();
			Troughness[i].cleanup();
			Tao[i]       .cleanup();
		}
		TirrMap.cleanup();
		TenvMap.cleanup();

		DSLlocal .cleanup();
		DSLglobal.cleanup();
		DSLskybox.cleanup();

		VD      .cleanup();
		VDskybox.cleanup();

		P      .destroy();
		PSkybox.destroy();

		RP.destroy();
		shutdownImGuiContext();
	}

	static void populateCommandBufferAccess(VkCommandBuffer commandBuffer, int currentImage, void *Params) {
		DungeonTavernNPC *T = (DungeonTavernNPC *)Params;
		T->populateCommandBuffer(commandBuffer, currentImage);
	}

	void populateCommandBuffer(VkCommandBuffer commandBuffer, int currentImage) {
		RP.begin(commandBuffer, currentImage);

		P.bind(commandBuffer);
		DSglobal.bind(commandBuffer, P, 0, currentImage);

		MSphere.bind(commandBuffer);
		DSlocalSphere.bind(commandBuffer, P, 1, currentImage);
		vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(MSphere.indices.size()), 1, 0, 0, 0);

		MCube.bind(commandBuffer);
		DSlocalCube.bind(commandBuffer, P, 1, currentImage);
		vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(MCube.indices.size()), 1, 0, 0, 0);

		MSoftbal.bind(commandBuffer);
		DSlocalSoftbal.bind(commandBuffer, P, 1, currentImage);
		vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(MSoftbal.indices.size()), 1, 0, 0, 0);

		MStatue.bind(commandBuffer);
		DSlocalStatue.bind(commandBuffer, P, 1, currentImage);
		vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(MStatue.indices.size()), 1, 0, 0, 0);


		PSkybox.bind(commandBuffer);
		DSskybox.bind(commandBuffer, PSkybox, 0, currentImage);
		MCube.bind(commandBuffer);
		vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(MCube.indices.size()), 1, 0, 0, 0);

		ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);

		RP.end(commandBuffer);
	}

	void updateUniformBuffer(uint32_t currentImage) {
		static bool debounce = false;
		static int curDebounce = 0;

		if(glfwGetKey(window, GLFW_KEY_3)) {
			if(!debounce) {
				debounce = true;
				curDebounce = GLFW_KEY_3;
				debugView.z += 1.0f;
				if(debugView.z > 3.5f) debugView.z = 0.0f;
			}
		} else {
			if((curDebounce == GLFW_KEY_3) && debounce) {
				debounce = false;
				curDebounce = 0;
			}
		}
		if(glfwGetKey(window, GLFW_KEY_ESCAPE)) {
			glfwSetWindowShouldClose(window, GL_TRUE);
		}

		float deltaT = GameLogic();

		// ---- Global UBO ----------------------------------------------------
		static float lightRotationAngle = 0.0f;
		lightRotationAngle += 10.0f * deltaT;

		const glm::mat4 lightView =
			glm::rotate(glm::mat4(1), glm::radians(lightRotationAngle), glm::vec3(0, 1, 0)) *
			glm::rotate(glm::mat4(1), glm::radians(-45.0f),             glm::vec3(1, 0, 0));
		const glm::vec3 lightDir = glm::vec3(lightView * glm::vec4(0, 0, -1, 0));

		GlobalUniformBufferObject gubo{};
		gubo.lightDir   = lightDir;
		gubo.lightColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f) * 5.0f;
		gubo.eyePos     = cameraPos;
		gubo.debugView  = debugView;
		DSglobal.map(currentImage, &gubo, 0);

		// ---- Skybox UBO ----------------------------------------------------
		SkyboxUBO skyUbo{};
		skyUbo.mvpMat = SkyViewPrj * glm::scale(glm::mat4(1), glm::vec3(50.0f));
		DSskybox.map(currentImage, &skyUbo, 0);

		// ---- Per-object UBOs -----------------------------------------------
		UniformBufferObject ubo{};

		ubo.mMat   = glm::translate(glm::mat4(1), glm::vec3(-6, 1, 0)) * glm::scale(glm::mat4(1), glm::vec3(2.0f));
		ubo.mvpMat = ViewPrj * ubo.mMat;
		ubo.nMat   = glm::inverse(glm::transpose(ubo.mMat));
		DSlocalSphere.map(currentImage, &ubo, 0);

		ubo.mMat   = glm::translate(glm::mat4(1), glm::vec3(6, 1, 0)) * glm::scale(glm::mat4(1), glm::vec3(2.0f));
		ubo.mvpMat = ViewPrj * ubo.mMat;
		ubo.nMat   = glm::inverse(glm::transpose(ubo.mMat));
		DSlocalCube.map(currentImage, &ubo, 0);

		ubo.mMat   = glm::translate(glm::mat4(1), glm::vec3(-3, 1, -5)) * glm::scale(glm::mat4(1), glm::vec3(2.0f));
		ubo.mvpMat = ViewPrj * ubo.mMat;
		ubo.nMat   = glm::inverse(glm::transpose(ubo.mMat));
		DSlocalSoftbal.map(currentImage, &ubo, 0);

		ubo.mMat   = glm::translate(glm::mat4(1), glm::vec3(3, 0, -5)) * glm::scale(glm::mat4(1), glm::vec3(5.22f));
		ubo.mvpMat = ViewPrj * ubo.mMat;
		ubo.nMat   = glm::inverse(glm::transpose(ubo.mMat));
		DSlocalStatue.map(currentImage, &ubo, 0);

		static float elapsedT = 0.0f;
		static int countedFrames = 0;
		countedFrames++;
		elapsedT += deltaT;
		if(elapsedT > 1.0f) {
			lastFps = (float)countedFrames / elapsedT;
			elapsedT = 0.0f;
			countedFrames = 0;
		}

		lastDeltaTime = deltaT;
		buildImGuiFrame();
		submitCommandBuffer("main", 0, populateCommandBufferAccess, this);
	}

	float GameLogic() {
		const float FOVy      = glm::radians(45.0f);
		const float nearPlane = 0.1f;
		const float farPlane  = 100.0f;
		const float ROT_SPEED  = glm::radians(120.0f);
		const float MOVE_SPEED = 5.0f;

		float deltaT;
		glm::vec3 m = glm::vec3(0.0f), r = glm::vec3(0.0f);
		bool fire = false;
		getSixAxis(deltaT, m, r, fire);

		static glm::vec3 camPos = glm::vec3(0.0f, 1.5f, 10.0f);
		static float Yaw   = glm::radians(0.0f);
		static float Pitch = 0.0f;

		Yaw   += ROT_SPEED * deltaT * r.y;
		Pitch += ROT_SPEED * deltaT * r.x;
		Pitch  = glm::clamp(Pitch, glm::radians(-89.0f), glm::radians(89.0f));

		glm::vec3 forward = glm::normalize(glm::vec3(
			sin(Yaw) * cos(Pitch),
			-sin(Pitch),
			-cos(Yaw) * cos(Pitch)));
		glm::vec3 walkForward = glm::normalize(glm::vec3(sin(Yaw), 0.0f, -cos(Yaw)));
		glm::vec3 right = glm::normalize(glm::cross(walkForward, glm::vec3(0, 1, 0)));

		camPos += walkForward * MOVE_SPEED * deltaT * (-m.z);
		camPos += right       * MOVE_SPEED * deltaT *   m.x;

		if(glfwGetKey(window, GLFW_KEY_SPACE))
			camPos += glm::vec3(0, 1, 0) * MOVE_SPEED * deltaT;
		if(glfwGetKey(window, GLFW_KEY_TAB))
			camPos -= glm::vec3(0, 1, 0) * MOVE_SPEED * deltaT;

		cameraPos = camPos;

		glm::mat4 Prj = glm::perspective(FOVy, Ar, nearPlane, farPlane);
		Prj[1][1] *= -1;

		glm::mat4 View = glm::lookAt(camPos, camPos + forward, glm::vec3(0, 1, 0));
		ViewPrj    = Prj * View;
		SkyViewPrj = Prj * glm::mat4(glm::mat3(View));

		return deltaT;
	}
};


// This is the main: probably you do not need to touch this!
int main() {
    DungeonTavernNPC app;

    try {
        app.run();
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
