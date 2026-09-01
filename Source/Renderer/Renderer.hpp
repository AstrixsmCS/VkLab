#pragma once

#include "Vulkan.hpp"
#include "SwapChain.hpp"
#include "TimelineSemaphore.hpp"
#include "CommandBuffer.hpp"
#include "RendererContext.hpp"

#include <vector>
#include <array>

struct SDL_Window;

static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 3;

struct FrameContext
{
	// ---- Render Resources ----

	CommandPool GraphicsCommandPool;
	CommandBuffer GraphicsCommandBuffer;

	CommandPool ComputeCommandPool;
	CommandBuffer ComputeCommandBuffer;

	std::vector<VkCommandBuffer> CommandBuffers;

	FrameContext()
	{
		CommandBuffers.reserve(16);
	}

	void AddCommandBuffer(VkCommandBuffer commandBuffer)
	{
		CommandBuffers.push_back(commandBuffer);
	}

	void Reset()
	{
		GraphicsCommandPool.Reset();
		ComputeCommandPool.Reset();

		CommandBuffers.clear();
	}
};

class FrameData
{
public:
	void Initialize()
	{
		RendererContext& context = RendererContext::Get();

		m_FrameIndex = 0;

		for (auto& frame : m_Frames)
		{
			frame.GraphicsCommandPool.Create(context.GetGraphicsFamily());
			frame.ComputeCommandPool.Create(context.GetComputeFamily());

			frame.GraphicsCommandBuffer = frame.GraphicsCommandPool.AllocateCommandBuffer();
			frame.ComputeCommandBuffer = frame.ComputeCommandPool.AllocateCommandBuffer();

			frame.CommandBuffers.clear();
		}
	}

	void Shutdown()
	{
		for (auto& frame : m_Frames)
		{
			frame.GraphicsCommandPool.Destroy();
			frame.ComputeCommandPool.Destroy();

			frame.GraphicsCommandBuffer = {};
			frame.ComputeCommandBuffer = {};

			frame.CommandBuffers.clear();
		}

		m_FrameIndex = 0;
	}

	// Current frame slot
	FrameContext& Current() { return m_Frames[m_FrameIndex % MAX_FRAMES_IN_FLIGHT]; }
	const FrameContext& Current() const { return m_Frames[m_FrameIndex % MAX_FRAMES_IN_FLIGHT]; }

	// Previous frame slot
	FrameContext& Previous() { return m_Frames[(m_FrameIndex + MAX_FRAMES_IN_FLIGHT - 1) % MAX_FRAMES_IN_FLIGHT]; }
	const FrameContext& Previous() const { return m_Frames[(m_FrameIndex + MAX_FRAMES_IN_FLIGHT - 1) % MAX_FRAMES_IN_FLIGHT]; }

	// Access by absolute frame index
	FrameContext& GetFrame(uint64_t index) { return m_Frames[index % MAX_FRAMES_IN_FLIGHT]; }
	const FrameContext& GetFrame(uint64_t index) const { return m_Frames[index % MAX_FRAMES_IN_FLIGHT]; }

	// Absolute frame index
	uint64_t GetFrameIndex() const { return m_FrameIndex; }

	// Advance to the next frame slot
	void Advance() { m_FrameIndex++; }
private:
	std::array<FrameContext, MAX_FRAMES_IN_FLIGHT> m_Frames;
	uint64_t m_FrameIndex = 0;
};

class Renderer
{
public:
	void Initialize(SDL_Window* windowHandle);
	void Shutdown();

	bool BeginFrame();
	void EndFrame();

	SwapChain& GetSwapChain() { return *m_SwapChain; }

	FrameContext& GetCurrentFrame() { return m_FrameData.Current(); }
	const FrameContext& GetCurrentFrame() const { return m_FrameData.Current(); }

	VkSemaphore GetImageAvailableSemaphore() const { return m_ImageAvailableSemaphores[m_FrameData.GetFrameIndex() % MAX_FRAMES_IN_FLIGHT]; }
	VkSemaphore GetRenderFinishedSemaphore(uint32_t imageIndex) const { return m_RenderFinishedSemaphores[imageIndex]; }

	uint32_t GetCurrentFrameIndex() const { return static_cast<uint32_t>(m_FrameData.GetFrameIndex() % MAX_FRAMES_IN_FLIGHT); }
	uint32_t GetCurrentImageIndex() const { return m_CurrentImageIndex; }

	VkCommandBuffer GetCurrentCommandBuffer() const { return GetCurrentFrame().GraphicsCommandBuffer.GetHandle(); }
	CommandBuffer& GetFrameCommandBuffer() { return GetCurrentFrame().GraphicsCommandBuffer; }

	static constexpr uint32_t GetFramesInFlight() { return MAX_FRAMES_IN_FLIGHT; }

private:
	void CreateSyncObjects();
	void DestroySyncObjects();

private:
	std::unique_ptr<SwapChain> m_SwapChain = nullptr;

	FrameData m_FrameData;

	std::vector<VkSemaphore> m_ImageAvailableSemaphores;
	std::vector<VkSemaphore> m_RenderFinishedSemaphores;

	TimelineSemaphore m_FrameTimeline;
	uint64_t m_NextSignalValue = 1;
	std::array<uint64_t, MAX_FRAMES_IN_FLIGHT> m_FrameSignalValues{};

	uint32_t m_CurrentImageIndex = UINT32_MAX;
};
