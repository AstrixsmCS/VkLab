#pragma once

#include "Image.hpp"
#include "Descriptors.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <memory>

struct TextureSpecification
{
	std::string DebugName;

	ImageType Type = ImageType::Image2D;
	Format Format = Format::RGBA8_UNorm;
	ImageUsage Usage = ImageUsage::Sampled;

	uint32_t Width = 1;
	uint32_t Height = 1;
	uint32_t Depth = 1;

	bool GenerateMips = true;
};

class Texture
{
public:
	Texture() = default;
	~Texture() = default;

	Texture(const Texture&) = delete;
	Texture& operator=(const Texture&) = delete;

	void Create(const TextureSpecification& specification);
	void Create(const TextureSpecification& specification, const void* data);
	void Create(const TextureSpecification& specification, const std::filesystem::path& path);

	void GenerateMips();

	void Destroy();

	bool IsValid() const { return m_Image.IsValid(); }

	const Image& GetImage() const { return m_Image; }
	Image& GetImage() { return m_Image; }

	VkImageView GetImageView() const { return m_Image.GetView(); }

	uint32_t GetWidth() const { return m_Image.GetWidth(); }
	uint32_t GetHeight() const { return m_Image.GetHeight(); }
	uint32_t GetDepth() const { return m_Image.GetDepth(); }

	uint32_t GetMipCount() const { return m_Image.GetMipCount(); }

	Format GetFormat() const { return m_Image.GetFormat(); }
	ImageType GetType() const { return m_Image.GetType(); }

	uint32_t GetTextureIndex() const { return m_Image.GetSampledIndex(); }

	const TextureSpecification& GetSpecification() const { return m_Specification; }

private:
	void SetData(const void* data, size_t size);

private:
	TextureSpecification m_Specification;

	Image m_Image;
};
