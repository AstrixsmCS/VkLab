#include "Application.hpp"

#include <SDL3/SDL.h>

#include <vma/vk_mem_alloc.h>

#include <backends/imgui_impl_sdl3.h>

#include <filesystem>

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
	Renderer::WaitForGPU();

	m_ToneMappingPipeline.Shutdown();
	m_SkyboxPipeline.Shutdown();
	m_EquirectangularToCubemapPipeline.Shutdown();

	if (m_ToneMappingMaterial.GetShader())
	{
		m_ToneMappingMaterial.GetShader()->Shutdown();
	}

	if (m_SkyboxMaterial.GetShader())
	{
		m_SkyboxMaterial.GetShader()->Shutdown();
	}

	if (m_EquirectangularToCubemapMaterial.GetShader())
	{
		m_EquirectangularToCubemapMaterial.GetShader()->Shutdown();
	}

	m_EnvironmentImage.Destroy();

	m_GeometryPipeline.Shutdown();

	if (m_GeometryMaterial.GetShader())
	{
		m_GeometryMaterial.GetShader()->Shutdown();
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

	DestroySceneImage();
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
	CreateSceneImage();

	const VkExtent2D extent = m_Renderer.GetSwapChain().GetExtent();
	const float aspectRatio = static_cast<float>(extent.width) / static_cast<float>(extent.height);
	m_Camera.SetPerspective(glm::radians(60.0f), aspectRatio, 0.1f, 1000.0f);
	m_Camera.SetPosition(glm::vec3(0.0f, 0.0f, 3.0f));

	for (UniformBuffer& cameraBuffer : m_CameraBuffers)
		cameraBuffer.Create(sizeof(UBCamera));

	if (!CreateGeometryPass())
	{
		ShowError("Unable to initialize the geometry pass.");
		return false;
	}

	if (!CreateSkyboxPass())
	{
		ShowError("Unable to initialize the skybox pass.");
		return false;
	}

	if (!CreateToneMappingPass())
	{
		ShowError("Unable to initialize the tone mapping pass.");
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

bool Application::CreateGeometryPass()
{
	auto shader = std::make_shared<Shader>();
	shader->Load("Resources/Shaders/Geometry.slang");

	if (!shader->IsValid())
		return false;

	m_GeometryMaterial.SetShader(shader);

	GraphicsPipelineSpecification spec;
	spec.DebugName    = "Mesh Pipeline";
	spec.Shader       = m_GeometryMaterial.GetShader();
	spec.ColorFormats = { Format::RGBA16_Float };
	spec.DepthFormat  = Format::D32_Float;
	spec.Layout       =
	{
		{ ShaderDataType::Float3, "Position" },
		{ ShaderDataType::Float3, "Normal"   },
		{ ShaderDataType::Float2, "TexCoord" },
		{ ShaderDataType::Float4, "Tangent"  },
	};

	m_GeometryPipeline.Create(spec);

	return m_GeometryPipeline.GetPipeline() != VK_NULL_HANDLE;
}

bool Application::CreateSkyboxPass()
{
	constexpr uint32_t cubemapSize = 512;
	const std::filesystem::path hdriPath = "Resources/EnvironmentMaps/GraaffReinetGrooteKerk.hdr";

	if (!std::filesystem::exists(hdriPath))
		return false;

	Texture equirectangularTexture;

	TextureSpecification textureSpecification;
	textureSpecification.DebugName = "Equirectangular HDRI";
	textureSpecification.Format = Format::RGBA32_Float;
	textureSpecification.GenerateMips = false;

	equirectangularTexture.Create(textureSpecification, hdriPath);

	ImageSpecification environmentSpecification
	{
		.DebugName = "Environment Cubemap",
		.Type = ImageType::Cube,
		.Format = Format::RGBA32_Float,
		.Usage = ImageUsage::Sampled | ImageUsage::Storage,
		.Width = cubemapSize,
		.Height = cubemapSize,
		.Depth = 1,
		.Mips = 1
	};

	m_EnvironmentImage.Create(environmentSpecification);

	auto computeShader = std::make_shared<Shader>();
	computeShader->Load("Resources/Shaders/EquirectangularToCubeMap.slang");

	if (!computeShader->IsValid())
		return false;

	m_EquirectangularToCubemapMaterial.SetShader(computeShader);

	ComputePipelineSpecification computeSpecification
	{
		.Shader = m_EquirectangularToCubemapMaterial.GetShader(),
		.DebugName = "Equirectangular To Cubemap Pipeline"
	};
	m_EquirectangularToCubemapPipeline.Create(computeSpecification);

	CommandPool& commandPool = RendererContext::Get().GetImmediateCommandPool();
	CommandBuffer commandBuffer = commandPool.AllocateCommandBuffer();
	commandBuffer.Begin(true);

	const VkImageSubresourceRange cubemapRange
	{
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.baseMipLevel = 0,
		.levelCount = 1,
		.baseArrayLayer = 0,
		.layerCount = m_EnvironmentImage.GetLayerCount()
	};

	InsertImageMemoryBarrier(
		commandBuffer.GetHandle(),
		m_EnvironmentImage.GetHandle(),
		VK_ACCESS_2_NONE,
		VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_GENERAL,
		VK_PIPELINE_STAGE_2_NONE,
		VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
		cubemapRange
	);

	m_EquirectangularToCubemapPipeline.Bind(commandBuffer.GetHandle());

	const VkDescriptorSet descriptorSet = Descriptor::GetSet();
	vkCmdBindDescriptorSets(commandBuffer.GetHandle(), VK_PIPELINE_BIND_POINT_COMPUTE, m_EquirectangularToCubemapPipeline.GetLayout(), 0, 1, &descriptorSet, 0, nullptr);

	m_EquirectangularToCubemapMaterial.Set("EquirectangularTexture", equirectangularTexture.GetTextureIndex());
	m_EquirectangularToCubemapMaterial.Set("OutputCubemap", m_EnvironmentImage.GetStorageIndex());
	m_EquirectangularToCubemapMaterial.Set("CubemapSize", cubemapSize);

	const auto& computeRanges = m_EquirectangularToCubemapMaterial.GetShader()->GetPushConstantRanges();
	assert(!computeRanges.empty());

	const auto& computeStorage = m_EquirectangularToCubemapMaterial.GetUniformStorage();
	assert(computeStorage.size() == computeRanges[0].Size);

	vkCmdPushConstants(commandBuffer.GetHandle(), m_EquirectangularToCubemapPipeline.GetLayout(), computeRanges[0].StageFlags, computeRanges[0].Offset, static_cast<uint32_t>(computeStorage.size()), computeStorage.data());

	vkCmdDispatch(commandBuffer.GetHandle(), (cubemapSize + 7) / 8, (cubemapSize + 7) / 8, 6);

	InsertImageMemoryBarrier(
		commandBuffer.GetHandle(),
		m_EnvironmentImage.GetHandle(),
		VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
		VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
		VK_IMAGE_LAYOUT_GENERAL,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
		VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
		cubemapRange
	);

	commandBuffer.Flush();
	commandPool.Reset();

	equirectangularTexture.Destroy();

	auto shader = std::make_shared<Shader>();
	shader->Load("Resources/Shaders/Skybox.slang");

	if (!shader->IsValid())
		return false;

	m_SkyboxMaterial.SetShader(shader);

	GraphicsPipelineSpecification skyboxSpecification;
	skyboxSpecification.DebugName = "Skybox Pipeline";
	skyboxSpecification.Shader = m_SkyboxMaterial.GetShader();
	skyboxSpecification.ColorFormats = { Format::RGBA16_Float };
	skyboxSpecification.DepthFormat = Format::D32_Float;
	skyboxSpecification.BackfaceCulling = false;
	skyboxSpecification.DepthTest = false;
	skyboxSpecification.DepthWrite = false;

	m_SkyboxPipeline.Create(skyboxSpecification);

	return m_SkyboxPipeline.GetPipeline() != VK_NULL_HANDLE;
}

void Application::RenderSkyboxPass(VkCommandBuffer cmd, const UniformBuffer& cameraBuffer)
{
	const VkDescriptorSet descriptorSet = Descriptor::GetSet();

	m_SkyboxPipeline.Bind(cmd);

	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_SkyboxPipeline.GetLayout(), 0, 1, &descriptorSet, 0, nullptr);

	m_SkyboxMaterial.Set("UBCamera", cameraBuffer.GetDeviceAddress());
	m_SkyboxMaterial.Set("EnvironmentTexture", m_EnvironmentImage.GetSampledIndex());
	m_SkyboxMaterial.Set("TextureLod", 0.0f);
	m_SkyboxMaterial.Set("Intensity", 1.0f);

	const auto& ranges = m_SkyboxMaterial.GetShader()->GetPushConstantRanges();
	assert(!ranges.empty());

	const auto& storage = m_SkyboxMaterial.GetUniformStorage();
	assert(storage.size() == ranges[0].Size);

	vkCmdPushConstants(cmd, m_SkyboxPipeline.GetLayout(), ranges[0].StageFlags, ranges[0].Offset, static_cast<uint32_t>(storage.size()), storage.data());

	vkCmdDraw(cmd, 3, 1, 0, 0);
}

void Application::RenderGeometryPass(VkCommandBuffer cmd, const UniformBuffer& cameraBuffer)
{
	const VkDescriptorSet descriptorSet = Descriptor::GetSet();

	m_GeometryPipeline.Bind(cmd);

	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_GeometryPipeline.GetLayout(), 0, 1, &descriptorSet, 0, nullptr);

	const auto& ranges = m_GeometryMaterial.GetShader()->GetPushConstantRanges();
	assert(!ranges.empty());

	const auto& range = ranges[0];

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

			uint32_t materialSlot = 0;

			if (submesh.MaterialIndex != UINT32_MAX && submesh.MaterialIndex < m_MaterialIndices.size())
			{
				materialSlot = m_MaterialIndices[submesh.MaterialIndex];
			}

			m_GeometryMaterial.Set("Model", modelScale * node.WorldTransform);

			m_GeometryMaterial.Set("UBCamera", cameraBuffer.GetDeviceAddress());

			m_GeometryMaterial.Set("SBMaterials", MaterialSystem::GetBuffer().GetDeviceAddress());
			m_GeometryMaterial.Set("MaterialIndex", materialSlot);

			m_GeometryMaterial.Set("LightDirection", glm::vec4(glm::normalize(m_DirectionalLight.Direction), 0.0f));
			m_GeometryMaterial.Set("LightColorIntensity", glm::vec4(m_DirectionalLight.Color, m_DirectionalLight.Intensity));

			const auto& storage = m_GeometryMaterial.GetUniformStorage();
			assert(storage.size() == range.Size);

			vkCmdPushConstants(cmd, m_GeometryPipeline.GetLayout(), range.StageFlags, range.Offset, static_cast<uint32_t>(storage.size()), storage.data());

			vkCmdDrawIndexed(cmd, submesh.IndexCount, 1, submesh.BaseIndex, static_cast<int32_t>(submesh.BaseVertex), 0);
		}
	});
}

