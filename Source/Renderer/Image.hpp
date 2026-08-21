#pragma once

#include "Vulkan.hpp"
#include "Allocator.hpp"

#include <vma/vk_mem_alloc.h>

#include <cstdint>
#include <string>

enum class ImageUsage
{
	None = 0,
	Texture,
	Attachment,
	Storage
};

struct ImageSpecification
{
	std::string DebugName;

	Format Format = Format::RGBA8_UNorm;
	ImageUsage Usage = ImageUsage::Texture;

	uint32_t Width = 1;
	uint32_t Height = 1;

	uint32_t Mips = 1;
	uint32_t Layers = 1;

	bool Transfer = false;
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

	bool IsValid() const { return m_Image != VK_NULL_HANDLE; }

	VkImage GetImage() const { return m_Image; }
	VkImageView GetView() const { return m_ImageView; }

	VmaAllocation GetAllocation() const { return m_Allocation; }

	uint32_t GetWidth() const { return m_Specification.Width; }
	uint32_t GetHeight() const { return m_Specification.Height; }

	uint32_t GetMipCount() const { return m_Specification.Mips; }
	uint32_t GetLayerCount() const { return m_Specification.Layers; }

	Format GetFormat() const { return m_Specification.Format; }

	const ImageSpecification& GetSpecification() const
	{
		return m_Specification;
	}

private:
	ImageSpecification m_Specification;

	VkImage m_Image = VK_NULL_HANDLE;
	VkImageView m_ImageView = VK_NULL_HANDLE;

	VmaAllocation m_Allocation = VK_NULL_HANDLE;
};
