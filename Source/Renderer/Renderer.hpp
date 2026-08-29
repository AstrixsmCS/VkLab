#pragma once

#include "Vulkan.hpp"
#include "SwapChain.hpp"
#include "TimelineSemaphore.hpp"
#include "CommandBuffer.hpp"

#include <vector>

struct SDL_Window;

class Renderer
{
public:
	void Initialize(SDL_Window* windowHandle);
	void Shutdown();

	bool BeginFrame();
	void EndFrame();

	SwapChain& GetSwapChain() { return *m_SwapChain; }

	VkSemaphore GetImageAvailableSemaphore() const { return m_ImageAvailableSemaphores[s_CurrentFrameIndex]; }
	VkSemaphore GetRenderFinishedSemaphore(uint32_t imageIndex) const { return m_RenderFinishedSemaphores[imageIndex]; }

	static uint32_t GetCurrentFrameIndex() { return s_CurrentFrameIndex; }
	uint32_t GetCurrentImageIndex() const { return m_CurrentImageIndex; }

	VkCommandBuffer GetCurrentCommandBuffer() const { return m_FrameCommandBuffer.GetActiveCommandBuffer(); }
	CommandBuffer& GetFrameCommandBuffer() { return m_FrameCommandBuffer; }

	static constexpr uint32_t  MAX_FRAMES_IN_FLIGHT = 3;
	static constexpr uint32_t GetFramesInFlight() { return MAX_FRAMES_IN_FLIGHT; }
private:
	void CreateSyncObjects();
	void DestroySyncObjects();
private:
	std::unique_ptr<SwapChain> m_SwapChain = nullptr;

	CommandBuffer m_FrameCommandBuffer;

	std::vector<VkSemaphore> m_ImageAvailableSemaphores;
	std::vector<VkSemaphore> m_RenderFinishedSemaphores;

	TimelineSemaphore m_FrameTimeline;
	uint64_t m_NextSignalValue = 1;
	uint64_t m_CurrentSignalValue = 0;

	uint32_t m_CurrentImageIndex = UINT32_MAX;

	static inline uint32_t s_CurrentFrameIndex = 0;
};