bool Application::CreateToneMappingPass()
{
	auto shader = std::make_shared<Shader>();
	shader->Load("Resources/Shaders/Tonemapping.slang");

	if (!shader->IsValid())
		return false;

	m_ToneMappingMaterial.SetShader(shader);

	GraphicsPipelineSpecification specification;
	specification.DebugName = "Tone Mapping Pipeline";
	specification.Shader = m_ToneMappingMaterial.GetShader();
	specification.ColorFormats = { Format::BGRA8_UNorm };
	specification.BackfaceCulling = false;
	specification.DepthTest = false;
	specification.DepthWrite = false;

	m_ToneMappingPipeline.Create(specification);

	return m_ToneMappingPipeline.GetPipeline() != VK_NULL_HANDLE;
}

void Application::RenderToneMappingPass(VkCommandBuffer cmd, const VkExtent2D& extent)
{
	const VkDescriptorSet descriptorSet = Descriptor::GetSet();

	m_ToneMappingPipeline.Bind(cmd);

	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_ToneMappingPipeline.GetLayout(), 0, 1, &descriptorSet, 0, nullptr);

	m_ToneMappingMaterial.Set("SceneTexture", m_SceneImage.GetSampledIndex());
	m_ToneMappingMaterial.Set("ToneMapper", static_cast<uint32_t>(m_ToneMapper));
	m_ToneMappingMaterial.Set("Exposure", m_Exposure);
	m_ToneMappingMaterial.Set("WhitePoint", m_WhitePoint);

	const auto& ranges = m_ToneMappingMaterial.GetShader()->GetPushConstantRanges();
	assert(!ranges.empty());

	const auto& storage = m_ToneMappingMaterial.GetUniformStorage();
	assert(storage.size() == ranges[0].Size);

	vkCmdPushConstants(cmd, m_ToneMappingPipeline.GetLayout(), ranges[0].StageFlags, ranges[0].Offset, static_cast<uint32_t>(storage.size()), storage.data());

	VkViewport viewport{ .x = 0, .y = 0, .width = static_cast<float>(extent.width), .height = static_cast<float>(extent.height), .minDepth = 0.0f, .maxDepth = 1.0f };
	vkCmdSetViewport(cmd, 0, 1, &viewport);

	VkRect2D scissor{ .offset = { 0, 0 }, .extent = extent };
	vkCmdSetScissor(cmd, 0, 1, &scissor);

	vkCmdDraw(cmd, 3, 1, 0, 0);
}

