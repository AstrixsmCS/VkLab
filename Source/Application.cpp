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

	for (auto& res : m_FrameResources)
		vkDestroyCommandPool(m_LogicalDevice.GetDevice(), res.commandPool, nullptr);

	m_Pipeline.Shutdown();

	if (m_Shader)
	{
		m_Shader->Shutdown();
		m_Shader.reset();
	}

	m_IndexBuffer.Destroy();
	m_VertexBuffer.Destroy();

	m_Renderer.Shutdown();

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

	m_Renderer.SetSwapChain(&m_SwapChain);
	m_Renderer.Initialize();

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

	if (!CreateGeometry())
	{
		ShowError("Unable to create geometry buffers.");
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
	m_Shader->Load("Resources/Shaders/Geometry.slang");
	return m_Shader->IsValid();
}

bool Application::CreateGraphicsPipeline()
{
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
	spec.Layout         =
	{
		{ ShaderDataType::Float3, "Position" },
		{ ShaderDataType::Float3, "Color"    },
	};
	spec.DebugName            = "Triangle Pipeline";

	m_Pipeline.Create(spec);

	return m_Pipeline.GetPipeline() != VK_NULL_HANDLE;
}

bool Application::CreateGeometry()
{
	const Vertex vertices[]
	{
		{ { -0.5f, -0.5f, 0.0f }, { 1.0f, 0.0f, 0.0f } },  // 0 top-left
		{ {  0.5f, -0.5f, 0.0f }, { 0.0f, 1.0f, 0.0f } },  // 1 top-right
		{ {  0.5f,  0.5f, 0.0f }, { 0.0f, 0.0f, 1.0f } },  // 2 bottom-right
		{ { -0.5f,  0.5f, 0.0f }, { 1.0f, 1.0f, 0.0f } },  // 3 bottom-left
	};

	const uint32_t indices[]
	{
		0, 1, 2,
		2, 3, 0,
	};

	m_VertexBuffer.Create(vertices, sizeof(vertices));
	m_IndexBuffer.Create(indices, sizeof(indices));

	return m_VertexBuffer.GetBuffer() != VK_NULL_HANDLE && m_IndexBuffer.GetBuffer()  != VK_NULL_HANDLE;
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
	m_Renderer.BeginFrame();

	const uint32_t frameResIndex = m_FrameIndex++ % MaxFramesInFlight;

	FrameResources& res = m_FrameResources[frameResIndex];
	vkResetCommandPool(m_LogicalDevice.GetDevice(), res.commandPool, 0);

	const uint32_t imageIndex = m_SwapChain.AcquireNextImage(m_Renderer.GetImageAvailableSemaphore());
	if (imageIndex == UINT32_MAX)
	{
		m_Renderer.EndFrame();
		return;
	}

	const VkExtent2D extent = m_SwapChain.GetExtent();

	VkCommandBufferBeginInfo beginInfo
	{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};
	vkBeginCommandBuffer(res.commandBuffer, &beginInfo);

	SetImageLayout(
		res.commandBuffer,
		m_SwapChain.GetCurrentImage(),
		VK_IMAGE_ASPECT_COLOR_BIT,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);

	SetImageLayout(
		res.commandBuffer,
		m_SwapChain.m_DepthStencil.Image,
		VK_IMAGE_ASPECT_DEPTH_BIT,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
		VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
		VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT);

	AttachmentInfo colorAttachment
	{
		.ImageView  = m_SwapChain.GetCurrentImageView(),
		.Format     = Format::BGRA8_UNorm,
		.LoadOp     = LoadOp::Clear,
		.StoreOp    = StoreOp::Store,
		.ClearValue = { .color = { .float32 = { 0.1f, 0.1f, 0.1f, 1.0f } } },
		.Layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
	};

	AttachmentInfo depthAttachment
	{
		.ImageView  = m_SwapChain.m_DepthStencil.ImageView,
		.Format     = Format::D32_Float,
		.LoadOp     = LoadOp::Clear,
		.StoreOp    = StoreOp::DontCare,
		.ClearValue = { .depthStencil = { .depth = 1.0f, .stencil = 0 } },
		.Layout     = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL
	};

	const AttachmentInfo colorAttachments[] { colorAttachment };

	RenderPassInfo renderPassInfo
	{
		.ColorAttachments = colorAttachments,
		.DepthAttachment  = &depthAttachment,
		.RenderArea       = { .offset = { 0, 0 }, .extent = extent },
		.LayerCount       = 1
	};

	DynamicRendering::BeginRendering(res.commandBuffer, renderPassInfo);
	{
		VkViewport viewport{ .x = 0, .y = 0, .width = static_cast<float>(extent.width), .height = static_cast<float>(extent.height), .minDepth = 0.0f, .maxDepth = 1.0f };
		vkCmdSetViewport(res.commandBuffer, 0, 1, &viewport);

		VkRect2D scissor{ .offset = { 0, 0 }, .extent = extent };
		vkCmdSetScissor(res.commandBuffer, 0, 1, &scissor);

		m_Pipeline.Bind(res.commandBuffer);

		const VkBuffer     vertexBuffers[] = { m_VertexBuffer.GetBuffer() };
		const VkDeviceSize offsets[]       = { 0 };
		vkCmdBindVertexBuffers(res.commandBuffer, 0, 1, vertexBuffers, offsets);
		vkCmdBindIndexBuffer(res.commandBuffer, m_IndexBuffer.GetBuffer(), 0, VK_INDEX_TYPE_UINT32);
		vkCmdDrawIndexed(res.commandBuffer, m_IndexBuffer.GetCount(), 1, 0, 0, 0);
	}
	DynamicRendering::EndRendering(res.commandBuffer);

	SetImageLayout(
		res.commandBuffer,
		m_SwapChain.GetCurrentImage(),
		VK_IMAGE_ASPECT_COLOR_BIT,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
		VK_PIPELINE_STAGE_2_NONE);

	vkEndCommandBuffer(res.commandBuffer);

	const VkSemaphoreSubmitInfo signalInfos[]
	{
		{
			.sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
			.semaphore = m_Renderer.GetRenderFinishedSemaphore(imageIndex),
			.stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
		},
		{
			.sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
			.semaphore = m_Renderer.GetFrameTimeline(),
			.value     = m_Renderer.GetCurrentSignalValue(),
			.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
		},
	};

	const VkSemaphoreSubmitInfo acquireWait
	{
		.sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
		.semaphore = m_Renderer.GetImageAvailableSemaphore(),
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

	m_SwapChain.Present(m_Renderer.GetRenderFinishedSemaphore(imageIndex));

	m_Renderer.EndFrame();
}

void Application::InsertImageMemoryBarrier(
	VkCommandBuffer commandBuffer,
	VkImage image,
	VkAccessFlags2 srcAccessMask,
	VkAccessFlags2 dstAccessMask,
	VkImageLayout oldImageLayout,
	VkImageLayout newImageLayout,
	VkPipelineStageFlags2 srcStageMask,
	VkPipelineStageFlags2 dstStageMask,
	VkImageSubresourceRange subresourceRange)
{
	VkImageMemoryBarrier2 imageMemoryBarrier
	{
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,

		.srcStageMask  = srcStageMask,
		.srcAccessMask = srcAccessMask,

		.dstStageMask  = dstStageMask,
		.dstAccessMask = dstAccessMask,

		.oldLayout = oldImageLayout,
		.newLayout = newImageLayout,

		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,

		.image = image,

		.subresourceRange = subresourceRange
	};

	VkDependencyInfo dependencyInfo
	{
		.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,

		.imageMemoryBarrierCount = 1,
		.pImageMemoryBarriers    = &imageMemoryBarrier
	};

	vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
}

void Application::SetImageLayout(
	VkCommandBuffer commandBuffer,
	VkImage image,
	VkImageLayout oldImageLayout,
	VkImageLayout newImageLayout,
	VkImageSubresourceRange subresourceRange,
	VkPipelineStageFlags2 srcStageMask,
	VkPipelineStageFlags2 dstStageMask)
{
	VkAccessFlags2 srcAccessMask = 0;
	VkAccessFlags2 dstAccessMask = 0;

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Source Layout
	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	switch (oldImageLayout)
	{
		case VK_IMAGE_LAYOUT_UNDEFINED:
		{
			srcAccessMask = 0;
			break;
		}

		case VK_IMAGE_LAYOUT_PREINITIALIZED:
		{
			srcAccessMask = VK_ACCESS_2_HOST_WRITE_BIT;
			break;
		}

		case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
		{
			srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
			break;
		}

		case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
		case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL:
		{
			srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			break;
		}

		case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
		{
			srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
			break;
		}

		case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
		{
			srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
			break;
		}

		case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
		{
			srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
			break;
		}

		case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
		{
			srcAccessMask = 0;
			break;
		}

		default:
		{
			break;
		}
	}

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Destination Layout
	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	switch (newImageLayout)
	{
		case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
		{
			dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
			break;
		}

		case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
		{
			dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
			break;
		}

		case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
		{
			dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
			break;
		}

		case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
		case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL:
		{
			dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			break;
		}

		case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
		{
			if (srcAccessMask == 0)
			{
				srcAccessMask =
					VK_ACCESS_2_HOST_WRITE_BIT |
					VK_ACCESS_2_TRANSFER_WRITE_BIT;
			}

			dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
			break;
		}

		case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
		{
			dstAccessMask = 0;
			break;
		}

		default:
		{
			break;
		}
	}

	InsertImageMemoryBarrier(
		commandBuffer,
		image,
		srcAccessMask,
		dstAccessMask,
		oldImageLayout,
		newImageLayout,
		srcStageMask,
		dstStageMask,
		subresourceRange
	);
}

void Application::SetImageLayout(
	VkCommandBuffer commandBuffer,
	VkImage image,
	VkImageAspectFlags aspectMask,
	VkImageLayout oldImageLayout,
	VkImageLayout newImageLayout,
	VkPipelineStageFlags2 srcStageMask,
	VkPipelineStageFlags2 dstStageMask)
{
	VkImageSubresourceRange subresourceRange
	{
		.aspectMask     = aspectMask,
		.baseMipLevel   = 0,
		.levelCount     = 1,
		.baseArrayLayer = 0,
		.layerCount     = 1
	};

	SetImageLayout(
		commandBuffer,
		image,
		oldImageLayout,
		newImageLayout,
		subresourceRange,
		srcStageMask,
		dstStageMask
	);
}
