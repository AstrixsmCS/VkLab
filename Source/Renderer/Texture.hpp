#pragma once

#include "Image.hpp"
#include "Descriptors.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

struct TextureSpecification
{
	std::string DebugName;

	Format Format = Format::RGBA8_UNorm;

	SamplerFilter Filter = SamplerFilter::Linear;
	SamplerWrap Wrap = SamplerWrap::Repeat;

	bool GenerateMips = true;
};

class Texture
{
public:
	Texture() = default;
	~Texture() = default;

	Texture(const Texture&) = delete;
	Texture& operator=(const Texture&) = delete;

	void Create(const TextureSpecification& specification, const void* data, uint32_t width, uint32_t height);
	void Create(const TextureSpecification& specification, const std::filesystem::path& path);

	void Destroy();

	bool IsValid() const { return m_Image.IsValid(); }

	const Image& GetImage() const { return m_Image; }
	Image& GetImage() { return m_Image; }

	VkSampler GetSampler() const { return m_Sampler; }

	uint32_t GetWidth() const { return m_Image.GetWidth(); }
	uint32_t GetHeight() const { return m_Image.GetHeight(); }

	uint32_t GetMipCount() const { return m_Image.GetMipCount(); }

	Format GetFormat() const { return m_Image.GetFormat(); }

	uint32_t GetBindlessIndex() const { return m_BindlessIndex; }

	const TextureSpecification& GetSpecification() const { return m_Specification; }
private:
	void Upload(const void* data, size_t size);
	void GenerateMips();
	void CreateSampler();

	// TODO: temp - move to a proper upload/transfer context
	VkCommandBuffer BeginTransientCommandBuffer();
	void EndTransientCommandBuffer(VkCommandBuffer commandBuffer);
private:
	TextureSpecification m_Specification;

	Image m_Image;

	VkSampler m_Sampler = VK_NULL_HANDLE;

	uint32_t m_TextureIndex = Descriptor::INVALID_INDEX;
	uint32_t m_SamplerIndex = Descriptor::INVALID_INDEX;

	VkCommandPool m_TransientCommandPool = VK_NULL_HANDLE; //TODO: temp
};
