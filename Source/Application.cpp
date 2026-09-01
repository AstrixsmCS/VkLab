#include "Application.hpp"

#include <SDL3/SDL.h>

#include <vma/vk_mem_alloc.h>

#include <backends/imgui_impl_sdl3.h>

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

	// Unregister materials before destroying the mesh so textures
	// are still alive when the slots are cleared.
	for (uint32_t index : m_MaterialIndices)
		MaterialSystem::UnregisterMaterial(index);
	m_MaterialIndices.clear();

	m_Mesh.Destroy();

	MaterialSystem::Shutdown();

	for (UniformBuffer& cameraBuffer : m_CameraBuffers)
		cameraBuffer.Destroy();

	DestroyDepthImage();

	DestroyDefaultSamplers();

	Descriptor::Shutdown();

	Allocator::Shutdown();

	m_ImGui.Shutdown();

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
			ImGui_ImplSDL3_ProcessEvent(&event);

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

	m_ImGui.Initialize();

	Allocator::Initialize();

	Descriptor::Initialize();

	CreateDefaultSamplers();

	MaterialSystem::Initialize();

	CreateDepthImage();

	const VkExtent2D extent = m_Renderer.GetSwapChain().GetExtent();
	const float aspectRatio = static_cast<float>(extent.width) / static_cast<float>(extent.height);
	m_Camera.SetPerspective(glm::radians(60.0f), aspectRatio, 0.1f, 1000.0f);
	m_Camera.SetPosition(glm::vec3(0.0f, 0.0f, 3.0f));

	for (UniformBuffer& cameraBuffer : m_CameraBuffers)
		cameraBuffer.Create(sizeof(CameraData));

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

	// Register every mesh material with the material system.
	const auto& materials = m_Mesh.GetMaterials();
	m_MaterialIndices.reserve(materials.size());

	for (const Material& material : materials)
	{
		auto mat = std::make_shared<Material>(material);
		const uint32_t index = MaterialSystem::RegisterMaterial(mat);
		m_MaterialIndices.push_back(index);
	}

	// Flush dirty materials to the GPU buffer before the first frame.
	MaterialSystem::Update();

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
	return m_Mesh.Load("Resources/Meshes/Sponza/Sponza.gltf");
}

void Application::CreateDefaultSamplers()
{
	auto CreateSampler = [this](DefaultSampler type, const SamplerSpecification& specification)
	{
		auto sampler = std::make_shared<Sampler>();

		sampler->Create(specification);

		m_DefaultSamplers[static_cast<size_t>(type)] = std::move(sampler);
	};

	CreateSampler(
		DefaultSampler::LinearRepeat,
		{
			.DebugName = "Linear Repeat Sampler",

			.MinFilter = SamplerFilter::Linear,
			.MagFilter = SamplerFilter::Linear,
			.Mip = SamplerMip::Linear,

			.WrapU = SamplerWrap::Repeat,
			.WrapV = SamplerWrap::Repeat,
			.WrapW = SamplerWrap::Repeat
		}
	);

	CreateSampler(
		DefaultSampler::LinearClamp,
		{
			.DebugName = "Linear Clamp Sampler",

			.MinFilter = SamplerFilter::Linear,
			.MagFilter = SamplerFilter::Linear,
			.Mip = SamplerMip::Linear,

			.WrapU = SamplerWrap::ClampToEdge,
			.WrapV = SamplerWrap::ClampToEdge,
			.WrapW = SamplerWrap::ClampToEdge
		}
	);

	CreateSampler(
		DefaultSampler::NearestClamp,
		{
			.DebugName = "Nearest Clamp Sampler",

			.MinFilter = SamplerFilter::Nearest,
			.MagFilter = SamplerFilter::Nearest,
			.Mip = SamplerMip::Disabled,

			.WrapU = SamplerWrap::ClampToEdge,
			.WrapV = SamplerWrap::ClampToEdge,
			.WrapW = SamplerWrap::ClampToEdge
		}
	);

	CreateSampler(
		DefaultSampler::AnisotropicRepeat,
		{
			.DebugName = "Anisotropic Repeat Sampler",

			.MinFilter = SamplerFilter::Linear,
			.MagFilter = SamplerFilter::Linear,
			.Mip = SamplerMip::Linear,

			.WrapU = SamplerWrap::Repeat,
			.WrapV = SamplerWrap::Repeat,
			.WrapW = SamplerWrap::Repeat,

			.Anisotropy = true,
			.MaxAnisotropy = 8.0f
		}
	);

	CreateSampler(
		DefaultSampler::ShadowCompare,
		{
			.DebugName = "Shadow Compare Sampler",

			.MinFilter = SamplerFilter::Linear,
			.MagFilter = SamplerFilter::Linear,
			.Mip = SamplerMip::Disabled,

			.WrapU = SamplerWrap::ClampToEdge,
			.WrapV = SamplerWrap::ClampToEdge,
			.WrapW = SamplerWrap::ClampToEdge,

			.Compare = true,
			.CompareOperation = CompareOp::LessEqual
		}
	);
}

