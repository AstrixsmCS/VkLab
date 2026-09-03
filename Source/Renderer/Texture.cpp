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
	uint32_t CalculateMipCount(uint32_t width, uint32_t height, uint32_t depth = 1)
	{
		return static_cast<uint32_t>(std::floor(std::log2(std::max({ width, height, depth })))) + 1;
	}

	size_t GetMemorySize(Format format, uint32_t width, uint32_t height, uint32_t depth = 1, uint32_t layers = 1)
	{
		size_t pixelSize = 0;

		switch (format)
		{
			case Format::R8_UNorm:
			case Format::R8_UInt:
				pixelSize = 1;
				break;

			case Format::R16_UInt:
				pixelSize = sizeof(uint16_t);
				break;

			case Format::R32_Float:
			case Format::R32_UInt:
				pixelSize = sizeof(uint32_t);
				break;

			case Format::RG16_Float:
				pixelSize = 2 * sizeof(uint16_t);
				break;

			case Format::RG32_Float:
				pixelSize = 2 * sizeof(float);
				break;

			case Format::RGBA8_UNorm:
			case Format::RGBA8_SRGB:
				pixelSize = 4;
				break;

			case Format::RGBA16_Float:
				pixelSize = 4 * sizeof(uint16_t);
				break;

			case Format::RGBA32_Float:
				pixelSize = 4 * sizeof(float);
				break;

			default:
				assert(false && "Unsupported texture format!");
				return 0;
		}

		return static_cast<size_t>(width) * height * depth * layers * pixelSize;
	}
}

void Texture::Create(const TextureSpecification& specification)
{
	assert(specification.Width > 0);
	assert(specification.Height > 0);
	assert(specification.Depth > 0);

	if (specification.Type == ImageType::Image2D)
	{
		assert(specification.Depth == 1);
	}

	if (specification.Type == ImageType::Cube)
	{
		assert(specification.Width == specification.Height);
		assert(specification.Depth == 1);
	}

	Destroy();

	m_Specification = specification;

	const uint32_t mipCount = m_Specification.GenerateMips ? CalculateMipCount(m_Specification.Width, m_Specification.Height, m_Specification.Depth) : 1;

	ImageSpecification imageSpecification
	{
		.DebugName = m_Specification.DebugName,
		.Type = m_Specification.Type,
		.Format = m_Specification.Format,
		.Usage = m_Specification.Usage | ImageUsage::TransferSrc | ImageUsage::TransferDst,
		.Width = m_Specification.Width,
		.Height = m_Specification.Height,
		.Depth = m_Specification.Depth,
		.Mips = mipCount
	};

	m_Image.Create(imageSpecification);
}

void Texture::Create(const TextureSpecification& specification, const void* data)
{
	assert(data);

	Create(specification);

	const uint32_t layerCount = m_Specification.Type == ImageType::Cube ? 6 : 1;

	const size_t dataSize = GetMemorySize(m_Specification.Format, m_Specification.Width, m_Specification.Height, m_Specification.Depth, layerCount);

	SetData(data, dataSize);
}

void Texture::Create(const TextureSpecification& specification, const std::filesystem::path& path)
{
	assert(specification.Type == ImageType::Image2D && "File texture loading currently only supports 2D textures!");

	int width = 0;
	int height = 0;
	int channels = 0;

	TextureSpecification textureSpecification = specification;

	const std::string pathString = path.string();
	const bool isHDR = stbi_is_hdr(pathString.c_str()) != 0;

	void* pixels = nullptr;

	if (isHDR)
	{
		pixels = stbi_loadf(
			pathString.c_str(),
			&width,
			&height,
			&channels,
			STBI_rgb_alpha
		);

		textureSpecification.Format = Format::RGBA32_Float;
	}
	else
	{
		pixels = stbi_load(
			pathString.c_str(),
			&width,
			&height,
			&channels,
			STBI_rgb_alpha
		);
	}

	assert(pixels && "Failed to load texture!");

	if (textureSpecification.DebugName.empty())
	{
		textureSpecification.DebugName = path.filename().string();
	}

	textureSpecification.Type = ImageType::Image2D;
	textureSpecification.Width = static_cast<uint32_t>(width);
	textureSpecification.Height = static_cast<uint32_t>(height);
	textureSpecification.Depth = 1;

	Create(textureSpecification, pixels);

	stbi_image_free(pixels);
}

