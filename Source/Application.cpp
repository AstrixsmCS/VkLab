#include "Application.hpp"

#include <SDL3/SDL.h>

#include <print>

void Application::ShowError(const std::string& errorMessage) const
{
	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", errorMessage.c_str(), m_Window);
}

bool Application::Initialize()
{
	SDL_InitSubSystem(SDL_INIT_VIDEO);

	m_Window = SDL_CreateWindow("VkLab", m_Width, m_Height, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
	if (!m_Window)
	{
		ShowError("Error creating window");
		return false;
	}

	return InitializeVulkan();
}

void Application::Shutdown()
{
	vkDeviceWaitIdle(m_LogicalDevice.GetDevice());

	m_TimelineSemaphore.Shutdown();

	for (auto& res : m_FrameResources)
		vkDestroyCommandPool(m_LogicalDevice.GetDevice(), res.commandPool, nullptr);

	m_Pipeline.Shutdown();

	if (m_Shader)
	{
		m_Shader->Shutdown();
		m_Shader.reset();
	}

	m_VertexBuffer.Shutdown();

	m_SwapChain.Shutdown();

	Allocator::Shutdown();

	m_LogicalDevice.Destroy();
	m_PhysicalDevice.reset();

	RendererContext::Shutdown();

	if (m_Window)
	{
		SDL_DestroyWindow(m_Window);
		m_Window = nullptr;
	}
	SDL_Quit();
}

void Application::Run()
{
	m_Running = true;
	while (m_Running)
	{
		SDL_Event event{};
		while (SDL_PollEvent(&event))
		{
			if (event.type == SDL_EVENT_QUIT)
			{
				m_Running = false;
				break;
			}
			else if (event.type == SDL_EVENT_WINDOW_RESIZED)
			{
				m_Width  = event.window.data1;
				m_Height = event.window.data2;
			}
		}

		Render();
	}
}

bool Application::InitializeVulkan()
{
	RendererContext::Initialize();

	m_PhysicalDevice = PhysicalDevice::Select();
	m_LogicalDevice.Create(*m_PhysicalDevice);

	RendererContext::SetPhysicalDevice(m_PhysicalDevice.get());
	RendererContext::SetLogicalDevice(&m_LogicalDevice);

	Allocator::Initialize(m_LogicalDevice);

	m_SwapChain.SetWindow(m_Window);
	m_SwapChain.Initialize();

	if (!CreateShader())
	{
		ShowError("Error creating shader modules.");
		return false;
	}

	if (!CreateGraphicsPipeline())
	{
		ShowError("Unable to initialize the graphics pipeline.");
		return false;
	}

	if (!CreateVertexBuffer())
	{
		ShowError("Unable to create vertex buffer.");
		return false;
	}

	if (!CreateSyncResources())
	{
		ShowError("Couldn't create synchronization resources.");
		return false;
	}

	if (!CreateCommandBuffers())
	{
		ShowError("Couldn't create command buffer objects.");
		return false;
	}

	return true;
}

bool Application::CreateShader()
{
	m_Shader = std::make_shared<Shader>();

	m_Shader->Load("Resources/Shaders/Triangle.slang");

	return m_Shader->IsValid();
}

bool Application::CreateGraphicsPipeline()
{
	VkVertexInputBindingDescription bindingDesc
	{
		.binding   = 0,
		.stride    = sizeof(Vertex),
		.inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
	};

	VkVertexInputAttributeDescription attributeDescs[]
	{
		{
			.location = 0,
			.binding  = 0,
			.format   = VK_FORMAT_R32G32B32_SFLOAT,
			.offset   = offsetof(Vertex, Position),
		},
		{
			.location = 1,
			.binding  = 0,
			.format   = VK_FORMAT_R32G32B32_SFLOAT,
			.offset   = offsetof(Vertex, Color),
		},
	};

	PipelineSpecification spec;
	spec.Shader               = m_Shader;
	spec.ColorFormats         = { Format::BGRA8_UNorm };
	spec.DepthFormat          = Format::D32_Float;
	spec.DepthTest            = true;
	spec.DepthWrite           = true;
	spec.DepthCompareOp       = CompareOp::Less;
	spec.CullMode             = CullMode::Back;
	spec.FrontFace            = WindingMode::CW;
	spec.Topology             = Topology::Triangle;
	spec.PolygonMode          = PolygonMode::Fill;
	spec.BlendEnabled         = false;
	spec.BindingDescriptions  = { bindingDesc };
	spec.AttributeDescriptions = { std::begin(attributeDescs), std::end(attributeDescs) };
	spec.DebugName            = "Triangle Pipeline";

	m_Pipeline.Create(spec);

	return m_Pipeline.GetPipeline() != VK_NULL_HANDLE;

}

bool Application::CreateVertexBuffer()
{
	const Vertex vertices[]
	{
		{ {  0.0f, -0.5f, 0.0f }, { 1.0f, 0.0f, 0.0f } }, // top    — red
		{ {  0.5f,  0.5f, 0.0f }, { 0.0f, 1.0f, 0.0f } }, // bottom right — green
		{ { -0.5f,  0.5f, 0.0f }, { 0.0f, 0.0f, 1.0f } }, // bottom left  — blue
	};

	BufferSpecification spec
	{
		.Size      = sizeof(vertices),
		.Usage     = BufferUsage::Vertex,
		.Memory    = BufferMemory::CPUToGPU,
		.Mapped    = false,
		.DebugName = "Triangle Vertex Buffer",
	};

	m_VertexBuffer.Create(spec);
	m_VertexBuffer.SetData(vertices, sizeof(vertices));

	return m_VertexBuffer.GetBuffer() != VK_NULL_HANDLE;
}

bool Application::CreateSyncResources()
{
	m_TimelineSemaphore.Initialize(MaxFramesInFlight);
	return true;
}

bool Application::CreateCommandBuffers()
{
	for (auto& res : m_FrameResources)
	{
		VkCommandPoolCreateInfo poolInfo
		{
			.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
			.queueFamilyIndex = static_cast<uint32_t>(m_PhysicalDevice->GetQueueFamilyIndices().Graphics),
		};

		if (vkCreateCommandPool(m_LogicalDevice.GetDevice(), &poolInfo, nullptr, &res.commandPool) != VK_SUCCESS)
		{
			ShowError("Unable to create command pool.");
			return false;
		}

		VkCommandBufferAllocateInfo allocInfo
		{
			.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
			.commandPool        = res.commandPool,
			.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			.commandBufferCount = 1,
		};

		if (vkAllocateCommandBuffers(m_LogicalDevice.GetDevice(), &allocInfo, &res.commandBuffer) != VK_SUCCESS)
		{
			ShowError("Unable to allocate command buffer.");
			return false;
		}
	}

	return true;
}

void Application::Render()
{
	const uint32_t frameResIndex = m_FrameIndex++ % MaxFramesInFlight;
	const uint64_t signalValue   = m_NextSignalValue++;
	const uint64_t waitValue     = signalValue - MaxFramesInFlight;

	m_TimelineSemaphore.Wait(waitValue);

	FrameResources& res = m_FrameResources[frameResIndex];
	vkResetCommandPool(m_LogicalDevice.GetDevice(), res.commandPool, 0);

	const uint32_t imageIndex = m_SwapChain.AcquireNextImage();
	if (imageIndex == UINT32_MAX)
		return;

	const VkExtent2D extent = m_SwapChain.GetExtent();

	VkCommandBufferBeginInfo beginInfo
	{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};
	vkBeginCommandBuffer(res.commandBuffer, &beginInfo);

	// Transition color + depth to attachment layouts
	const VkImageMemoryBarrier2 layoutBarriers[]
	{
		{
			.sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
			.srcStageMask  = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
			.srcAccessMask = 0,
			.dstStageMask  = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
			.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
			.oldLayout     = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			.image         = m_SwapChain.GetCurrentImage(),
			.subresourceRange = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 },
		},
		{
			.sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
			.srcStageMask  = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
			.srcAccessMask = 0,
			.dstStageMask  = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
			.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			.oldLayout     = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout     = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
			.image         = m_SwapChain.m_DepthStencil.Image,
			.subresourceRange = { .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT, .levelCount = 1, .layerCount = 1 },
		},
	};

	VkDependencyInfo depInfo
	{
		.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
		.imageMemoryBarrierCount = 2,
		.pImageMemoryBarriers    = layoutBarriers,
	};
	vkCmdPipelineBarrier2(res.commandBuffer, &depInfo);

	AttachmentInfo colorAttachment
	{
		.ImageView = m_SwapChain.GetCurrentImageView(),
		.Format = Format::BGRA8_UNorm,
		.LoadOp = LoadOp::Clear,
		.StoreOp = StoreOp::Store,
		.ClearValue =
		{
			.color =
			{
				.float32 =
				{
					0.1f,
					0.1f,
					0.1f,
					1.0f
				}
			}
		},
		.Layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
	};

	AttachmentInfo depthAttachment
	{
		.ImageView = m_SwapChain.m_DepthStencil.ImageView,
		.Format = Format::D32_Float,
		.LoadOp = LoadOp::Clear,
		.StoreOp = StoreOp::DontCare,
		.ClearValue =
		{
			.depthStencil =
			{
				.depth = 1.0f,
				.stencil = 0
			}
		},
		.Layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL
	};

	const AttachmentInfo colorAttachments[]
	{
		colorAttachment
	};

	RenderPassInfo renderPassInfo
	{
		.ColorAttachments = colorAttachments,
		.DepthAttachment = &depthAttachment,
		.RenderArea =
		{
			.offset = { 0, 0 },
			.extent = extent
		},
		.LayerCount = 1
	};

	DynamicRendering::BeginRendering(res.commandBuffer, renderPassInfo);
	{
		VkViewport viewport{ .x = 0, .y = 0, .width = static_cast<float>(extent.width), .height = static_cast<float>(extent.height), .minDepth = 0.0f, .maxDepth = 1.0f };
		vkCmdSetViewport(res.commandBuffer, 0, 1, &viewport);

		VkRect2D scissor{ .offset = { 0, 0 }, .extent = extent };
		vkCmdSetScissor(res.commandBuffer, 0, 1, &scissor);

		m_Pipeline.Bind(res.commandBuffer);

		const VkBuffer vertexBuffers[] = { m_VertexBuffer.GetBuffer() };
		const VkDeviceSize offsets[]   = { 0 };
		vkCmdBindVertexBuffers(res.commandBuffer, 0, 1, vertexBuffers, offsets);

		vkCmdDraw(res.commandBuffer, 3, 1, 0, 0);
	}
	DynamicRendering::EndRendering(res.commandBuffer);

	// Transition color to present layout
	const VkImageMemoryBarrier2 presentBarrier
	{
		.sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
		.srcStageMask  = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
		.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
		.dstStageMask  = VK_PIPELINE_STAGE_2_NONE,
		.dstAccessMask = 0,
		.oldLayout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		.newLayout     = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
		.image         = m_SwapChain.GetCurrentImage(),
		.subresourceRange = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 },
	};

	VkDependencyInfo presentDepInfo
	{
		.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
		.imageMemoryBarrierCount = 1,
		.pImageMemoryBarriers    = &presentBarrier,
	};
	vkCmdPipelineBarrier2(res.commandBuffer, &presentDepInfo);

	vkEndCommandBuffer(res.commandBuffer);

	// SwapChain's RenderFinished semaphore is indexed by image, so we signal it here
	// and SwapChain::Present waits on it.
	const VkSemaphoreSubmitInfo signalInfos[]
	{
		{
			.sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
			.semaphore = m_SwapChain.m_RenderFinishedSemaphores[imageIndex],
			.stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
		},
		{
			.sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
			.semaphore = m_TimelineSemaphore.GetHandle(),
			.value     = signalValue,
			.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
		},
	};

	// SwapChain's ImageAvailable semaphore is indexed by frame-in-flight.
	const VkSemaphoreSubmitInfo acquireWait
	{
		.sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
		.semaphore = m_SwapChain.m_ImageAvailableSemaphores[frameResIndex],
		.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
	};

	const VkCommandBufferSubmitInfo cmdSubmit
	{
		.sType         = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
		.commandBuffer = res.commandBuffer,
	};

	const VkSubmitInfo2 submitInfo
	{
		.sType                    = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
		.waitSemaphoreInfoCount   = 1,
		.pWaitSemaphoreInfos      = &acquireWait,
		.commandBufferInfoCount   = 1,
		.pCommandBufferInfos      = &cmdSubmit,
		.signalSemaphoreInfoCount = 2,
		.pSignalSemaphoreInfos    = signalInfos,
	};

	vkQueueSubmit2(m_LogicalDevice.GetGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);

	m_SwapChain.Present();

}
