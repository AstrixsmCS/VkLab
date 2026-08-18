#pragma once

#include "Renderer/RendererContext.hpp"
#include "Renderer/Device.hpp"
#include "Renderer/Allocator.hpp"
#include "Renderer/SwapChain.hpp"
#include "Renderer/Shader.hpp"
#include "Renderer/TimelineSemaphore.hpp"
#include "Renderer/DynamicRendering.hpp"
#include "Renderer/GraphicsPipeline.hpp"
#include "Renderer/Buffer.hpp"

#include <string>
#include <array>

struct SDL_Window;

struct FrameResources
{
	VkCommandPool   commandPool            = VK_NULL_HANDLE;
	VkCommandBuffer commandBuffer          = VK_NULL_HANDLE;
};

struct Vertex
{
	float Position[3];
	float Color[3];
};

class Application
{
public:
	bool Initialize();
	void Shutdown();
	void Run();

private:
	void ShowError(const std::string& errorMessage) const;

	bool           InitializeVulkan();
	bool           CreateShader();
	bool           CreateGraphicsPipeline();
	bool           CreateVertexBuffer();
	bool           CreateSyncResources();
	bool           CreateCommandBuffers();
	void           Render();

private:
	static constexpr uint32_t MaxFramesInFlight = 2;

	SDL_Window* m_Window  = nullptr;
	uint32_t    m_Width   = 1280;
	uint32_t    m_Height  = 720;
	bool        m_Running = false;

	uint64_t m_FrameIndex      = 0;
	uint64_t m_NextSignalValue = MaxFramesInFlight + 1;

	// Device
	std::unique_ptr<PhysicalDevice> m_PhysicalDevice;
	LogicalDevice                   m_LogicalDevice;

	// Swapchain
	SwapChain m_SwapChain;

	// Pipeline
	Pipeline m_Pipeline;

	// Shader
	std::shared_ptr<Shader> m_Shader;

	// Geometry
	VertexBuffer m_VertexBuffer;

	// Synchronization
	TimelineSemaphore                             m_TimelineSemaphore;
	std::array<FrameResources, MaxFramesInFlight> m_FrameResources;
};
