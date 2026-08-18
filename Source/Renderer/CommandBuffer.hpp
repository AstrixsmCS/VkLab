#pragma once

#include "Vulkan.hpp"

#include <string>
#include <vector>

class CommandBuffer
{
public:
	void Create(uint32_t count, const std::string& debugName = "");
	void Destroy();

	void Begin();
	void End();
	void Submit();

	void Reset();

	VkCommandBuffer GetActiveCommandBuffer() const { return m_ActiveCommandBuffer; }
	VkCommandBuffer GetCommandBuffer(uint32_t frameIndex) const { return m_CommandBuffers[frameIndex]; }
private:
	std::string m_DebugName;
	VkCommandPool m_CommandPool = VK_NULL_HANDLE;
	std::vector<VkCommandBuffer> m_CommandBuffers;
	VkCommandBuffer m_ActiveCommandBuffer = VK_NULL_HANDLE;
};
