#pragma once

#include "Vulkan.hpp"
#include "Allocator.hpp"
#include "Descriptors.hpp"

#include <vma/vk_mem_alloc.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

enum class ImageUsage : uint32_t
{
	None       = 0,
	Attachment = 1 << 0,
	Sampled    = 1 << 1,
	Storage    = 1 << 2
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

	TextureType Type = TextureType::Texture2D;

	Format Format = Format::RGBA8_UNorm;
	ImageUsage Usage = ImageUsage::Sampled;

	uint32_t Width = 1;
	uint32_t Height = 1;
	uint32_t Depth = 1;

	uint32_t Mips = 1;
	uint32_t Layers = 1;

	bool Transfer = false;
};

struct ImageInfo
{
	VkImage ImageHandle = VK_NULL_HANDLE;
	VkImageView ImageViewHandle = VK_NULL_HANDLE;
	VmaAllocation Allocation = VK_NULL_HANDLE;
};

enum class ImageViewType : uint8_t
{
	Auto = 0,
	Image2D,
	Image2DArray,
	Image3D,
	Cube,
	CubeArray
};

struct ImageViewSpecification;

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

	const ImageInfo& GetImageInfo() const { return m_Info; }

	uint32_t GetWidth() const { return m_Specification.Width; }
	uint32_t GetHeight() const { return m_Specification.Height; }

	uint32_t GetDepth() const { return m_Specification.Depth; }

	uint32_t GetMipCount() const { return m_Specification.Mips; }
	uint32_t GetLayerCount() const { return m_Specification.Layers; }

	Format GetFormat() const { return m_Specification.Format; }
	TextureType GetType() const { return m_Specification.Type; }

	const ImageSpecification& GetSpecification() const { return m_Specification; }
private:
	VkImageView CreateImageView(ImageViewType type, uint32_t baseMip, uint32_t mipCount, uint32_t baseLayer, uint32_t layerCount, VkImageAspectFlags aspectMask = 0) const;
private:
	ImageSpecification m_Specification;
	ImageInfo m_Info;

	std::vector<VkImageView> m_PerLayerImageViews;
	std::map<uint32_t, VkImageView> m_PerMipImageViews;
};

struct ImageViewSpecification
{
	std::string DebugName;

	Image* Image = nullptr;

	ImageViewType Type = ImageViewType::Auto;

	uint32_t BaseMip = 0;
	uint32_t MipCount = 0;

	uint32_t BaseLayer = 0;
	uint32_t LayerCount = 0;

	VkImageAspectFlags AspectMask = 0;
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

	bool IsValid() const { return m_Info.ImageViewHandle != VK_NULL_HANDLE; }

	VkImageView GetHandle() const { return m_Info.ImageViewHandle; }

	const ImageInfo& GetImageInfo() const { return m_Info; }

	const ImageViewSpecification& GetSpecification() const { return m_Specification; }
private:
	ImageViewSpecification m_Specification;
	ImageInfo m_Info;
};

struct SamplerSpecification
{
	std::string DebugName;

	SamplerFilter MinFilter = SamplerFilter::Linear;
	SamplerFilter MagFilter = SamplerFilter::Linear;

	SamplerMip Mip = SamplerMip::Linear;

	SamplerWrap WrapU = SamplerWrap::Repeat;
	SamplerWrap WrapV = SamplerWrap::Repeat;
	SamplerWrap WrapW = SamplerWrap::Repeat;

	float MipLodBias = 0.0f;

	float MinLod = 0.0f;
	float MaxLod = VK_LOD_CLAMP_NONE;

	bool Anisotropy = false;
	float MaxAnisotropy = 1.0f;

	bool Compare = false;
	CompareOp CompareOperation = CompareOp::LessEqual;
};

class Sampler
{
public:
	Sampler() = default;
	~Sampler() = default;

	Sampler(const Sampler&) = delete;
	Sampler& operator=(const Sampler&) = delete;

	void Create(const SamplerSpecification& specification);
	void Destroy();

	bool IsValid() const { return m_SamplerHandle != VK_NULL_HANDLE; }

	VkSampler GetHandle() const { return m_SamplerHandle; }

	uint32_t GetDescriptorIndex() const { return m_DescriptorIndex; }

	const SamplerSpecification& GetSpecification() const { return m_Specification; }

private:
	SamplerSpecification m_Specification;

	uint32_t m_DescriptorIndex = Descriptor::INVALID_INDEX;

	VkSampler m_SamplerHandle = VK_NULL_HANDLE;
};
