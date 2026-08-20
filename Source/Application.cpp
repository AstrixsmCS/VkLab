#include "Application.hpp"

#include <SDL3/SDL.h>

#include <vma/vk_mem_alloc.h>

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
	vkDeviceWaitIdle(RendererContext::Get().GetDevice());

	m_Pipeline.Shutdown();

	if (m_Shader)
	{
		m_Shader->Shutdown();
		m_Shader.reset();
	}

	m_IndexBuffer.Destroy();
	m_VertexBuffer.Destroy();

	DestroyDepthImage();

	Allocator::Shutdown();

	m_Renderer.Shutdown();

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
	m_Renderer.Initialize(m_Window);

	Allocator::Initialize();

	CreateDepthImage();

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
	spec.DebugName            = "Quad Pipeline";

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

void Application::CreateDepthImage()
{
	VkDevice device = RendererContext::Get().GetDevice();

	SwapChain& swapChain = m_Renderer.GetSwapChain();
	const VkExtent2D extent = swapChain.GetExtent();

	VkImageCreateInfo depthImageInfo
	{
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = VK_FORMAT_D32_SFLOAT,
		.extent =
		{
			.width = extent.width,
			.height = extent.height,
			.depth = 1
		},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
	};

	VmaAllocationCreateInfo allocationInfo
	{
		.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
	};

	VK_CHECK(vmaCreateImage(Allocator::GetAllocator(), &depthImageInfo, &allocationInfo, &m_DepthStencil.Image, &m_DepthStencil.Allocation, nullptr));

	VkImageViewCreateInfo depthViewInfo
	{
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = m_DepthStencil.Image,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = VK_FORMAT_D32_SFLOAT,
		.subresourceRange =
		{
			.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1
		}
	};

	VK_CHECK(vkCreateImageView(device, &depthViewInfo, nullptr, &m_DepthStencil.ImageView));
}

void Application::DestroyDepthImage()
{
	VkDevice device = RendererContext::Get().GetDevice();

	if (m_DepthStencil.ImageView)
	{
		vkDestroyImageView(device, m_DepthStencil.ImageView, nullptr);

		m_DepthStencil.ImageView = VK_NULL_HANDLE;
	}

	if (m_DepthStencil.Image)
	{
		vmaDestroyImage(Allocator::GetAllocator(), m_DepthStencil.Image, m_DepthStencil.Allocation);

		m_DepthStencil.Image = VK_NULL_HANDLE;
		m_DepthStencil.Allocation = VK_NULL_HANDLE;
	}
}

void Application::Render()
{
	if (!m_Renderer.BeginFrame())
		return;

	VkCommandBuffer cmd = m_Renderer.GetCurrentCommandBuffer();

	SwapChain& swapChain = m_Renderer.GetSwapChain();
	const VkExtent2D extent = swapChain.GetExtent();

	SetImageLayout(
		cmd,
		swapChain.GetCurrentImage(),
		VK_IMAGE_ASPECT_COLOR_BIT,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);

	SetImageLayout(
		cmd,
		m_DepthStencil.Image,
		VK_IMAGE_ASPECT_DEPTH_BIT,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
		VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
		VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT);

	AttachmentInfo colorAttachment
	{
		.ImageView  = swapChain.GetCurrentImageView(),
		.Format     = Format::BGRA8_UNorm,
		.LoadOp     = LoadOp::Clear,
		.StoreOp    = StoreOp::Store,
		.ClearValue = { .color = { .float32 = { 0.1f, 0.1f, 0.1f, 1.0f } } },
		.Layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
	};

	AttachmentInfo depthAttachment
	{
		.ImageView  = m_DepthStencil.ImageView,
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

	DynamicRendering::BeginRendering(cmd, renderPassInfo);
	{
		VkViewport viewport{ .x = 0, .y = 0, .width = static_cast<float>(extent.width), .height = static_cast<float>(extent.height), .minDepth = 0.0f, .maxDepth = 1.0f };
		vkCmdSetViewport(cmd, 0, 1, &viewport);

		VkRect2D scissor{ .offset = { 0, 0 }, .extent = extent };
		vkCmdSetScissor(cmd, 0, 1, &scissor);

		m_Pipeline.Bind(cmd);

		const VkBuffer     vertexBuffers[] = { m_VertexBuffer.GetBuffer() };
		const VkDeviceSize offsets[]       = { 0 };
		vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
		vkCmdBindIndexBuffer(cmd, m_IndexBuffer.GetBuffer(), 0, VK_INDEX_TYPE_UINT32);
		vkCmdDrawIndexed(cmd, m_IndexBuffer.GetCount(), 1, 0, 0, 0);
	}
	DynamicRendering::EndRendering(cmd);

	SetImageLayout(
		cmd,
		swapChain.GetCurrentImage(),
		VK_IMAGE_ASPECT_COLOR_BIT,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
		VK_PIPELINE_STAGE_2_NONE);

	m_Renderer.EndFrame();
}
