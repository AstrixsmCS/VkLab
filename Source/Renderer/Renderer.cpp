#include "Renderer.hpp"

#include "RendererContext.hpp"

#include "../Application.hpp"

void Renderer::Initialize()
{
	m_FrameTimeline.Initialize(MAX_FRAMES_IN_FLIGHT);
	CreateSyncObjects();
}

void Renderer::CreateSyncObjects()
{
	VkDevice device = RendererContext::GetDevice()->GetDevice();

	uint32_t imageCount = m_SwapChain->GetImageCount();

	VkSemaphoreCreateInfo semaphoreInfo
	{
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
	};

	m_ImageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
	m_RenderFinishedSemaphores.resize(imageCount);

	for (VkSemaphore& semaphore : m_ImageAvailableSemaphores)
	{
		VK_CHECK(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &semaphore));
	}

	for (VkSemaphore& semaphore : m_RenderFinishedSemaphores)
	{
		VK_CHECK(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &semaphore));
	}
}

void Renderer::DestroySyncObjects()
{
	VkDevice device = RendererContext::GetDevice()->GetDevice();

	for (VkSemaphore semaphore : m_ImageAvailableSemaphores)
		vkDestroySemaphore(device, semaphore, nullptr);

	for (VkSemaphore semaphore : m_RenderFinishedSemaphores)
		vkDestroySemaphore(device, semaphore, nullptr);

	m_ImageAvailableSemaphores.clear();
	m_RenderFinishedSemaphores.clear();
}

void Renderer::BeginFrame()
{
	m_CurrentSignalValue = m_NextSignalValue++;

	const uint64_t waitValue = m_CurrentSignalValue - MAX_FRAMES_IN_FLIGHT;

	m_FrameTimeline.Wait(waitValue);
}

void Renderer::EndFrame()
{
	m_CurrentFrameIndex = (m_CurrentFrameIndex + 1) % MAX_FRAMES_IN_FLIGHT;
}

void Renderer::Shutdown()
{
	vkDeviceWaitIdle(RendererContext::GetDevice()->GetDevice());

	DestroySyncObjects();

	m_FrameTimeline.Shutdown();
}
