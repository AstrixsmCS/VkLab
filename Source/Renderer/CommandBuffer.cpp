#include "CommandBuffer.hpp"

#include "Renderer.hpp"
#include "RendererContext.hpp"

#include <cassert>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Command Pool
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

CommandPool::CommandPool()
{
	VkDevice device = RendererContext::Get().GetDevice();

	VkCommandPoolCreateInfo poolInfo
	{
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
		.queueFamilyIndex = RendererContext::Get().GetGraphicsFamily()
	};
	VK_CHECK(vkCreateCommandPool(device, &poolInfo, nullptr, &m_GraphicsCommandPool));

	poolInfo.queueFamilyIndex = RendererContext::Get().GetComputeFamily();
	VK_CHECK(vkCreateCommandPool(device, &poolInfo, nullptr, &m_ComputeCommandPool));
}

CommandPool::~CommandPool()
{
	VkDevice device = RendererContext::Get().GetDevice();

	if (m_GraphicsCommandPool)
	{
		vkDestroyCommandPool(device, m_GraphicsCommandPool, nullptr);
		m_GraphicsCommandPool = VK_NULL_HANDLE;
	}

	if (m_ComputeCommandPool)
	{
		vkDestroyCommandPool(device, m_ComputeCommandPool, nullptr);
		m_ComputeCommandPool = VK_NULL_HANDLE;
	}
}

VkCommandBuffer CommandPool::AllocateCommandBuffer(bool begin, bool compute)
{
	VkDevice device = RendererContext::Get().GetDevice();

	VkCommandBufferAllocateInfo allocInfo
	{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = compute ? m_ComputeCommandPool : m_GraphicsCommandPool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1
	};

	VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
	VK_CHECK(vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer));

	if (begin)
	{
		VkCommandBufferBeginInfo beginInfo
		{
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
			.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
		};

		VK_CHECK(vkBeginCommandBuffer(commandBuffer, &beginInfo));
	}

	return commandBuffer;
}

void CommandPool::FlushCommandBuffer(VkCommandBuffer commandBuffer)
{
	FlushCommandBuffer(commandBuffer, RendererContext::Get().GetGraphicsQueue(), false);
}

void CommandPool::FlushCommandBuffer(VkCommandBuffer commandBuffer, VkQueue queue, bool compute)
{
	assert(commandBuffer != VK_NULL_HANDLE);
	assert(queue != VK_NULL_HANDLE);

	VkDevice device = RendererContext::Get().GetDevice();

	VK_CHECK(vkEndCommandBuffer(commandBuffer));

	VkCommandBufferSubmitInfo commandBufferInfo
	{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
		.commandBuffer = commandBuffer
	};

	VkSubmitInfo2 submitInfo
	{
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
		.commandBufferInfoCount = 1,
		.pCommandBufferInfos = &commandBufferInfo
	};

	VkFenceCreateInfo fenceInfo
	{
		.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO
	};

	VkFence fence = VK_NULL_HANDLE;
	VK_CHECK(vkCreateFence(device, &fenceInfo, nullptr, &fence));

	VK_CHECK(vkQueueSubmit2(queue, 1, &submitInfo, fence));

	VK_CHECK(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX));
	vkDestroyFence(device, fence, nullptr);

	VkCommandPool commandPool = compute ? m_ComputeCommandPool : m_GraphicsCommandPool;

	vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Command Buffer
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void CommandBuffer::Create(uint32_t count, bool compute, const std::string& debugName)
{
	m_DebugName = debugName;
	m_Compute = compute;

	VkDevice device = RendererContext::Get().GetDevice();
	CommandPool& commandPool = RendererContext::Get().GetCommandPool();

	m_CommandPool = m_Compute ? commandPool.GetComputeCommandPool() : commandPool.GetGraphicsCommandPool();

	const uint32_t commandBufferCount = count == 0 ? Renderer::MAX_FRAMES_IN_FLIGHT : count;

	m_CommandBuffers.resize(commandBufferCount);

	VkCommandBufferAllocateInfo allocInfo
	{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = m_CommandPool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = commandBufferCount
	};

	VK_CHECK(vkAllocateCommandBuffers(device, &allocInfo, m_CommandBuffers.data()));
}

void CommandBuffer::Destroy()
{
	if (m_CommandPool == VK_NULL_HANDLE)
		return;

	VkDevice device = RendererContext::Get().GetDevice();

	if (!m_CommandBuffers.empty())
	{
		vkFreeCommandBuffers(device, m_CommandPool, static_cast<uint32_t>(m_CommandBuffers.size()), m_CommandBuffers.data());
		m_CommandBuffers.clear();
	}

	m_CommandPool = VK_NULL_HANDLE;
	m_ActiveCommandBuffer = VK_NULL_HANDLE;

	m_DebugName.clear();
	m_Compute = false;
}

void CommandBuffer::Begin()
{
	const uint32_t frameIndex = Renderer::GetCurrentFrameIndex();

	assert(frameIndex < m_CommandBuffers.size());

	m_ActiveCommandBuffer = m_CommandBuffers[frameIndex];

	VK_CHECK(vkResetCommandBuffer(m_ActiveCommandBuffer, 0));

	VkCommandBufferBeginInfo beginInfo
	{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
	};

	VK_CHECK(vkBeginCommandBuffer(m_ActiveCommandBuffer, &beginInfo));
}

void CommandBuffer::End()
{
	assert(m_ActiveCommandBuffer != VK_NULL_HANDLE);

	VK_CHECK(vkEndCommandBuffer(m_ActiveCommandBuffer));

	m_ActiveCommandBuffer = VK_NULL_HANDLE;
}

void CommandBuffer::Submit()
{
	const uint32_t frameIndex = Renderer::GetCurrentFrameIndex();

	assert(frameIndex < m_CommandBuffers.size());

	VkCommandBuffer commandBuffer = m_CommandBuffers[frameIndex];

	VkCommandBufferSubmitInfo commandBufferInfo
	{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
		.commandBuffer = commandBuffer
	};

	VkSubmitInfo2 submitInfo
	{
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
		.commandBufferInfoCount = 1,
		.pCommandBufferInfos = &commandBufferInfo
	};

	VkQueue queue = m_Compute ? RendererContext::Get().GetComputeQueue() : RendererContext::Get().GetGraphicsQueue();

	VK_CHECK(vkQueueSubmit2(queue, 1, &submitInfo, VK_NULL_HANDLE));
}
