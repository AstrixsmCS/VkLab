#pragma once

#include "Vulkan.hpp"

#include <string>
#include <vector>

class CommandPool
{
public:
	CommandPool();
	~CommandPool();

	VkCommandBuffer AllocateCommandBuffer(bool begin, bool compute = false);
	void FlushCommandBuffer(VkCommandBuffer commandBuffer);
	void FlushCommandBuffer(VkCommandBuffer commandBuffer, VkQueue queue, bool compute = false);

	VkCommandPool GetGraphicsCommandPool() const { return m_GraphicsCommandPool; }
	VkCommandPool GetComputeCommandPool() const { return m_ComputeCommandPool; }
private:
	VkCommandPool m_GraphicsCommandPool = VK_NULL_HANDLE;
	VkCommandPool m_ComputeCommandPool = VK_NULL_HANDLE;
};

class CommandBuffer
{
public:
	void Create(uint32_t count, bool compute, const std::string& debugName = "");
	void Destroy();

	void Begin();
	void End();
	void Submit();

	VkCommandBuffer GetActiveCommandBuffer() const { return m_ActiveCommandBuffer; }
	VkCommandBuffer GetCommandBuffer(uint32_t frameIndex) const { return m_CommandBuffers[frameIndex]; }
private:
	std::string m_DebugName;

	VkCommandPool m_CommandPool = VK_NULL_HANDLE;

	std::vector<VkCommandBuffer> m_CommandBuffers;
	VkCommandBuffer m_ActiveCommandBuffer = VK_NULL_HANDLE;

	bool m_Compute = false;
};
