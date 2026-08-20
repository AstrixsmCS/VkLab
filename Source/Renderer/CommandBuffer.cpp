#include "CommandBuffer.hpp"

#include "Renderer.hpp"
#include "RendererContext.hpp"

void CommandBuffer::Create(uint32_t count, const std::string& debugName)
{
	m_DebugName = debugName;

	VkDevice device = RendererContext::Get().GetDevice();

	const uint32_t commandBufferCount = Renderer::MAX_FRAMES_IN_FLIGHT;

	VkCommandPoolCreateInfo poolInfo
	{
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
		.queueFamilyIndex = static_cast<uint32_t>(RendererContext::Get().GetGraphicsFamily())
	};

	VK_CHECK(vkCreateCommandPool(device, &poolInfo, nullptr, &m_CommandPool));

	m_CommandBuffers.resize(commandBufferCount);

	VkCommandBufferAllocateInfo allocInfo
	{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = m_CommandPool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = count == 0 ? Renderer::MAX_FRAMES_IN_FLIGHT : count
	};

	VK_CHECK(vkAllocateCommandBuffers(device, &allocInfo, m_CommandBuffers.data()));
}

void CommandBuffer::Destroy()
{
	if (m_CommandPool == VK_NULL_HANDLE)
		return;

	VkDevice device = RendererContext::Get().GetDevice();

	vkDestroyCommandPool(device, m_CommandPool, nullptr);

	m_CommandPool = VK_NULL_HANDLE;

	m_CommandBuffers.clear();

	m_ActiveCommandBuffer = VK_NULL_HANDLE;
}

void CommandBuffer::Reset()
{
	const uint32_t frameIndex = Renderer::GetCurrentFrameIndex();

	VK_CHECK(vkResetCommandBuffer(m_CommandBuffers[frameIndex], 0));
}

void CommandBuffer::Begin()
{
	const uint32_t frameIndex = Renderer::GetCurrentFrameIndex();

	m_ActiveCommandBuffer = m_CommandBuffers[frameIndex];

	VkCommandBufferBeginInfo beginInfo
	{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
	};

	VK_CHECK(vkBeginCommandBuffer(m_ActiveCommandBuffer, &beginInfo));
}

void CommandBuffer::End()
{
	VK_CHECK(vkEndCommandBuffer(m_ActiveCommandBuffer));

	m_ActiveCommandBuffer = VK_NULL_HANDLE;
}

void CommandBuffer::Submit()
{
	const uint32_t frameIndex = Renderer::GetCurrentFrameIndex();

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

	VK_CHECK(vkQueueSubmit2(RendererContext::Get().GetGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE));
}
