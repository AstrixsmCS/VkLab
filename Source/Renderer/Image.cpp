#include "Image.hpp"

#include "RendererContext.hpp"

#include <cassert>

namespace
{
	bool IsDepthFormat(Format format)
	{
		switch (format)
		{
			case Format::D32_Float:
			case Format::D32_Float_S8_UInt:
			case Format::D24_UNorm_S8_UInt:
				return true;

			default:
				return false;
		}
	}

	bool HasStencil(Format format)
	{
		return format == Format::D32_Float_S8_UInt || format == Format::D24_UNorm_S8_UInt;
	}

	VkImageAspectFlags GetAspectMask(Format format)
	{
		if (!IsDepthFormat(format))
			return VK_IMAGE_ASPECT_COLOR_BIT;

		VkImageAspectFlags aspect = VK_IMAGE_ASPECT_DEPTH_BIT;

		if (HasStencil(format))
			aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;

		return aspect;
	}

	VkImageUsageFlags GetUsageFlags(const ImageSpecification& specification)
	{
		VkImageUsageFlags usage = 0;

		switch (specification.Usage)
		{
			case ImageUsage::Texture:
				usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
				break;

			case ImageUsage::Attachment:
			{
				if (IsDepthFormat(specification.Format))
					usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
				else
					usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

				break;
			}

			case ImageUsage::Storage:
				usage |= VK_IMAGE_USAGE_STORAGE_BIT;
				break;

			case ImageUsage::None:
				break;
		}

		if (specification.Transfer)
		{
			usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
			usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		}

		return usage;
	}
}

void Image::Create(const ImageSpecification& specification)
{
	assert(specification.Width > 0);
	assert(specification.Height > 0);
	assert(specification.Mips > 0);
	assert(specification.Layers > 0);

	Destroy();

	m_Specification = specification;

	const VkDevice device = RendererContext::Get().GetDevice();
	const VkFormat format = ToVulkan(m_Specification.Format);

	VkImageCreateInfo imageInfo
	{
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = format,
		.extent =
		{
			.width = m_Specification.Width,
			.height = m_Specification.Height,
			.depth = 1
		},
		.mipLevels = m_Specification.Mips,
		.arrayLayers = m_Specification.Layers,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = GetUsageFlags(m_Specification),
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
	};

	VmaAllocationCreateInfo allocationInfo
	{
		.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
	};

	VK_CHECK(vmaCreateImage(Allocator::GetAllocator(), &imageInfo, &allocationInfo, &m_Image, &m_Allocation, nullptr));

	VkImageViewCreateInfo viewInfo
	{
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = m_Image,
		.viewType = m_Specification.Layers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D,
		.format = format,
		.subresourceRange =
		{
			.aspectMask = GetAspectMask(m_Specification.Format),
			.baseMipLevel = 0,
			.levelCount = m_Specification.Mips,
			.baseArrayLayer = 0,
			.layerCount = m_Specification.Layers
		}
	};

	VK_CHECK(vkCreateImageView(device, &viewInfo, nullptr, &m_ImageView));

	if (!m_Specification.DebugName.empty())
	{
		SetDebugUtilsObjectName(device, VK_OBJECT_TYPE_IMAGE, m_Specification.DebugName, m_Image);
		SetDebugUtilsObjectName(device, VK_OBJECT_TYPE_IMAGE_VIEW, m_Specification.DebugName + " View", m_ImageView);
	}
}

void Image::Destroy()
{
	const VkDevice device = RendererContext::Get().GetDevice();

	if (m_ImageView != VK_NULL_HANDLE)
	{
		vkDestroyImageView(device, m_ImageView, nullptr);
		m_ImageView = VK_NULL_HANDLE;
	}

	if (m_Image != VK_NULL_HANDLE)
	{
		vmaDestroyImage(Allocator::GetAllocator(), m_Image, m_Allocation);

		m_Image = VK_NULL_HANDLE;
		m_Allocation = VK_NULL_HANDLE;
	}
}

void Image::Resize(uint32_t width, uint32_t height)
{
	if (width == 0 || height == 0)
		return;

	if (width == m_Specification.Width && height == m_Specification.Height)
	{
		return;
	}

	ImageSpecification specification = m_Specification;

	specification.Width = width;
	specification.Height = height;

	Create(specification);
}
