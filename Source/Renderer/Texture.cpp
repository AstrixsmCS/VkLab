#include "Texture.hpp"

#include "Allocator.hpp"
#include "RendererContext.hpp"

#include <stb_image.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>

namespace
{
	uint32_t CalculateMipCount(uint32_t width, uint32_t height)
	{
		return static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;
	}

	size_t GetMemorySize(Format format, uint32_t width, uint32_t height)
	{
		switch (format)
		{
			case Format::R8_UNorm:
			case Format::R8_UInt:
				return static_cast<size_t>(width) * height;

			case Format::R16_UInt:
				return static_cast<size_t>(width) * height * sizeof(uint16_t);

			case Format::R32_Float:
			case Format::R32_UInt:
				return static_cast<size_t>(width) * height * sizeof(uint32_t);

			case Format::RG16_Float:
				return static_cast<size_t>(width) * height * 2 * sizeof(uint16_t);

			case Format::RG32_Float:
				return static_cast<size_t>(width) * height * 2 * sizeof(float);

			case Format::RGBA8_UNorm:
			case Format::RGBA8_SRGB:
				return static_cast<size_t>(width) * height * 4;

			case Format::RGBA16_Float:
				return static_cast<size_t>(width) * height * 4 * sizeof(uint16_t);

			case Format::RGBA32_Float:
				return static_cast<size_t>(width) * height * 4 * sizeof(float);

			default:
				break;
		}

		assert(false && "Unsupported texture format!");
		return 0;
	}
}

void Texture::Create(const TextureSpecification& specification, const void* data, uint32_t width, uint32_t height)
{
	assert(data);
	assert(width > 0);
	assert(height > 0);

	Destroy();

	m_Specification = specification;

	const uint32_t mipCount = m_Specification.GenerateMips ? CalculateMipCount(width, height) : 1;

	ImageSpecification imageSpecification
	{
		.DebugName = m_Specification.DebugName,
		.Format = m_Specification.Format,
		.Usage = ImageUsage::Texture,
		.Width = width,
		.Height = height,
		.Mips = mipCount,
		.Layers = 1,
		.Transfer = true
	};

	m_Image.Create(imageSpecification);

	const size_t dataSize = GetMemorySize(m_Specification.Format, width, height);

	Upload(data, dataSize);

	CreateSampler();

	m_TextureIndex = Descriptor::RegisterTexture(m_Image.GetView());
	m_SamplerIndex = Descriptor::RegisterSampler(m_Sampler);
}

void Texture::Create(const TextureSpecification& specification, const std::filesystem::path& path)
{
	int width = 0;
	int height = 0;
	int channels = 0;

	stbi_uc* pixels = stbi_load(
		path.string().c_str(),
		&width,
		&height,
		&channels,
		STBI_rgb_alpha
	);

	assert(pixels && "Failed to load texture!");

	TextureSpecification textureSpecification = specification;

	if (textureSpecification.DebugName.empty())
	{
		textureSpecification.DebugName = path.filename().string();
	}

	Create(textureSpecification, pixels, static_cast<uint32_t>(width), static_cast<uint32_t>(height));

	stbi_image_free(pixels);
}

void Texture::Upload(const void* data, size_t size)
{
	assert(data);
	assert(size > 0);

	VkBuffer stagingBuffer = VK_NULL_HANDLE;

	VmaAllocation stagingAllocation = VK_NULL_HANDLE;

	VkBufferCreateInfo bufferInfo
	{
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = static_cast<VkDeviceSize>(size),
		.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE
	};

	VmaAllocationCreateInfo allocationInfo
	{
		.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
		.usage = VMA_MEMORY_USAGE_AUTO
	};

	VmaAllocationInfo mappedInfo{};

	VK_CHECK(vmaCreateBuffer(Allocator::GetAllocator(), &bufferInfo, &allocationInfo, &stagingBuffer, &stagingAllocation, &mappedInfo));

	std::memcpy(mappedInfo.pMappedData, data, size);

	vmaFlushAllocation(Allocator::GetAllocator(), stagingAllocation, 0, static_cast<VkDeviceSize>(size));

	VkCommandBuffer commandBuffer = BeginTransientCommandBuffer();

	VkImageSubresourceRange mipZeroRange
	{
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.baseMipLevel = 0,
		.levelCount = 1,
		.baseArrayLayer = 0,
		.layerCount = 1
	};

	SetImageLayout(
		commandBuffer,
		m_Image.GetImage(),
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		mipZeroRange
	);

	VkBufferImageCopy copyRegion
	{
		.bufferOffset = 0,
		.bufferRowLength = 0,
		.bufferImageHeight = 0,

		.imageSubresource =
		{
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.mipLevel = 0,
			.baseArrayLayer = 0,
			.layerCount = 1
		},

		.imageOffset =
		{
			0,
			0,
			0
		},

		.imageExtent =
		{
			.width = m_Image.GetWidth(),
			.height = m_Image.GetHeight(),
			.depth = 1
		}
	};

	vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, m_Image.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

	if (m_Image.GetMipCount() > 1)
	{
		SetImageLayout(
			commandBuffer,
			m_Image.GetImage(),
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			mipZeroRange
		);
	}
	else
	{
		SetImageLayout(
			commandBuffer,
			m_Image.GetImage(),
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			mipZeroRange
		);
	}

	EndTransientCommandBuffer(commandBuffer);

	vmaDestroyBuffer(Allocator::GetAllocator(), stagingBuffer, stagingAllocation);

	if (m_Image.GetMipCount() > 1)
		GenerateMips();
}

