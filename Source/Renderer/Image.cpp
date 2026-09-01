#include "Image.hpp"

#include "RendererContext.hpp"

#include <cassert>

namespace
{
	bool IsDepthFormat(Format format)
	{
		switch (format)
		{
			case Format::D16_UNorm:
			case Format::D24_UNorm_S8_UInt:
			case Format::D32_Float:
			case Format::D32_Float_S8_UInt:
				return true;

			default:
				return false;
		}
	}

	bool HasStencil(Format format)
	{
		switch (format)
		{
			case Format::S8_UInt:
			case Format::D24_UNorm_S8_UInt:
			case Format::D32_Float_S8_UInt:
				return true;

			default:
				return false;
		}
	}

	VkImageAspectFlags GetAspectMask(Format format)
	{
		VkImageAspectFlags aspectMask = 0;

		if (IsDepthFormat(format))
			aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;

		if (HasStencil(format))
			aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;

		if (aspectMask == 0)
			aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

		return aspectMask;
	}

	VkImageUsageFlags GetUsageFlags(const ImageSpecification& specification)
	{
		VkImageUsageFlags usage = 0;

		if ((specification.Usage & ImageUsage::Sampled) != ImageUsage::None)
			usage |= VK_IMAGE_USAGE_SAMPLED_BIT;

		if ((specification.Usage & ImageUsage::Storage) != ImageUsage::None)
			usage |= VK_IMAGE_USAGE_STORAGE_BIT;

		if ((specification.Usage & ImageUsage::Attachment) != ImageUsage::None)
		{
			if (IsDepthFormat(specification.Format) || HasStencil(specification.Format))
			{
				usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
			}
			else
			{
				usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
			}
		}

		if (specification.Transfer)
		{
			usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
			usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		}

		return usage;
	}

	VkImageViewType GetImageViewType(ImageViewType type, const ImageSpecification& specification, uint32_t layerCount)
	{
		if (type != ImageViewType::Auto)
		{
			switch (type)
			{
				case ImageViewType::Image2D: return VK_IMAGE_VIEW_TYPE_2D;
				case ImageViewType::Image2DArray: return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
				case ImageViewType::Image3D: return VK_IMAGE_VIEW_TYPE_3D;
				case ImageViewType::Cube: return VK_IMAGE_VIEW_TYPE_CUBE;
				case ImageViewType::CubeArray: return VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;

				default:
					break;
			}
		}

		switch (specification.Type)
		{
			case TextureType::Texture2D: return layerCount > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
			case TextureType::Texture3D: return VK_IMAGE_VIEW_TYPE_3D;
			case TextureType::Cube: return layerCount > 6 ? VK_IMAGE_VIEW_TYPE_CUBE_ARRAY : VK_IMAGE_VIEW_TYPE_CUBE;
		}

		assert(false && "Invalid image type!");
		return VK_IMAGE_VIEW_TYPE_2D;
	}
}

