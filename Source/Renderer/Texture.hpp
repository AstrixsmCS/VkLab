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

	uint32_t Width = 1;
	uint32_t Height = 1;
	uint32_t Depth = 1;

	Format Format = Format::RGBA8_UNorm;

	bool GenerateMips = true;
};

class Texture
{
public:
	Texture() = default;
	~Texture() = default;

	Texture(const Texture&) = delete;
	Texture& operator=(const Texture&) = delete;

	void Create(const TextureSpecification& specification, const void* data);
	void Create(const TextureSpecification& specification, const std::filesystem::path& path);

	void CreateCube(const TextureSpecification& specification, const void* data);
	void Create3D(const TextureSpecification& specification, const void* data);

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

	uint32_t GetTextureIndex() const { return m_TextureIndex; }

	const TextureSpecification& GetSpecification() const { return m_Specification; }
private:
	void SetData(const void* data, size_t size);
private:
	TextureSpecification m_Specification;

	Image m_Image;

	uint32_t m_TextureIndex = Descriptor::INVALID_INDEX;
};
