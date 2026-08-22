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

	m_Mesh.Destroy();

	m_CameraBuffer.Destroy();

	DestroyDepthImage();

	Descriptor::Shutdown();

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

	uint64_t previousCounter = SDL_GetPerformanceCounter(); //TODO: Move TimeStep stuff out of here

	while (m_Running)
	{
		const uint64_t currentCounter = SDL_GetPerformanceCounter();
		const float deltaTime = static_cast<float>(currentCounter - previousCounter) / static_cast<float>(SDL_GetPerformanceFrequency());
		previousCounter = currentCounter;

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

		m_Camera.OnUpdate(deltaTime);

		Render();
	}
}

bool Application::InitializeVulkan()
{
	m_Renderer.Initialize(m_Window);

	Allocator::Initialize();

	Descriptor::Initialize();

	CreateDepthImage();

	const VkExtent2D extent = m_Renderer.GetSwapChain().GetExtent();
	const float aspectRatio = static_cast<float>(extent.width) / static_cast<float>(extent.height);
	m_Camera.SetPerspective(glm::radians(60.0f), aspectRatio, 0.1f, 1000.0f);
	m_Camera.SetPosition(glm::vec3(0.0f, 0.0f, 3.0f));

	m_CameraBuffer.Create(sizeof(CameraData));

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

	if (!LoadMesh())
	{
		ShowError("Unable to load mesh.");
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
	spec.Shader         = m_Shader;
	spec.ColorFormats   = { Format::BGRA8_UNorm };
	spec.DepthFormat    = Format::D32_Float;
	spec.DepthTest      = true;
	spec.DepthWrite     = true;
	spec.DepthCompareOp = CompareOp::Less;
	spec.CullMode       = CullMode::Back;
	spec.FrontFace      = WindingMode::CCW;
	spec.Topology       = Topology::Triangle;
	spec.PolygonMode    = PolygonMode::Fill;
	spec.BlendEnabled   = false;
	spec.Layout         =
	{
		{ ShaderDataType::Float3, "Position" },
		{ ShaderDataType::Float3, "Normal"   },
		{ ShaderDataType::Float2, "TexCoord" },
		{ ShaderDataType::Float4, "Tangent"  },
	};
	spec.DebugName = "Mesh Pipeline";

	m_Pipeline.Create(spec);

	return m_Pipeline.GetPipeline() != VK_NULL_HANDLE;
}

bool Application::LoadMesh()
{
	return m_Mesh.Load("Resources/Meshes/DamagedHelmet/DamagedHelmet.glb");
}

void Application::CreateDepthImage()
{
	SwapChain& swapChain = m_Renderer.GetSwapChain();
	const VkExtent2D extent = swapChain.GetExtent();

	ImageSpecification specification
	{
		.DebugName = "Depth Image",
		.Format = Format::D32_Float,
		.Usage = ImageUsage::Attachment,
		.Width = extent.width,
		.Height = extent.height,
		.Mips = 1,
		.Layers = 1,
		.Transfer = false
	};

	m_DepthImage.Create(specification);
}

void Application::DestroyDepthImage()
{
	m_DepthImage.Destroy();
}

void Application::Render()
{
	if (!m_Renderer.BeginFrame())
		return;

	VkCommandBuffer cmd = m_Renderer.GetCurrentCommandBuffer();

	SwapChain& swapChain = m_Renderer.GetSwapChain();
	const VkExtent2D extent = swapChain.GetExtent();

	const float aspectRatio = static_cast<float>(extent.width) / static_cast<float>(extent.height);
	m_Camera.SetPerspective(glm::radians(60.0f), aspectRatio, 0.1f, 1000.0f);

	const CameraData cameraData
	{
		.ViewProjection = m_Camera.GetViewProjection(),
		.CameraPosition = m_Camera.GetPosition(),
	};

	m_CameraBuffer.SetData(&cameraData, sizeof(CameraData));

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
		m_DepthImage.GetImage(),
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
		.ImageView  = m_DepthImage.GetView(),
		.Format     = m_DepthImage.GetFormat(),
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

		const VkDescriptorSet bindlessSet = Descriptor::GetSet();
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline.GetLayout(), 0, 1, &bindlessSet, 0, nullptr);

		const auto& ranges = m_Shader->GetPushConstantRanges();
		assert(!ranges.empty());
		const auto& range = ranges[0];
		assert(range.Size == sizeof(PushConstants));

		const VkBuffer vertexBuffer = m_Mesh.GetVertexBuffer();
		const VkDeviceSize offset = 0;
		vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, &offset);
		vkCmdBindIndexBuffer(cmd, m_Mesh.GetIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);

		const auto& submeshes = m_Mesh.GetSubmeshes();
		const auto& materials = m_Mesh.GetMaterials();

		const glm::mat4 modelScale = glm::scale(glm::mat4(1.0f), glm::vec3(1.0f));

		m_Mesh.TraverseNodes(
		[&](const Node& node)
		{
			for (uint32_t submeshIndex : node.Submeshes)
			{
				assert(submeshIndex < submeshes.size());

				const Submesh& submesh = submeshes[submeshIndex];

				uint32_t textureIndex = 0;
				uint32_t samplerIndex = 0;

				if (submesh.MaterialIndex != UINT32_MAX)
				{
					assert(submesh.MaterialIndex < materials.size());

					const Material& material = materials[submesh.MaterialIndex];

					textureIndex = material.BaseColorTexture;
					samplerIndex = material.BaseColorSampler;
				}

				PushConstants pushConstants
				{
					.Model = modelScale * node.WorldTransform,
					.Camera = m_CameraBuffer.GetDeviceAddress(),
					.TextureIndex = textureIndex,
					.SamplerIndex = samplerIndex
				};

				vkCmdPushConstants(cmd, m_Pipeline.GetLayout(), range.StageFlags, range.Offset, sizeof(PushConstants), &pushConstants);

				vkCmdDrawIndexed(cmd, submesh.IndexCount, 1, submesh.BaseIndex, static_cast<int32_t>(submesh.BaseVertex), 0);
			}
		});
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
