#include "Renderer.hpp"

#include "RendererContext.hpp"

#include "ComputePipeline.hpp"

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

std::pair<std::shared_ptr<Texture>, std::shared_ptr<Texture>> Renderer::CreateEnvironmentMap(const std::string& filepath)
{
	constexpr uint32_t cubemapSize = 1024;
	constexpr uint32_t irradianceSize = 32;
	constexpr uint32_t sampleCount = 512;

	if (!std::filesystem::exists(filepath))
		return {};

	// Load equirectangular HDRI
	Texture equirectangularTexture;

	TextureSpecification equirectangularSpecification;
	equirectangularSpecification.DebugName = "Equirectangular HDRI";
	equirectangularSpecification.Format = Format::RGBA32_Float;
	equirectangularSpecification.GenerateMips = false;

	equirectangularTexture.Create(equirectangularSpecification, filepath);

	// Create unfiltered environment cubemap
	Texture unfilteredEnvironment;

	TextureSpecification unfilteredSpecification;
	unfilteredSpecification.DebugName = "Unfiltered Environment";
	unfilteredSpecification.Type = ImageType::Cube;
	unfilteredSpecification.Format = Format::RGBA16_Float;
	unfilteredSpecification.Usage = ImageUsage::Sampled | ImageUsage::Storage;
	unfilteredSpecification.Width = cubemapSize;
	unfilteredSpecification.Height = cubemapSize;
	unfilteredSpecification.GenerateMips = true;

	unfilteredEnvironment.Create(unfilteredSpecification);

	// Create prefiltered radiance map
	auto radianceMap = std::make_shared<Texture>();

	TextureSpecification radianceSpecification;
	radianceSpecification.DebugName = "Environment Radiance";
	radianceSpecification.Type = ImageType::Cube;
	radianceSpecification.Format = Format::RGBA16_Float;
	radianceSpecification.Usage = ImageUsage::Sampled | ImageUsage::Storage;
	radianceSpecification.Width = cubemapSize;
	radianceSpecification.Height = cubemapSize;
	radianceSpecification.GenerateMips = true;

	radianceMap->Create(radianceSpecification);

	// Create irradiance map
	auto irradianceMap = std::make_shared<Texture>();

	TextureSpecification irradianceSpecification;
	irradianceSpecification.DebugName = "Environment Irradiance";
	irradianceSpecification.Type = ImageType::Cube;
	irradianceSpecification.Format = Format::RGBA16_Float;
	irradianceSpecification.Usage = ImageUsage::Sampled | ImageUsage::Storage;
	irradianceSpecification.Width = irradianceSize;
	irradianceSpecification.Height = irradianceSize;
	irradianceSpecification.GenerateMips = false;

	irradianceMap->Create(irradianceSpecification);

	// Load compute shaders
	auto equirectangularShader = std::make_shared<Shader>();
	equirectangularShader->Load("Resources/Shaders/EquirectangularToCubeMap.slang");
	if (!equirectangularShader->IsValid())
		return {};

	auto prefilteredShader = std::make_shared<Shader>();
	prefilteredShader->Load("Resources/Shaders/EnvironmentPrefiltered.slang");
	if (!prefilteredShader->IsValid())
		return {};

	auto irradianceShader = std::make_shared<Shader>();
	irradianceShader->Load("Resources/Shaders/EnvironmentIrradiance.slang");
	if (!irradianceShader->IsValid())
		return {};

	// Create local materials
	Material equirectangularMaterial;
	Material prefilteredMaterial;
	Material irradianceMaterial;

	equirectangularMaterial.SetShader(equirectangularShader);
	prefilteredMaterial.SetShader(prefilteredShader);
	irradianceMaterial.SetShader(irradianceShader);

	// Create local compute pipelines
	ComputePipeline equirectangularPipeline;
	ComputePipeline prefilteredPipeline;
	ComputePipeline irradiancePipeline;

	{
		ComputePipelineSpecification specification
		{
			.Shader = equirectangularMaterial.GetShader(),
			.DebugName = "Equirectangular To Cubemap Pipeline"
		};

		equirectangularPipeline.Create(specification);
	}

	{
		ComputePipelineSpecification specification
		{
			.Shader = prefilteredMaterial.GetShader(),
			.DebugName = "Environment Prefiltered Pipeline"
		};

		prefilteredPipeline.Create(specification);
	}

	{
		ComputePipelineSpecification specification
		{
			.Shader = irradianceMaterial.GetShader(),
			.DebugName = "Environment Irradiance Pipeline"
		};

		irradiancePipeline.Create(specification);
	}

	CommandPool& commandPool = RendererContext::Get().GetImmediateCommandPool();

	// Equirectangular -> unfiltered cubemap
	{
		CommandBuffer commandBuffer = commandPool.AllocateCommandBuffer();
		commandBuffer.Begin(true);

		const VkImageSubresourceRange cubemapRange
		{
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 6
		};

		InsertImageMemoryBarrier(
			commandBuffer.GetHandle(),
			unfilteredEnvironment.GetImage().GetHandle(),
			VK_ACCESS_2_NONE,
			VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_PIPELINE_STAGE_2_NONE,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
			cubemapRange
		);

		equirectangularPipeline.Bind(commandBuffer.GetHandle());

		const VkDescriptorSet descriptorSet = Descriptor::GetSet();

		vkCmdBindDescriptorSets(commandBuffer.GetHandle(), VK_PIPELINE_BIND_POINT_COMPUTE, equirectangularPipeline.GetLayout(), 0, 1, &descriptorSet, 0, nullptr);

		equirectangularMaterial.Set("EquirectangularTexture", equirectangularTexture.GetTextureIndex());
		equirectangularMaterial.Set("OutputCubemap", unfilteredEnvironment.GetImage().GetStorageIndex());
		equirectangularMaterial.Set("CubemapSize", cubemapSize);

		const auto& ranges = equirectangularMaterial.GetShader()->GetPushConstantRanges();

		assert(!ranges.empty());

		const auto& storage = equirectangularMaterial.GetUniformStorage();

		vkCmdPushConstants(commandBuffer.GetHandle(), equirectangularPipeline.GetLayout(), ranges[0].StageFlags, ranges[0].Offset, static_cast<uint32_t>(storage.size()), storage.data());

		vkCmdDispatch(commandBuffer.GetHandle(), (cubemapSize + 7) / 8, (cubemapSize + 7) / 8, 6);

		InsertImageMemoryBarrier(
			commandBuffer.GetHandle(),
			unfilteredEnvironment.GetImage().GetHandle(),
			VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
			VK_ACCESS_2_TRANSFER_READ_BIT,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT,
			cubemapRange
		);

		commandBuffer.Flush();
		commandPool.Reset();
	}

	// Generate normal source mip chain.
	// These mips are used by the GGX importance sampler to reduce variance.
	unfilteredEnvironment.GenerateMips();

	const uint32_t radianceMipCount = radianceMap->GetMipCount();

	// Create one storage view for every filtered radiance mip.
	std::vector<std::unique_ptr<ImageView>> radianceStorageViews(radianceMipCount);
	std::vector<uint32_t> radianceStorageIndices(radianceMipCount, Descriptor::INVALID_INDEX);

	for (uint32_t mip = 1; mip < radianceMipCount; ++mip)
	{
		radianceStorageViews[mip] = std::make_unique<ImageView>();

		ImageViewSpecification specification;
		specification.DebugName = "Environment Radiance Mip " + std::to_string(mip);
		specification.Image = &radianceMap->GetImage();
		specification.Mip = mip;
		specification.Storage = true;

		radianceStorageViews[mip]->Create(specification);

		radianceStorageIndices[mip] = Descriptor::RegisterStorageImage(radianceStorageViews[mip]->GetHandle());
	}

	// Copy the unfiltered environment to radiance mip 0.
	// This is used for perfectly reflective materials.
	{
		CommandBuffer commandBuffer = commandPool.AllocateCommandBuffer();
		commandBuffer.Begin(true);

		const VkImageSubresourceRange sourceRange
		{
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 6
		};

		const VkImageSubresourceRange destinationRange
		{
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 6
		};

		InsertImageMemoryBarrier(
			commandBuffer.GetHandle(),
			unfilteredEnvironment.GetImage().GetHandle(),
			VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
			VK_ACCESS_2_TRANSFER_READ_BIT,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT,
			sourceRange
		);

		InsertImageMemoryBarrier(
			commandBuffer.GetHandle(),
			radianceMap->GetImage().GetHandle(),
			VK_ACCESS_2_NONE,
			VK_ACCESS_2_TRANSFER_WRITE_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_PIPELINE_STAGE_2_NONE,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT,
			destinationRange
		);

		const VkImageCopy copyRegion
		{
			.srcSubresource =
			{
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.mipLevel = 0,
				.baseArrayLayer = 0,
				.layerCount = 6
			},
			 .srcOffset = { 0, 0, 0 },

			.dstSubresource =
			{
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.mipLevel = 0,
				.baseArrayLayer = 0,
				.layerCount = 6
			},
			.dstOffset = { 0, 0, 0 },

			.extent =
			{
				.width = cubemapSize,
				.height = cubemapSize,
				.depth = 1
			}
		};

		vkCmdCopyImage(
			commandBuffer.GetHandle(),
			unfilteredEnvironment.GetImage().GetHandle(),
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			radianceMap->GetImage().GetHandle(),
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1,
			&copyRegion
		);

		InsertImageMemoryBarrier(
			commandBuffer.GetHandle(),
			unfilteredEnvironment.GetImage().GetHandle(),
			VK_ACCESS_2_TRANSFER_READ_BIT,
			VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
			sourceRange
		);

		InsertImageMemoryBarrier(
			commandBuffer.GetHandle(),
			radianceMap->GetImage().GetHandle(),
			VK_ACCESS_2_TRANSFER_WRITE_BIT,
			VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT,
			VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
			destinationRange
		);

		commandBuffer.Flush();
		commandPool.Reset();
	}

	// Prefilter environment into the remaining radiance mips.
	{
		CommandBuffer commandBuffer = commandPool.AllocateCommandBuffer();
		commandBuffer.Begin(true);

		const VkImageSubresourceRange radianceRange
		{
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 1,
			.levelCount = radianceMipCount - 1,
			.baseArrayLayer = 0,
			.layerCount = 6
		};

		InsertImageMemoryBarrier(
			commandBuffer.GetHandle(),
			radianceMap->GetImage().GetHandle(),
			VK_ACCESS_2_NONE,
			VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_PIPELINE_STAGE_2_NONE,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
			radianceRange
		);

		prefilteredPipeline.Bind(commandBuffer.GetHandle());

		const VkDescriptorSet descriptorSet = Descriptor::GetSet();

		vkCmdBindDescriptorSets(commandBuffer.GetHandle(), VK_PIPELINE_BIND_POINT_COMPUTE, prefilteredPipeline.GetLayout(), 0, 1, &descriptorSet, 0, nullptr);

		const uint32_t sourceMipCount = unfilteredEnvironment.GetMipCount();
		const float deltaRoughness = 1.0f / std::max(static_cast<float>(radianceMipCount - 1), 1.0f);

		// Mip level 0 is a copy of the unfiltered environment map.
		for (uint32_t mip = 1, size = cubemapSize / 2; mip < radianceMipCount; ++mip, size = std::max(size / 2, 1u))
		{
			const float linearRoughness = static_cast<float>(mip) * deltaRoughness;
			const float roughness = linearRoughness * linearRoughness;

			prefilteredMaterial.Set("EnvironmentTexture", unfilteredEnvironment.GetTextureIndex());
			prefilteredMaterial.Set("OutputImage", radianceStorageIndices[mip]);
			prefilteredMaterial.Set("OutputSize", size);
			prefilteredMaterial.Set("SourceResolution", cubemapSize);
			prefilteredMaterial.Set("SourceMipCount", sourceMipCount);
			prefilteredMaterial.Set("SampleCount", sampleCount);
			prefilteredMaterial.Set("Roughness", roughness);

			const auto& ranges = prefilteredMaterial.GetShader()->GetPushConstantRanges();

			assert(!ranges.empty());

			const auto& storage = prefilteredMaterial.GetUniformStorage();

			vkCmdPushConstants(commandBuffer.GetHandle(), prefilteredPipeline.GetLayout(), ranges[0].StageFlags, ranges[0].Offset, static_cast<uint32_t>(storage.size()), storage.data());

			vkCmdDispatch(commandBuffer.GetHandle(), (size + 7) / 8, (size + 7) / 8, 6);
		}

		InsertImageMemoryBarrier(
			commandBuffer.GetHandle(),
			radianceMap->GetImage().GetHandle(),
			VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
			VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
			radianceRange
		);

		commandBuffer.Flush();
		commandPool.Reset();
	}

	// Generate irradiance map from the filtered radiance map
	{
		CommandBuffer commandBuffer = commandPool.AllocateCommandBuffer();
		commandBuffer.Begin(true);

		const VkImageSubresourceRange irradianceRange
		{
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 6
		};

		InsertImageMemoryBarrier(
			commandBuffer.GetHandle(),
			irradianceMap->GetImage().GetHandle(),
			VK_ACCESS_2_NONE,
			VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_PIPELINE_STAGE_2_NONE,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
			irradianceRange
		);

		irradiancePipeline.Bind(commandBuffer.GetHandle());

		const VkDescriptorSet descriptorSet = Descriptor::GetSet();

		vkCmdBindDescriptorSets(commandBuffer.GetHandle(), VK_PIPELINE_BIND_POINT_COMPUTE, irradiancePipeline.GetLayout(), 0, 1, &descriptorSet, 0, nullptr);

		irradianceMaterial.Set("EnvironmentMap", unfilteredEnvironment.GetTextureIndex());
		irradianceMaterial.Set("OutputImage", irradianceMap->GetImage().GetStorageIndex());
		irradianceMaterial.Set("OutputSize", irradianceSize);
		irradianceMaterial.Set("SourceResolution", unfilteredEnvironment.GetWidth());
		irradianceMaterial.Set("SourceMipCount", unfilteredEnvironment.GetMipCount());
		irradianceMaterial.Set("SampleCount", sampleCount);

		const auto& ranges = irradianceMaterial.GetShader()->GetPushConstantRanges();

		assert(!ranges.empty());

		const auto& storage = irradianceMaterial.GetUniformStorage();

		vkCmdPushConstants(commandBuffer.GetHandle(), irradiancePipeline.GetLayout(), ranges[0].StageFlags, ranges[0].Offset, static_cast<uint32_t>(storage.size()), storage.data());

		vkCmdDispatch(commandBuffer.GetHandle(), (irradianceSize + 7) / 8, (irradianceSize + 7) / 8, 6);

		InsertImageMemoryBarrier(
			commandBuffer.GetHandle(),
			irradianceMap->GetImage().GetHandle(),
			VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
			VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
			irradianceRange
		);

		commandBuffer.Flush();
		commandPool.Reset();
	}

	// Cleanup temporary per-mip storage descriptors/views
	for (uint32_t mip = 1; mip < radianceMipCount; ++mip)
	{
		Descriptor::UnregisterStorageImage(radianceStorageIndices[mip]);
		radianceStorageViews[mip]->Destroy();
	}

	// Cleanup temporary compute resources.
	irradiancePipeline.Shutdown();
	prefilteredPipeline.Shutdown();
	equirectangularPipeline.Shutdown();

	if (irradianceMaterial.GetShader())
		irradianceMaterial.GetShader()->Shutdown();

	if (prefilteredMaterial.GetShader())
		prefilteredMaterial.GetShader()->Shutdown();

	if (equirectangularMaterial.GetShader())
		equirectangularMaterial.GetShader()->Shutdown();

	// Temporary source resources can now go away.
	unfilteredEnvironment.Destroy();
	equirectangularTexture.Destroy();

	return{ radianceMap, irradianceMap };
}