void Texture::GenerateMips()
{
	const uint32_t mipCount = m_Image.GetMipCount();

	assert(mipCount > 1);

	VkCommandBuffer commandBuffer = BeginTransientCommandBuffer();

	int32_t mipWidth = static_cast<int32_t>(m_Image.GetWidth());

	int32_t mipHeight = static_cast<int32_t>(m_Image.GetHeight());

	for (uint32_t mip = 1; mip < mipCount; ++mip)
	{
		VkImageSubresourceRange destinationRange
		{
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = mip,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1
		};

		SetImageLayout(
			commandBuffer,
			m_Image.GetImage(),
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			destinationRange
		);

		const int32_t nextWidth = std::max(mipWidth / 2, 1);
		const int32_t nextHeight = std::max(mipHeight / 2, 1);

		VkImageBlit blit{};

		blit.srcOffsets[0] =
		{
			0,
			0,
			0
		};

		blit.srcOffsets[1] =
		{
			mipWidth,
			mipHeight,
			1
		};

		blit.srcSubresource =
		{
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.mipLevel = mip - 1,
			.baseArrayLayer = 0,
			.layerCount = 1
		};

		blit.dstOffsets[0] =
		{
			0,
			0,
			0
		};

		blit.dstOffsets[1] =
		{
			nextWidth,
			nextHeight,
			1
		};

		blit.dstSubresource =
		{
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.mipLevel = mip,
			.baseArrayLayer = 0,
			.layerCount = 1
		};

		vkCmdBlitImage(
			commandBuffer,
			m_Image.GetImage(),
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			m_Image.GetImage(),
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1,
			&blit,
			VK_FILTER_LINEAR
		);

		SetImageLayout(
			commandBuffer,
			m_Image.GetImage(),
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			destinationRange
		);

		mipWidth = nextWidth;
		mipHeight = nextHeight;
	}

	VkImageSubresourceRange allMips
	{
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.baseMipLevel = 0,
		.levelCount = mipCount,
		.baseArrayLayer = 0,
		.layerCount = 1
	};

	SetImageLayout(
		commandBuffer,
		m_Image.GetImage(),
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		allMips
	);

	EndTransientCommandBuffer(commandBuffer);
}

void Texture::CreateSampler()
{
	const VkDevice device = RendererContext::Get().GetDevice();

	const VkFilter filter = ToVulkan(m_Specification.Filter);
	const VkSamplerAddressMode wrap = ToVulkan(m_Specification.Wrap);

	VkSamplerCreateInfo samplerInfo
	{
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter = filter,
		.minFilter = filter,
		.mipmapMode = m_Specification.Filter == SamplerFilter::Nearest ? VK_SAMPLER_MIPMAP_MODE_NEAREST : VK_SAMPLER_MIPMAP_MODE_LINEAR,
		.addressModeU = wrap,
		.addressModeV = wrap,
		.addressModeW = wrap,
		.mipLodBias = 0.0f,
		.anisotropyEnable = VK_FALSE,
		.maxAnisotropy = 1.0f,
		.compareEnable = VK_FALSE,
		.compareOp = VK_COMPARE_OP_NEVER,
		.minLod = 0.0f,
		.maxLod = static_cast<float>(m_Image.GetMipCount()),
		.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
		.unnormalizedCoordinates = VK_FALSE
	};

	VK_CHECK(vkCreateSampler(device, &samplerInfo, nullptr, &m_Sampler));

	if (!m_Specification.DebugName.empty())
	{
		SetDebugUtilsObjectName(device, VK_OBJECT_TYPE_SAMPLER, m_Specification.DebugName + " Sampler", m_Sampler);
	}
}

VkCommandBuffer Texture::BeginTransientCommandBuffer()
{
	RendererContext& context = RendererContext::Get();
	const VkDevice device = context.GetDevice();

	VkCommandPoolCreateInfo poolInfo
	{
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
		.queueFamilyIndex = context.GetGraphicsFamily()
	};

	VK_CHECK(vkCreateCommandPool(device, &poolInfo, nullptr, &m_TransientCommandPool));

	VkCommandBufferAllocateInfo allocateInfo
	{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = m_TransientCommandPool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1
	};

	VkCommandBuffer commandBuffer = VK_NULL_HANDLE;

	VK_CHECK(vkAllocateCommandBuffers(device, &allocateInfo, &commandBuffer));

	VkCommandBufferBeginInfo beginInfo
	{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
	};

	VK_CHECK(vkBeginCommandBuffer(commandBuffer, &beginInfo));

	return commandBuffer;
}

void Texture::EndTransientCommandBuffer(
	VkCommandBuffer commandBuffer)
{
	RendererContext& context = RendererContext::Get();
	const VkDevice device = context.GetDevice();

	VK_CHECK(vkEndCommandBuffer(commandBuffer));

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

	VkFenceCreateInfo fenceInfo
	{
		.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO
	};

	VkFence fence = VK_NULL_HANDLE;

	VK_CHECK(vkCreateFence(device, &fenceInfo, nullptr, &fence));

	VK_CHECK(vkQueueSubmit2(context.GetGraphicsQueue(), 1, &submitInfo, fence));

	VK_CHECK(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX));

	vkDestroyFence(device, fence, nullptr);

	vkDestroyCommandPool(device, m_TransientCommandPool, nullptr);

	m_TransientCommandPool = VK_NULL_HANDLE;
}

void Texture::Destroy()
{
	const VkDevice device = RendererContext::Get().GetDevice();

	Descriptor::UnregisterTexture(m_TextureIndex);
	Descriptor::UnregisterSampler(m_SamplerIndex);

	m_TextureIndex = Descriptor::INVALID_INDEX;
	m_SamplerIndex = Descriptor::INVALID_INDEX;

	if (m_Sampler != VK_NULL_HANDLE)
	{
		vkDestroySampler(device, m_Sampler, nullptr);
		m_Sampler = VK_NULL_HANDLE;
	}

	m_Image.Destroy();
}