void Texture::SetData(const void* data, size_t size)
{
	assert(data);
	assert(size > 0);
	assert(m_Image.IsValid());

	VkBuffer stagingBuffer = VK_NULL_HANDLE;
	VmaAllocation stagingAllocation = VK_NULL_HANDLE;

	VkBufferCreateInfo stagingBufferInfo
	{
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = static_cast<VkDeviceSize>(size),
		.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE
	};

	VmaAllocationCreateInfo stagingAllocationInfo
	{
		.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
		.usage = VMA_MEMORY_USAGE_AUTO
	};

	VK_CHECK(vmaCreateBuffer(Allocator::GetAllocator(), &stagingBufferInfo, &stagingAllocationInfo, &stagingBuffer, &stagingAllocation, nullptr));

	void* mappedData = nullptr;

	VK_CHECK(vmaMapMemory(Allocator::GetAllocator(), stagingAllocation, &mappedData));

	std::memcpy(mappedData, data, size);

	vmaUnmapMemory(Allocator::GetAllocator(), stagingAllocation);

	CommandPool& commandPool = RendererContext::Get().GetImmediateCommandPool();
	CommandBuffer commandBuffer = commandPool.AllocateCommandBuffer();
	commandBuffer.Begin(true);

	const uint32_t layerCount = m_Image.GetType() == ImageType::Cube ? m_Image.GetLayerCount() : 1;

	VkImageSubresourceRange mipZeroRange
	{
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.baseMipLevel = 0,
		.levelCount = 1,
		.baseArrayLayer = 0,
		.layerCount = layerCount
	};

	SetImageLayout(
		commandBuffer.GetHandle(),
		m_Image.GetHandle(),
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
			.layerCount = layerCount
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
			.depth = m_Image.GetDepth()
		}
	};

	vkCmdCopyBufferToImage(
		commandBuffer.GetHandle(),
		stagingBuffer,
		m_Image.GetHandle(),
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		1,
		&copyRegion
	);

	if (m_Image.GetMipCount() > 1)
	{
		SetImageLayout(
			commandBuffer.GetHandle(),
			m_Image.GetHandle(),
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			mipZeroRange
		);
	}
	else
	{
		SetImageLayout(
			commandBuffer.GetHandle(),
			m_Image.GetHandle(),
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			mipZeroRange
		);
	}

	commandBuffer.Flush();
	commandPool.Reset();

	vmaDestroyBuffer(Allocator::GetAllocator(), stagingBuffer, stagingAllocation);

	if (m_Image.GetMipCount() > 1)
	{
		GenerateMips();
	}
}

void Texture::GenerateMips()
{
	const uint32_t mipCount = m_Image.GetMipCount();
	const uint32_t layerCount = m_Image.GetType() == ImageType::Cube ? m_Image.GetLayerCount() : 1;

	assert(mipCount > 1);

	CommandPool& commandPool = RendererContext::Get().GetImmediateCommandPool();
	CommandBuffer commandBuffer = commandPool.AllocateCommandBuffer();
	commandBuffer.Begin(true);

	int32_t mipWidth = static_cast<int32_t>(m_Image.GetWidth());
	int32_t mipHeight = static_cast<int32_t>(m_Image.GetHeight());
	int32_t mipDepth = static_cast<int32_t>(m_Image.GetDepth());

	for (uint32_t mip = 1; mip < mipCount; ++mip)
	{
		VkImageSubresourceRange destinationRange
		{
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = mip,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = layerCount
		};

		SetImageLayout(
			commandBuffer.GetHandle(),
			m_Image.GetHandle(),
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			destinationRange
		);

		const int32_t nextWidth = std::max(mipWidth / 2, 1);
		const int32_t nextHeight = std::max(mipHeight / 2, 1);
		const int32_t nextDepth = std::max(mipDepth / 2, 1);

		if (m_Image.GetType() == ImageType::Cube)
		{
			for (uint32_t layer = 0; layer < layerCount; ++layer)
			{
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
					.baseArrayLayer = layer,
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
					.baseArrayLayer = layer,
					.layerCount = 1
				};

				vkCmdBlitImage(
					commandBuffer.GetHandle(),
					m_Image.GetHandle(),
					VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
					m_Image.GetHandle(),
					VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					1,
					&blit,
					VK_FILTER_LINEAR
				);
			}
		}
		else
		{
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
				mipDepth
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
				nextDepth
			};

			blit.dstSubresource =
			{
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.mipLevel = mip,
				.baseArrayLayer = 0,
				.layerCount = 1
			};

			vkCmdBlitImage(
				commandBuffer.GetHandle(),
				m_Image.GetHandle(),
				VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				m_Image.GetHandle(),
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				1,
				&blit,
				VK_FILTER_LINEAR
			);
		}

		SetImageLayout(
			commandBuffer.GetHandle(),
			m_Image.GetHandle(),
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			destinationRange
		);

		mipWidth = nextWidth;
		mipHeight = nextHeight;
		mipDepth = nextDepth;
	}

	VkImageSubresourceRange allMips
	{
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.baseMipLevel = 0,
		.levelCount = mipCount,
		.baseArrayLayer = 0,
		.layerCount = layerCount
	};

	SetImageLayout(
		commandBuffer.GetHandle(),
		m_Image.GetHandle(),
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		allMips
	);

	commandBuffer.Flush();
	commandPool.Reset();
}

void Texture::Destroy()
{
	m_Image.Destroy();
}