void Image::Create(const ImageSpecification& specification)
{
	assert(specification.Width > 0);
	assert(specification.Height > 0);
	assert(specification.Depth > 0);
	assert(specification.Mips > 0);
	assert(specification.Layers > 0);

	if (specification.Type == TextureType::Texture3D)
	{
		assert(specification.Layers == 1 && "3D images cannot have array layers!");
	}

	if (specification.Type == TextureType::Cube)
	{
		assert(specification.Width == specification.Height && "Cube images must be square!");
		assert(specification.Depth == 1 && "Cube images must have a depth of 1!");
		assert(specification.Layers >= 6 && specification.Layers % 6 == 0 && "Cube images require layers in multiples of 6!");
	}

	Destroy();

	m_Specification = specification;

	const VkFormat format = ToVulkan(m_Specification.Format);

	VkImageCreateInfo imageInfo
	{
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.flags = m_Specification.Type == TextureType::Cube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : static_cast<VkImageCreateFlags>(0),
		.imageType = ToVulkan(m_Specification.Type),
		.format = format,

		.extent =
		{
			.width = m_Specification.Width,
			.height = m_Specification.Height,
			.depth = m_Specification.Depth
		},

		.mipLevels = m_Specification.Mips,
		.arrayLayers = m_Specification.Type == TextureType::Texture3D ? 1 : m_Specification.Layers,
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

	VK_CHECK(vmaCreateImage(Allocator::GetAllocator(), &imageInfo, &allocationInfo, &m_Info.ImageHandle, &m_Info.Allocation, nullptr));

	if (!m_Specification.DebugName.empty())
	{
		SetDebugUtilsObjectName(RendererContext::Get().GetDevice(), VK_OBJECT_TYPE_IMAGE, m_Specification.DebugName, m_Info.ImageHandle);
	}

	m_Info.ImageViewHandle = CreateImageView(ImageViewType::Auto, 0, m_Specification.Mips, 0, m_Specification.Layers);

	if (!m_Specification.DebugName.empty())
	{
		SetDebugUtilsObjectName(RendererContext::Get().GetDevice(), VK_OBJECT_TYPE_IMAGE_VIEW, m_Specification.DebugName + " View", m_Info.ImageViewHandle);
	}
}

void Image::Destroy()
{
	const VkDevice device = RendererContext::Get().GetDevice();

	for (VkImageView imageView : m_PerLayerImageViews)
	{
		if (imageView != VK_NULL_HANDLE)
		{
			vkDestroyImageView(device, imageView, nullptr);
		}
	}

	m_PerLayerImageViews.clear();

	for (const auto& [mip, imageView] : m_PerMipImageViews)
	{
		if (imageView != VK_NULL_HANDLE)
		{
			vkDestroyImageView(device, imageView, nullptr);
		}
	}

	m_PerMipImageViews.clear();

	if (m_Info.ImageViewHandle != VK_NULL_HANDLE)
	{
		vkDestroyImageView(device, m_Info.ImageViewHandle, nullptr);

		m_Info.ImageViewHandle = VK_NULL_HANDLE;
	}

	if (m_Info.ImageHandle != VK_NULL_HANDLE)
	{
		vmaDestroyImage(Allocator::GetAllocator(), m_Info.ImageHandle, m_Info.Allocation);

		m_Info.ImageHandle = VK_NULL_HANDLE;
		m_Info.Allocation = VK_NULL_HANDLE;
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

VkImageView Image::CreateImageView(ImageViewType type, uint32_t baseMip, uint32_t mipCount, uint32_t baseLayer, uint32_t layerCount, VkImageAspectFlags aspectMask) const
{
	assert(m_Info.ImageHandle != VK_NULL_HANDLE);

	assert(baseMip < m_Specification.Mips);
	assert(baseLayer < m_Specification.Layers);

	const uint32_t resolvedMipCount = mipCount == 0 ? m_Specification.Mips - baseMip : mipCount;
	const uint32_t resolvedLayerCount = layerCount == 0 ? m_Specification.Layers - baseLayer : layerCount;

	assert(baseMip + resolvedMipCount <= m_Specification.Mips);
	assert(baseLayer + resolvedLayerCount <= m_Specification.Layers);

	if (aspectMask == 0)
	{
		aspectMask = GetAspectMask(m_Specification.Format);
	}

	VkImageViewCreateInfo viewInfo
	{
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = m_Info.ImageHandle,
		.viewType = GetImageViewType(type, m_Specification, resolvedLayerCount),
		.format = ToVulkan(m_Specification.Format),

		.subresourceRange =
		{
			.aspectMask = aspectMask,
			.baseMipLevel = baseMip,
			.levelCount = resolvedMipCount,
			.baseArrayLayer = baseLayer,
			.layerCount = resolvedLayerCount
		}
	};

	VkImageView imageView = VK_NULL_HANDLE;

	VK_CHECK(vkCreateImageView(RendererContext::Get().GetDevice(), &viewInfo, nullptr, &imageView));

	return imageView;
}

void Image::CreatePerLayerImageViews()
{
	assert(m_Specification.Layers > 1);

	if (!m_PerLayerImageViews.empty())
		return;

	m_PerLayerImageViews.resize(m_Specification.Layers, VK_NULL_HANDLE);

	for (uint32_t layer = 0; layer < m_Specification.Layers; layer++)
	{
		m_PerLayerImageViews[layer] = CreateImageView(ImageViewType::Image2D, 0, m_Specification.Mips, layer, 1);
	}
}

VkImageView Image::GetLayerImageView(uint32_t layer) const
{
	assert(layer < m_Specification.Layers);
	assert(layer < m_PerLayerImageViews.size());

	return m_PerLayerImageViews[layer];
}

VkImageView Image::GetMipImageView(uint32_t mip)
{
	assert(mip < m_Specification.Mips);

	const auto it = m_PerMipImageViews.find(mip);

	if (it != m_PerMipImageViews.end())
		return it->second;

	const VkImageView imageView = CreateImageView(ImageViewType::Auto, mip, 1, 0, m_Specification.Layers);

	m_PerMipImageViews[mip] = imageView;

	return imageView;
}

void ImageView::Create(const ImageViewSpecification& specification)
{
	assert(specification.Image);
	assert(specification.Image->IsValid());

	Destroy();

	m_Specification = specification;
	m_Info = m_Specification.Image->GetImageInfo();

	const ImageSpecification& imageSpecification = m_Specification.Image->GetSpecification();

	assert(m_Specification.BaseMip < imageSpecification.Mips);
	assert(m_Specification.BaseLayer < imageSpecification.Layers);

	const uint32_t mipCount = m_Specification.MipCount == 0 ? imageSpecification.Mips - m_Specification.BaseMip : m_Specification.MipCount;
	const uint32_t layerCount = m_Specification.LayerCount == 0 ? imageSpecification.Layers - m_Specification.BaseLayer : m_Specification.LayerCount;

	assert(m_Specification.BaseMip + mipCount <= imageSpecification.Mips);
	assert(m_Specification.BaseLayer + layerCount <= imageSpecification.Layers);

	const VkDevice device = RendererContext::Get().GetDevice();

	const VkImageAspectFlags aspectMask = m_Specification.AspectMask != 0 ? m_Specification.AspectMask : GetAspectMask(imageSpecification.Format);

	VkImageViewCreateInfo viewInfo
	{
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = m_Info.ImageHandle,
		.viewType = GetImageViewType(m_Specification.Type, imageSpecification, layerCount),
		.format = ToVulkan(imageSpecification.Format),

		.subresourceRange =
		{
			.aspectMask = aspectMask,
			.baseMipLevel = m_Specification.BaseMip,
			.levelCount = mipCount,
			.baseArrayLayer = m_Specification.BaseLayer,
			.layerCount = layerCount
		}
	};

	m_Info.ImageViewHandle = VK_NULL_HANDLE;

	VK_CHECK(vkCreateImageView(device, &viewInfo, nullptr, &m_Info.ImageViewHandle));

	if (!m_Specification.DebugName.empty())
	{
		SetDebugUtilsObjectName(device, VK_OBJECT_TYPE_IMAGE_VIEW, m_Specification.DebugName, m_Info.ImageViewHandle);
	}
}

void ImageView::Destroy()
{
	if (m_Info.ImageViewHandle != VK_NULL_HANDLE)
	{
		vkDestroyImageView(RendererContext::Get().GetDevice(), m_Info.ImageViewHandle, nullptr);
		m_Info.ImageViewHandle = VK_NULL_HANDLE;
	}

	m_Info.ImageHandle = VK_NULL_HANDLE;
	m_Info.Allocation = VK_NULL_HANDLE;
}

void Sampler::Create(const SamplerSpecification& specification)
{
	Destroy();

	m_Specification = specification;

	const VkDevice device = RendererContext::Get().GetDevice();

	const bool mipmapping = m_Specification.Mip != SamplerMip::Disabled;

	VkSamplerCreateInfo samplerInfo
	{
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,

		.magFilter = ToVulkan(m_Specification.MagFilter),
		.minFilter = ToVulkan(m_Specification.MinFilter),

		.mipmapMode = ToVulkan(m_Specification.Mip),

		.addressModeU = ToVulkan(m_Specification.WrapU),
		.addressModeV = ToVulkan(m_Specification.WrapV),
		.addressModeW = ToVulkan(m_Specification.WrapW),

		.mipLodBias = m_Specification.MipLodBias,

		.anisotropyEnable = m_Specification.Anisotropy ? VK_TRUE : VK_FALSE,
		.maxAnisotropy = m_Specification.MaxAnisotropy,

		.compareEnable = m_Specification.Compare ? VK_TRUE : VK_FALSE,
		.compareOp = ToVulkan(m_Specification.CompareOperation),

		.minLod = mipmapping ? m_Specification.MinLod : 0.0f,
		.maxLod = mipmapping ? m_Specification.MaxLod : 0.0f,

		.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK,

		.unnormalizedCoordinates = VK_FALSE
	};

	VK_CHECK(vkCreateSampler(device, &samplerInfo, nullptr, &m_SamplerHandle));

	if (!m_Specification.DebugName.empty())
	{
		SetDebugUtilsObjectName(device, VK_OBJECT_TYPE_SAMPLER, m_Specification.DebugName, m_SamplerHandle);
	}

	m_DescriptorIndex = Descriptor::RegisterSampler(m_SamplerHandle);
}

void Sampler::Destroy()
{
	if (m_DescriptorIndex != Descriptor::INVALID_INDEX)
	{
		Descriptor::UnregisterSampler(m_DescriptorIndex);
		m_DescriptorIndex = Descriptor::INVALID_INDEX;
	}

	if (m_SamplerHandle != VK_NULL_HANDLE)
	{
		vkDestroySampler(RendererContext::Get().GetDevice(), m_SamplerHandle, nullptr);
		m_SamplerHandle = VK_NULL_HANDLE;
	}
}
