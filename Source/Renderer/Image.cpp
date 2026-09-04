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

	VkImageType GetImageType(ImageType type)
	{
		switch (type)
		{
			case ImageType::Image2D:
			case ImageType::Cube:
				return VK_IMAGE_TYPE_2D;

			case ImageType::Image3D:
				return VK_IMAGE_TYPE_3D;
		}

		assert(false && "Invalid image type!");
		return VK_IMAGE_TYPE_2D;
	}

	VkImageViewType GetImageViewType(ImageType type)
	{
		switch (type)
		{
			case ImageType::Image2D:
				return VK_IMAGE_VIEW_TYPE_2D;

			case ImageType::Image3D:
				return VK_IMAGE_VIEW_TYPE_3D;

			case ImageType::Cube:
				return VK_IMAGE_VIEW_TYPE_CUBE;
		}

		assert(false && "Invalid image type!");
		return VK_IMAGE_VIEW_TYPE_2D;
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

		if ((specification.Usage & ImageUsage::TransferSrc) != ImageUsage::None)
			usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

		if ((specification.Usage & ImageUsage::TransferDst) != ImageUsage::None)
			usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;

		return usage;
	}
}

void Image::Create(const ImageSpecification& specification)
{
	assert(specification.Width > 0);
	assert(specification.Height > 0);
	assert(specification.Depth > 0);
	assert(specification.Mips > 0);

	if (specification.Type == ImageType::Image2D)
	{
		assert(specification.Depth == 1 && "2D images must have a depth of 1!");
	}

	if (specification.Type == ImageType::Cube)
	{
		assert(specification.Width == specification.Height && "Cube images must be square!");
		assert(specification.Depth == 1 && "Cube images must have a depth of 1!");
	}

	Destroy();

	m_Specification = specification;

	const VkDevice device = RendererContext::Get().GetDevice();
	const uint32_t layerCount = GetLayerCount();

	VkImageCreateInfo imageInfo
	{
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.flags = m_Specification.Type == ImageType::Cube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : static_cast<VkImageCreateFlags>(0),
		.imageType = GetImageType(m_Specification.Type),
		.format = ToVulkan(m_Specification.Format),

		.extent =
		{
			.width = m_Specification.Width,
			.height = m_Specification.Height,
			.depth = m_Specification.Depth
		},

		.mipLevels = m_Specification.Mips,
		.arrayLayers = layerCount,
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
		SetDebugUtilsObjectName(device, VK_OBJECT_TYPE_IMAGE, m_Specification.DebugName, m_Info.ImageHandle);
	}

	m_Info.ImageViewHandle = CreateImageView(0, m_Specification.Mips, 0, layerCount);

	if (!m_Specification.DebugName.empty())
	{
		SetDebugUtilsObjectName(device, VK_OBJECT_TYPE_IMAGE_VIEW, m_Specification.DebugName + " View", m_Info.ImageViewHandle);
	}

	if ((m_Specification.Usage & ImageUsage::Sampled) != ImageUsage::None)
	{
		m_SampledIndex = Descriptor::RegisterTexture(m_Info.ImageViewHandle);
	}

	if ((m_Specification.Usage & ImageUsage::Storage) != ImageUsage::None)
	{
		m_StorageImageView = CreateStorageImageView();

		if (!m_Specification.DebugName.empty())
		{
			SetDebugUtilsObjectName(device, VK_OBJECT_TYPE_IMAGE_VIEW, m_Specification.DebugName + " Storage View", m_StorageImageView);
		}

		m_StorageIndex = Descriptor::RegisterStorageImage(m_StorageImageView);
	}
}

