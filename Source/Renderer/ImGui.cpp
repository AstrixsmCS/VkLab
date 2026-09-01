#include "ImGui.hpp"

#include "Renderer.hpp"
#include "RendererContext.hpp"

#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_vulkan.h>

#include <SDL3/SDL.h>

#include <cassert>

namespace
{
	VkDescriptorPool s_DescriptorPool = VK_NULL_HANDLE;
}

void ImGuiLayer::Initialize()
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();

	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

	ImGui::StyleColorsDark();

	VkDevice device = RendererContext::Get().GetDevice();

	const VkDescriptorPoolSize poolSizes[]
	{
		{
			.type = VK_DESCRIPTOR_TYPE_SAMPLER,
			.descriptorCount = 1000
		},
		{
			.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1000
		},
		{
			.type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
			.descriptorCount = 1000
		}
	};

	const VkDescriptorPoolCreateInfo poolInfo
	{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
		.maxSets = 1000,
		.poolSizeCount = static_cast<uint32_t>(std::size(poolSizes)),
		.pPoolSizes = poolSizes
	};

	VK_CHECK(vkCreateDescriptorPool(device, &poolInfo, nullptr, &s_DescriptorPool));

	int windowCount = 0;
	SDL_Window** windows = SDL_GetWindows(&windowCount);

	assert(windows && windowCount > 0 && "No SDL window available for ImGui!");

	SDL_Window* window = windows[0];

	ImGui_ImplSDL3_InitForVulkan(window);

	ImGui_ImplVulkan_InitInfo initInfo{};
	initInfo.Instance = RendererContext::Get().GetInstance();
	initInfo.PhysicalDevice = RendererContext::Get().GetPhysicalDevice();
	initInfo.Device = device;
	initInfo.QueueFamily = RendererContext::Get().GetGraphicsFamily();
	initInfo.Queue = RendererContext::Get().GetGraphicsQueue();
	initInfo.DescriptorPool = s_DescriptorPool;

	initInfo.MinImageCount = 2;
	initInfo.ImageCount = Renderer::GetFramesInFlight();

	initInfo.UseDynamicRendering = true;

	VkPipelineRenderingCreateInfo renderingInfo
	{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
		.colorAttachmentCount = 1
	};

	const VkFormat colorFormat = VK_FORMAT_B8G8R8A8_UNORM;
	renderingInfo.pColorAttachmentFormats = &colorFormat;

	initInfo.PipelineInfoMain.PipelineRenderingCreateInfo = renderingInfo;

	ImGui_ImplVulkan_Init(&initInfo);
}

void ImGuiLayer::Shutdown()
{
	VkDevice device = RendererContext::Get().GetDevice();

	vkDeviceWaitIdle(device);

	ImGui_ImplVulkan_Shutdown();
	ImGui_ImplSDL3_Shutdown();

	if (s_DescriptorPool != VK_NULL_HANDLE)
	{
		vkDestroyDescriptorPool(device, s_DescriptorPool, nullptr);

		s_DescriptorPool = VK_NULL_HANDLE;
	}

	ImGui::DestroyContext();
}

void ImGuiLayer::Begin()
{
	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplSDL3_NewFrame();

	ImGui::NewFrame();
}

void ImGuiLayer::End(CommandBuffer& commandBuffer)
{
	ImGui::Render();

	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer.GetHandle());
}