void Application::DestroyDefaultSamplers()
{
	for (auto& sampler : m_DefaultSamplers)
	{
		if (!sampler)
			continue;

		sampler->Destroy();
		sampler.reset();
	}
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

	const uint32_t frameIndex = m_Renderer.GetCurrentFrameIndex();
	UniformBuffer& cameraBuffer = m_CameraBuffers[frameIndex];

	// Flush any material changes to the GPU buffer before drawing.
	MaterialSystem::Update();

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
	cameraBuffer.SetData(&cameraData, sizeof(CameraData));

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
		m_DepthImage.GetHandle(),
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

		VkDescriptorSet descriptorSet = Descriptor::GetSet();
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline.GetLayout(), 0, 1, &descriptorSet, 0, nullptr);

		const auto& ranges = m_Shader->GetPushConstantRanges();
		assert(!ranges.empty());
		const auto& range = ranges[0];
		assert(range.Size == sizeof(PushConstants));

		const VkBuffer vertexBuffer = m_Mesh.GetVertexBuffer();
		const VkDeviceSize offset = 0;
		vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, &offset);
		vkCmdBindIndexBuffer(cmd, m_Mesh.GetIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);

		const auto& submeshes = m_Mesh.GetSubmeshes();
		const glm::mat4 modelScale = glm::scale(glm::mat4(1.0f), glm::vec3(1.0f));

		m_Mesh.TraverseNodes([&](const Node& node)
		{
			for (uint32_t submeshIndex : node.Submeshes)
			{
				assert(submeshIndex < submeshes.size());
				const Submesh& submesh = submeshes[submeshIndex];

				// Map the mesh material index to the material system slot index.
				uint32_t materialSlot = 0;
				if (submesh.MaterialIndex != UINT32_MAX && submesh.MaterialIndex < m_MaterialIndices.size())
				{
					materialSlot = m_MaterialIndices[submesh.MaterialIndex];
				}

				PushConstants pushConstants
				{
					.Model          = modelScale * node.WorldTransform,

					.Camera         = cameraBuffer.GetDeviceAddress(),

					.MaterialBuffer = MaterialSystem::GetBuffer().GetDeviceAddress(),
					.MaterialIndex  = materialSlot,

					.LightDirection = glm::vec4(glm::normalize(m_DirectionalLight.Direction), 0.0f),
					.LightColorIntensity = glm::vec4(m_DirectionalLight.Color, m_DirectionalLight.Intensity),
				};

				vkCmdPushConstants(cmd, m_Pipeline.GetLayout(), range.StageFlags, range.Offset, sizeof(PushConstants), &pushConstants);

				vkCmdDrawIndexed(cmd, submesh.IndexCount, 1, submesh.BaseIndex, static_cast<int32_t>(submesh.BaseVertex), 0);
			}
		});
	}
	DynamicRendering::EndRendering(cmd);

	m_ImGui.Begin();

	ImGui::Begin("Directional Light");
	ImGui::DragFloat3("Direction", &m_DirectionalLight.Direction.x, 0.01f, -1.0f, 1.0f);
	ImGui::ColorEdit3("Color", &m_DirectionalLight.Color.x);
	ImGui::DragFloat("Intensity", &m_DirectionalLight.Intensity, 0.1f, 0.0f, 20.0f);
	ImGui::End();

	AttachmentInfo imguiColorAttachment
	{
		.ImageView  = swapChain.GetCurrentImageView(),
		.Format     = Format::BGRA8_UNorm,
		.LoadOp     = LoadOp::Load,
		.StoreOp    = StoreOp::Store,
		.Layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
	};

	const AttachmentInfo imguiColorAttachments[] { imguiColorAttachment };

	RenderPassInfo imguiRenderPassInfo
	{
		.ColorAttachments = imguiColorAttachments,
		.DepthAttachment  = nullptr,
		.RenderArea       = { .offset = { 0, 0 }, .extent = extent },
		.LayerCount       = 1
	};

	DynamicRendering::BeginRendering(cmd, imguiRenderPassInfo);
	{
		m_ImGui.End(m_Renderer.GetFrameCommandBuffer());
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