void Image::Destroy()
{
	const VkDevice device = RendererContext::Get().GetDevice();

	if (m_SampledIndex != Descriptor::INVALID_INDEX)
	{
		Descriptor::UnregisterTexture(m_SampledIndex);
		m_SampledIndex = Descriptor::INVALID_INDEX;
	}

	if (m_StorageIndex != Descriptor::INVALID_INDEX)
	{
		Descriptor::UnregisterStorageImage(m_StorageIndex);
		m_StorageIndex = Descriptor::INVALID_INDEX;
	}

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

	if (m_StorageImageView != VK_NULL_HANDLE)
	{
		vkDestroyImageView(device, m_StorageImageView, nullptr);
		m_StorageImageView = VK_NULL_HANDLE;
	}

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

VkImageView Image::CreateImageView(uint32_t baseMip, uint32_t mipCount, uint32_t baseLayer, uint32_t layerCount) const
{
	assert(m_Info.ImageHandle != VK_NULL_HANDLE);

	assert(baseMip < m_Specification.Mips);
	assert(baseLayer < GetLayerCount());

	assert(baseMip + mipCount <= m_Specification.Mips);
	assert(baseLayer + layerCount <= GetLayerCount());

	VkImageViewCreateInfo viewInfo
	{
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = m_Info.ImageHandle,
		.viewType = GetImageViewType(m_Specification.Type),
		.format = ToVulkan(m_Specification.Format),

		.subresourceRange =
		{
			.aspectMask = GetAspectMask(m_Specification.Format),
			.baseMipLevel = baseMip,
			.levelCount = mipCount,
			.baseArrayLayer = baseLayer,
			.layerCount = layerCount
		}
	};

	VkImageView imageView = VK_NULL_HANDLE;

	VK_CHECK(vkCreateImageView(RendererContext::Get().GetDevice(), &viewInfo, nullptr, &imageView));

	return imageView;
}

VkImageView Image::CreateStorageImageView() const
{
	assert(m_Info.ImageHandle != VK_NULL_HANDLE);

	VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D;

	switch (m_Specification.Type)
	{
		case ImageType::Image2D:
			viewType = VK_IMAGE_VIEW_TYPE_2D;
			break;

		case ImageType::Image3D:
			viewType = VK_IMAGE_VIEW_TYPE_3D;
			break;

		case ImageType::Cube:
			viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
			break;
	}

	VkImageViewCreateInfo viewInfo
	{
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = m_Info.ImageHandle,
		.viewType = viewType,
		.format = ToVulkan(m_Specification.Format),

		.subresourceRange =
		{
			.aspectMask = GetAspectMask(m_Specification.Format),
			.baseMipLevel = 0,
			.levelCount = m_Specification.Mips,
			.baseArrayLayer = 0,
			.layerCount = GetLayerCount()
		}
	};

	VkImageView imageView = VK_NULL_HANDLE;

	VK_CHECK(vkCreateImageView(RendererContext::Get().GetDevice(), &viewInfo, nullptr, &imageView));

	return imageView;
}

void Image::CreatePerLayerImageViews()
{
	const uint32_t layerCount = GetLayerCount();

	assert(layerCount > 1);

	if (!m_PerLayerImageViews.empty())
		return;

	m_PerLayerImageViews.resize(layerCount, VK_NULL_HANDLE);

	const VkDevice device = RendererContext::Get().GetDevice();

	for (uint32_t layer = 0; layer < layerCount; layer++)
	{
		VkImageViewCreateInfo viewInfo
		{
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = m_Info.ImageHandle,
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = ToVulkan(m_Specification.Format),

			.subresourceRange =
			{
				.aspectMask = GetAspectMask(m_Specification.Format),
				.baseMipLevel = 0,
				.levelCount = m_Specification.Mips,
				.baseArrayLayer = layer,
				.layerCount = 1
			}
		};

		VK_CHECK(vkCreateImageView(device, &viewInfo, nullptr, &m_PerLayerImageViews[layer]));

		if (!m_Specification.DebugName.empty())
		{
			SetDebugUtilsObjectName(device, VK_OBJECT_TYPE_IMAGE_VIEW, m_Specification.DebugName + " Layer " + std::to_string(layer), m_PerLayerImageViews[layer]);
		}
	}
}

VkImageView Image::GetLayerImageView(uint32_t layer) const
{
	assert(layer < m_PerLayerImageViews.size());

	return m_PerLayerImageViews[layer];
}

VkImageView Image::GetMipImageView(uint32_t mip)
{
	assert(mip < m_Specification.Mips);

	const auto it = m_PerMipImageViews.find(mip);

	if (it != m_PerMipImageViews.end())
		return it->second;

	VkImageView imageView = CreateImageView(mip, 1, 0, GetLayerCount());

	m_PerMipImageViews[mip] = imageView;

	if (!m_Specification.DebugName.empty())
	{
		SetDebugUtilsObjectName(RendererContext::Get().GetDevice(), VK_OBJECT_TYPE_IMAGE_VIEW, m_Specification.DebugName + " Mip " + std::to_string(mip), imageView);
	}

	return imageView;
}

void ImageView::Create(const ImageViewSpecification& specification)
{
	Destroy();

	assert(specification.Image);

	m_Specification = specification;

	const uint32_t layerCount = m_Specification.Image->GetLayerCount();

	if (m_Specification.Storage)
	{
		VkImageViewCreateInfo viewInfo
		{
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = m_Specification.Image->GetHandle(),
			.viewType = m_Specification.Image->GetType() == ImageType::Cube ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D,
			.format = ToVulkan(m_Specification.Image->GetFormat()),

			.subresourceRange =
			{
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = m_Specification.Mip,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = layerCount
			}
		};

		VK_CHECK(vkCreateImageView(RendererContext::Get().GetDevice(), &viewInfo, nullptr, &m_ImageView));
	}
	else
	{
		m_ImageView = m_Specification.Image->CreateImageView(m_Specification.Mip, 1, 0, layerCount);
	}
}

void ImageView::Destroy()
{
	if (m_ImageView != VK_NULL_HANDLE)
	{
		vkDestroyImageView(RendererContext::Get().GetDevice(), m_ImageView, nullptr);
		m_ImageView = VK_NULL_HANDLE;
	}
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
