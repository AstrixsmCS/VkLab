#pragma once

#include "Shader.hpp"
#include "Texture.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <ostream>
#include <vector>

enum class MapType : uint8_t
{
	Albedo = 0,
	Normal,
	MetallicRoughness,
	Occlusion,
};

struct MapInfo
{
	std::shared_ptr<Texture> Texture;

	MapType Type;
	uint32_t UvIndex = 0;
	bool UseMap = true;
	bool UseTexture = true;
};

// GPU-friendly material data (scalar); must match the shader-side struct.
struct GPUMaterialData
{
	glm::vec4 Albedo{ 1.0f, 1.0f, 1.0f, 1.0f };

	// Texture Indices (Bindless; slot 0 = reserved null/white)
	uint32_t AlbedoIndex = 0;
	uint32_t NormalIndex = 0;
	uint32_t MetallicRoughnessIndex = 0;
	uint32_t OcclusionIndex = 0;

	// Factors
	float Metalness = 0.0f;
	float Roughness = 0.5f;
	float AlphaCutoff = 0.5f;
	uint32_t Flags = 0;
};
static_assert(sizeof(GPUMaterialData) == 48);

class Material {
public:
	Material() = default;
	explicit Material(std::shared_ptr<Shader> shader);

	enum class RenderMode { Opaque, Cutout, Transparent, Fade };
	enum class BlendFactor { Zero, One, SrcAlpha, OneMinusSrcAlpha, DstAlpha, OneMinusDstAlpha };
	enum class CullMode { Back, Front, None };

	// Shader
	void SetShader(std::shared_ptr<Shader> shader);
	const std::shared_ptr<Shader>& GetShader() const { return m_Shader; }

	// Map management
	void AddTexture(const MapInfo& texture)
	{
		m_Maps.push_back(texture);
		MarkDirty();
	}

	void SetTexture(const MapInfo& texture)
	{
		for (auto& map : m_Maps)
		{
			if (map.Type == texture.Type)
			{
				map = texture;
				MarkDirty();
				return;
			}
		}

		m_Maps.push_back(texture);
		MarkDirty();
	}

	const std::vector<MapInfo>& GetTextures() const { return m_Maps; }

	std::optional<uint32_t> GetUVIndex(MapType type) const
	{
		for (const auto& tex : m_Maps)
		{
			if (tex.Type == type) return tex.UvIndex;
		}
		return std::nullopt;
	}

	void EnableUseMap(MapType type, bool enable)
	{
		for (auto& tex : m_Maps)
		{
			if (tex.Type == type)
			{
				tex.UseMap = enable;
				MarkDirty();
				return;
			}
		}
	}

	bool IsUseMapEnabled(MapType type) const
	{
		for (const auto& tex : m_Maps)
		{
			if (tex.Type == type) return tex.UseMap;
		}
		return false;
	}

	void EnableUseTexture(MapType type, bool enable)
	{
		for (auto& tex : m_Maps)
		{
			if (tex.Type == type)
			{
				tex.UseTexture = enable;
				MarkDirty();
				return;
			}
		}
	}

	bool IsUseTextureEnabled(MapType type) const
	{
		for (const auto& tex : m_Maps)
		{
			if (tex.Type == type) return tex.UseTexture;
		}
		return false;
	}

	// Render mode
	RenderMode GetRenderMode() const { return m_RenderMode; }
	void SetRenderMode(RenderMode mode)
	{
		m_RenderMode = mode;
		MarkDirty();
	}

	// Alpha cutoff for RenderMode::Cutout
	float GetAlphaCutoff() const { return m_AlphaCutoff; }
	void SetAlphaCutoff(float cutoff)
	{
		m_AlphaCutoff = cutoff;
		MarkDirty();
	}

	// Blend factors
	void SetBlendSrc(BlendFactor factor) { m_BlendSrc = factor; }
	BlendFactor GetBlendSrc() const { return m_BlendSrc; }

	void SetBlendDst(BlendFactor factor) { m_BlendDst = factor; }
	BlendFactor GetBlendDst() const { return m_BlendDst; }

	// Face culling
	CullMode GetCullMode() const { return m_CullMode; }
	void SetCullMode(CullMode mode) { m_CullMode = mode; }

	void EnableAlphaFromAlbedo(bool enable) { m_AlphaFromAlbedo = enable; }
	bool IsAlphaFromAlbedoEnabled() const { return m_AlphaFromAlbedo; }

	// Albedo color
	glm::vec4 GetColor() const { return m_GPUData.Albedo; }
	void SetColor(const glm::vec4& color)
	{
		m_GPUData.Albedo = color;
		MarkDirty();
	}

	// Metalness / roughness
	float GetMetalness() const { return m_GPUData.Metalness; }
	void SetMetalness(float metalness)
	{
		m_GPUData.Metalness = metalness;
		MarkDirty();
	}
	float GetRoughness() const { return m_GPUData.Roughness; }
	void SetRoughness(float roughness)
	{
		m_GPUData.Roughness = roughness;
		MarkDirty();
	}

	// GPU Data Access
	const GPUMaterialData& GetGPUData() const { return m_GPUData; }
	void UpdateGPUData(); // Updates m_GPUData from internal state/maps

	// Dirty tracking
	bool IsGpuDirty() const { return m_GpuDirty; }
	bool NeedsSave()  const { return m_NeedsSave; }
	void MarkDirty()        { m_GpuDirty = true; m_NeedsSave = true; }
	void ClearGpuDirty()    { m_GpuDirty = false; }
	void ClearNeedsSave()   { m_NeedsSave = false; }

	static const char* ToString(MapType type);
private:
	std::shared_ptr<Shader> m_Shader;

	std::vector<MapInfo> m_Maps;

	GPUMaterialData m_GPUData;

	RenderMode m_RenderMode = RenderMode::Opaque;
	BlendFactor m_BlendSrc = BlendFactor::SrcAlpha;
	BlendFactor m_BlendDst = BlendFactor::OneMinusSrcAlpha;
	float m_AlphaCutoff = 0.5f;
	CullMode m_CullMode = CullMode::Back;
	bool m_AlphaFromAlbedo = false;
	bool m_GpuDirty  = true;
	bool m_NeedsSave = false;
};

inline std::ostream& operator<<(std::ostream& os, const MapType type)
{
	return os << Material::ToString(type);
}
