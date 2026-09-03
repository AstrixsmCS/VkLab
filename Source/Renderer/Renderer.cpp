#include "Renderer.hpp"

#include "RendererContext.hpp"

void Renderer::Initialize(SDL_Window* windowHandle)
{
	RendererContext::Initialize();

	m_SwapChain = std::make_unique<SwapChain>(windowHandle);
	m_SwapChain->Initialize();

	m_FrameData.Initialize();
	CreateSyncObjects();
}

void Renderer::Shutdown()
{
	WaitForGPU();

	DestroySyncObjects();

	m_FrameTimeline.Shutdown();
	m_FrameData.Shutdown();

	m_SwapChain.reset();

	RendererContext::Shutdown();
}

void Renderer::WaitForGPU()
{
	vkDeviceWaitIdle(RendererContext::Get().GetDevice());
}

bool Renderer::BeginFrame()
{
	const uint32_t frameIndex = GetCurrentFrameIndex();
	const uint64_t waitValue = m_FrameSignalValues[frameIndex];

	if (waitValue != 0)
		m_FrameTimeline.Wait(waitValue);

	FrameContext& frame = GetCurrentFrame();

	frame.Reset();

	m_CurrentImageIndex = m_SwapChain->AcquireNextImage(GetImageAvailableSemaphore());

	if (m_CurrentImageIndex == UINT32_MAX)
		return false;

	frame.GraphicsCommandBuffer.Begin();

	return true;
}

void Renderer::EndFrame()
{
	FrameContext& frame = GetCurrentFrame();

	frame.GraphicsCommandBuffer.End();

	const uint64_t signalValue = m_NextSignalValue++;

	const VkSemaphoreSubmitInfo imageAvailableWait
	{
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
		.semaphore = GetImageAvailableSemaphore(),
		.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT
	};

	const VkSemaphoreSubmitInfo signalInfos[]
	{
		{
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
			.semaphore = GetRenderFinishedSemaphore(m_CurrentImageIndex),
			.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT
		},
		{
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
			.semaphore = m_FrameTimeline.GetHandle(),
			.value = signalValue,
			.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT
		}
	};

	const VkCommandBufferSubmitInfo commandBufferInfo
	{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
		.commandBuffer = frame.GraphicsCommandBuffer.GetHandle()
	};

	const VkSubmitInfo2 submitInfo
	{
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
		.waitSemaphoreInfoCount = 1,
		.pWaitSemaphoreInfos = &imageAvailableWait,
		.commandBufferInfoCount = 1,
		.pCommandBufferInfos = &commandBufferInfo,
		.signalSemaphoreInfoCount = 2,
		.pSignalSemaphoreInfos = signalInfos
	};

	VK_CHECK(vkQueueSubmit2(RendererContext::Get().GetGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE));

	m_FrameSignalValues[GetCurrentFrameIndex()] = signalValue;

	m_SwapChain->Present(GetRenderFinishedSemaphore(m_CurrentImageIndex));

	m_FrameData.Advance();
}

void Renderer::CreateSyncObjects()
{
	VkDevice device = RendererContext::Get().GetDevice();

	const uint32_t imageCount = m_SwapChain->GetImageCount();

	m_FrameTimeline.Initialize(0);
	m_FrameSignalValues.fill(0);

	VkSemaphoreCreateInfo semaphoreInfo
	{
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
	};

	m_ImageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
	m_RenderFinishedSemaphores.resize(imageCount);

	for (VkSemaphore& semaphore : m_ImageAvailableSemaphores)
		VK_CHECK(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &semaphore));

	for (VkSemaphore& semaphore : m_RenderFinishedSemaphores)
		VK_CHECK(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &semaphore));
}

void Renderer::DestroySyncObjects()
{
	VkDevice device = RendererContext::Get().GetDevice();

	for (VkSemaphore semaphore : m_ImageAvailableSemaphores)
		vkDestroySemaphore(device, semaphore, nullptr);

	for (VkSemaphore semaphore : m_RenderFinishedSemaphores)
		vkDestroySemaphore(device, semaphore, nullptr);

	m_ImageAvailableSemaphores.clear();
	m_RenderFinishedSemaphores.clear();
}
