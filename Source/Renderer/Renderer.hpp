#pragma once

#include "Vulkan.hpp"
#include "SwapChain.hpp"
#include "TimelineSemaphore.hpp"

#include <vector>

class Renderer
{
public:
	void Initialize();
	void Shutdown();

	void BeginFrame();
	void EndFrame();

	VkSemaphore GetImageAvailableSemaphore() const { return m_ImageAvailableSemaphores[m_CurrentFrameIndex]; }
	VkSemaphore GetRenderFinishedSemaphore(uint32_t imageIndex) const { return m_RenderFinishedSemaphores[imageIndex]; }

	uint32_t GetCurrentFrameIndex() const { return m_CurrentFrameIndex; }

	VkSemaphore GetFrameTimeline() const { return m_FrameTimeline.GetHandle(); }
	uint64_t GetCurrentSignalValue() const { return m_CurrentSignalValue; }

	void SetSwapChain(SwapChain* swapChain) { m_SwapChain = swapChain; }
private:
	void CreateSyncObjects();
	void DestroySyncObjects();
private:
	static constexpr uint32_t  MAX_FRAMES_IN_FLIGHT = 3;

	SwapChain* m_SwapChain = nullptr;

	std::vector<VkSemaphore> m_ImageAvailableSemaphores;
	std::vector<VkSemaphore> m_RenderFinishedSemaphores;

	TimelineSemaphore m_FrameTimeline;
	uint64_t m_NextSignalValue = MAX_FRAMES_IN_FLIGHT + 1;
	uint64_t m_CurrentSignalValue = 0;

	uint32_t m_CurrentFrameIndex = 0;
};