bool Application::LoadMesh()
{
	return m_Mesh.Load("Resources/Meshes/FlightHelmet/FlightHelmet.gltf");
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
		.Type = ImageType::Image2D,
		.Format = Format::D32_Float,
		.Usage = ImageUsage::Attachment,
		.Width = extent.width,
		.Height = extent.height,
		.Depth = 1,
		.Mips = 1
	};

	m_DepthImage.Create(specification);
}

void Application::DestroyDepthImage()
{
	m_DepthImage.Destroy();
}

void Application::CreateSceneImage()
{
	SwapChain& swapChain = m_Renderer.GetSwapChain();
	const VkExtent2D extent = swapChain.GetExtent();

	ImageSpecification specification
	{
		.DebugName = "HDR Scene Image",
		.Type = ImageType::Image2D,
		.Format = Format::RGBA16_Float,
		.Usage = ImageUsage::Attachment | ImageUsage::Sampled,
		.Width = extent.width,
		.Height = extent.height,
		.Depth = 1,
		.Mips = 1
	};

	m_SceneImage.Create(specification);
}

void Application::DestroySceneImage()
{
	m_SceneImage.Destroy();
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

	const glm::mat4 viewProjection = m_Camera.GetViewProjection();

	const UBCamera cameraData
	{
		.ViewProjection = viewProjection,
		.InverseViewProjection = glm::inverse(viewProjection),
		.CameraPosition = m_Camera.GetPosition(),
	};
	cameraBuffer.SetData(&cameraData, sizeof(UBCamera));

	SetImageLayout(
		cmd,
		m_SceneImage.GetHandle(),
		VK_IMAGE_ASPECT_COLOR_BIT,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_PIPELINE_STAGE_2_NONE,
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
		.ImageView  = m_SceneImage.GetView(),
		.Format     = Format::RGBA16_Float,
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

		RenderSkyboxPass(cmd, cameraBuffer);
		RenderGeometryPass(cmd, cameraBuffer);
	}
	DynamicRendering::EndRendering(cmd);

	SetImageLayout(
		cmd,
		m_SceneImage.GetHandle(),
		VK_IMAGE_ASPECT_COLOR_BIT,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
		VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);

	SetImageLayout(
		cmd,
		swapChain.GetCurrentImage(),
		VK_IMAGE_ASPECT_COLOR_BIT,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_PIPELINE_STAGE_2_NONE,
		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);

	AttachmentInfo toneMappingColorAttachment
	{
		.ImageView  = swapChain.GetCurrentImageView(),
		.Format     = Format::BGRA8_UNorm,
		.LoadOp     = LoadOp::Clear,
		.StoreOp    = StoreOp::Store,
		.ClearValue = { .color = { .float32 = { 0.0f, 0.0f, 0.0f, 1.0f } } },
		.Layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
	};

	const AttachmentInfo toneMappingColorAttachments[] { toneMappingColorAttachment };

	RenderPassInfo toneMappingRenderPassInfo
	{
		.ColorAttachments = toneMappingColorAttachments,
		.DepthAttachment  = nullptr,
		.RenderArea       = { .offset = { 0, 0 }, .extent = extent },
		.LayerCount       = 1
	};

	DynamicRendering::BeginRendering(cmd, toneMappingRenderPassInfo);
	{
		RenderToneMappingPass(cmd, extent);
	}
	DynamicRendering::EndRendering(cmd);

	m_ImGui.Begin();

	ImGui::Begin("Renderer");
	const ImGuiIO& io = ImGui::GetIO();
	ImGui::Text("FPS: %.1f", io.Framerate);
	ImGui::Text("Frame time: %.3f ms", io.Framerate > 0.0f ? 1000.0f / io.Framerate : 0.0f);

	ImGui::SeparatorText("Directional Light");
	ImGui::DragFloat3("Direction", &m_DirectionalLight.Direction.x, 0.01f, -1.0f, 1.0f);
	ImGui::ColorEdit3("Color", &m_DirectionalLight.Color.x);
	ImGui::DragFloat("Intensity", &m_DirectionalLight.Intensity, 0.1f, 0.0f, 20.0f);

	ImGui::SeparatorText("Tone Mapping");
	const char* toneMapperNames[]
	{
		"Extended Reinhard",
		"ACES",
		"Uncharted 2"
	};
	int toneMapper = static_cast<int>(m_ToneMapper);
	if (ImGui::Combo("Tone Mapper", &toneMapper, toneMapperNames, static_cast<int>(std::size(toneMapperNames))))
		m_ToneMapper = static_cast<ToneMapper>(toneMapper);
	ImGui::DragFloat("Exposure", &m_Exposure, 0.01f, 0.01f, 10.0f);
	if (m_ToneMapper == ToneMapper::Reinhard)
		ImGui::DragFloat("White Point", &m_WhitePoint, 0.1f, 0.1f, 32.0f);
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
