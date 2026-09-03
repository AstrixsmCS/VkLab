#include "Material.hpp"

#include <algorithm>
#include <cstring>

void Material::SetShader(std::shared_ptr<Shader> shader)
{
	m_Shader = std::move(shader);
	InitializeStorage();
	MarkDirty();
}

void Material::UpdateGPUData()
{
	auto GetIndex = [&](MapType type) -> uint32_t
	{
		for (const auto& map : m_Maps)
		{
			if (map.Type != type)
				continue;

			if (!map.Texture || !map.UseMap || !map.UseTexture)
				return 0u;

			const uint32_t index = map.Texture->GetTextureIndex();

			return index == Descriptor::INVALID_INDEX ? 0u : index;
		}

		return 0u;
	};

	m_GPUData.AlbedoIndex = GetIndex(MapType::Albedo);
	m_GPUData.NormalIndex = GetIndex(MapType::Normal);
	m_GPUData.MetallicRoughnessIndex = GetIndex(MapType::MetallicRoughness);
	m_GPUData.OcclusionIndex = GetIndex(MapType::Occlusion);

	m_GPUData.AlphaCutoff = m_RenderMode == RenderMode::Cutout ? m_AlphaCutoff : 0.0f;

	m_GPUData.Flags = 0;

	// bits 0-3: texture presence
	if (m_GPUData.NormalIndex != 0)
		m_GPUData.Flags |= (1u << 0);

	if (m_GPUData.MetallicRoughnessIndex != 0)
		m_GPUData.Flags |= (1u << 1);

	if (m_GPUData.OcclusionIndex != 0)
		m_GPUData.Flags |= (1u << 2);

	if (m_GPUData.AlbedoIndex != 0)
		m_GPUData.Flags |= (1u << 3);

	// bits 16-23: UV indices
	auto PackUVIndex = [&](MapType type, uint32_t bitOffset)
	{
		for (const auto& map : m_Maps)
		{
			if (map.Type != type)
				continue;

			if (!map.Texture || !map.UseMap || !map.UseTexture)
				return;

			m_GPUData.Flags |= ((map.UvIndex & 0x3u) << bitOffset);
			return;
		}
	};

	PackUVIndex(MapType::Albedo,             16);
	PackUVIndex(MapType::Normal,             18);
	PackUVIndex(MapType::MetallicRoughness,  20);
	PackUVIndex(MapType::Occlusion,          22);
}

Material::Material(std::shared_ptr<Shader> shader)
	: m_Shader(std::move(shader))
{
	InitializeStorage();
}

void Material::InitializeStorage()
{
	m_UniformStorage.clear();

	if (!m_Shader)
		return;

	const auto& ranges = m_Shader->GetPushConstantRanges();

	if (ranges.empty())
		return;

	uint32_t storageSize = 0;

	for (const auto& range : ranges)
		storageSize = std::max(storageSize, range.Offset + range.Size);

	if (storageSize == 0)
		return;

	m_UniformStorage.resize(storageSize);
	std::memset(m_UniformStorage.data(), 0, m_UniformStorage.size());
}

bool Material::SetUniformData(const std::string& name, const void* data, uint32_t size)
{
	if (!m_Shader)
		return false;

	if (m_UniformStorage.empty())
		InitializeStorage();

	if (m_UniformStorage.empty())
		return false;

	const auto& members = m_Shader->GetPushConstantMembers();

	for (const auto& member : members)
	{
		if (member.Name != name)
			continue;

		if (member.Size != size)
			return false;

		if (member.Offset + size > m_UniformStorage.size())
			return false;

		std::memcpy(m_UniformStorage.data() + member.Offset, data, size);

		return true;
	}

	return false;
}

bool Material::GetUniformData(const std::string& name, void* outData, uint32_t size) const
{
	if (!m_Shader || m_UniformStorage.empty())
		return false;

	const auto& members = m_Shader->GetPushConstantMembers();

	for (const auto& member : members)
	{
		if (member.Name != name)
			continue;

		if (member.Size != size)
			return false;

		if (member.Offset + size > m_UniformStorage.size())
			return false;

		std::memcpy(outData, m_UniformStorage.data() + member.Offset, size);

		return true;
	}

	return false;
}

const char* Material::ToString(MapType type)
{
	switch (type)
	{
		case MapType::Albedo: return "Albedo";
		case MapType::Normal: return "Normal";
		case MapType::MetallicRoughness: return "MetallicRoughness";
		case MapType::Occlusion: return "Occlusion";

		default: return "Unknown";
	}
}
