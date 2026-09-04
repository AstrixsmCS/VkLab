#pragma once

#include "Vulkan.hpp"
#include "Allocator.hpp"
#include "Descriptors.hpp"

#include <vma/vk_mem_alloc.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

enum class ImageType : uint8_t
{
	Image2D,
	Image3D,
	Cube
};

enum class ImageUsage : uint32_t
{
	None        = 0,
	Attachment  = 1 << 0,
	Sampled     = 1 << 1,
	Storage     = 1 << 2,
	TransferSrc = 1 << 3,
	TransferDst = 1 << 4
};

constexpr ImageUsage operator|(ImageUsage lhs, ImageUsage rhs)
{
	return static_cast<ImageUsage>(static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
}

constexpr ImageUsage operator&(ImageUsage lhs, ImageUsage rhs)
{
	return static_cast<ImageUsage>(static_cast<uint32_t>(lhs) & static_cast<uint32_t>(rhs));
}

struct ImageSpecification
{
	std::string DebugName;

	ImageType Type = ImageType::Image2D;

	Format Format = Format::RGBA8_UNorm;
	ImageUsage Usage = ImageUsage::Sampled;

	uint32_t Width = 1;
	uint32_t Height = 1;
	uint32_t Depth = 1;

	uint32_t Mips = 1;
};

struct ImageInfo
{
	VkImage ImageHandle = VK_NULL_HANDLE;
	VkImageView ImageViewHandle = VK_NULL_HANDLE;

	VmaAllocation Allocation = VK_NULL_HANDLE;
};

class Image
{
public:
	Image() = default;
	~Image() = default;

	Image(const Image&) = delete;
	Image& operator=(const Image&) = delete;

	void Create(const ImageSpecification& specification);
	void Destroy();

	void Resize(uint32_t width, uint32_t height);

	void CreatePerLayerImageViews();

	bool IsValid() const { return m_Info.ImageHandle != VK_NULL_HANDLE; }

	VkImage GetHandle() const { return m_Info.ImageHandle; }
	VkImageView GetView() const { return m_Info.ImageViewHandle; }

	VkImageView GetLayerImageView(uint32_t layer) const;
	VkImageView GetMipImageView(uint32_t mip);

	uint32_t GetSampledIndex() const { return m_SampledIndex; }
	uint32_t GetStorageIndex() const { return m_StorageIndex; }

	const ImageInfo& GetImageInfo() const { return m_Info; }

	uint32_t GetWidth() const { return m_Specification.Width; }
	uint32_t GetHeight() const { return m_Specification.Height; }
	uint32_t GetDepth() const { return m_Specification.Depth; }

	uint32_t GetMipCount() const { return m_Specification.Mips; }
	uint32_t GetLayerCount() const { return m_Specification.Type == ImageType::Cube ? 6 : 1; }

	Format GetFormat() const { return m_Specification.Format; }
	ImageType GetType() const { return m_Specification.Type; }

	const ImageSpecification& GetSpecification() const { return m_Specification; }

private:
	friend class ImageView;

	VkImageView CreateImageView(uint32_t baseMip, uint32_t mipCount, uint32_t baseLayer, uint32_t layerCount) const;
	VkImageView CreateStorageImageView() const;

private:
	ImageSpecification m_Specification;
	ImageInfo m_Info;

	VkImageView m_StorageImageView = VK_NULL_HANDLE;

	uint32_t m_SampledIndex = Descriptor::INVALID_INDEX;
	uint32_t m_StorageIndex = Descriptor::INVALID_INDEX;

	std::vector<VkImageView> m_PerLayerImageViews;
	std::map<uint32_t, VkImageView> m_PerMipImageViews;
};

struct ImageViewSpecification
{
	std::string DebugName;

	Image* Image = nullptr;

	uint32_t Mip = 0;

	bool Storage = false;
};

class ImageView
{
public:
	ImageView() = default;
	~ImageView() = default;

	ImageView(const ImageView&) = delete;
	ImageView& operator=(const ImageView&) = delete;

	void Create(const ImageViewSpecification& specification);
	void Destroy();

	bool IsValid() const { return m_ImageView != VK_NULL_HANDLE; }

	VkImageView GetHandle() const { return m_ImageView; }

	const ImageViewSpecification& GetSpecification() const { return m_Specification; }

private:
	ImageViewSpecification m_Specification;

	VkImageView m_ImageView = VK_NULL_HANDLE;
};
