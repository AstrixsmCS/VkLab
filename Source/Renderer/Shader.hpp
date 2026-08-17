#pragma once

#include "Vulkan.hpp"
#include "RendererTypes.hpp"

#include <filesystem>
#include <vector>

class Shader
{
public:
	void Load(const std::filesystem::path& filePath);
	void Shutdown();
	void Reload();

	bool IsValid() const { return m_ShaderModule != VK_NULL_HANDLE; }

	const std::filesystem::path& GetPath() const { return m_Path; }

	VkShaderModule GetShaderModule() const { return m_ShaderModule; }

	const std::vector<VkPipelineShaderStageCreateInfo>& GetStageInfos() const { return m_StageInfos; }
private:
	void CreateShaderModule();
	void CreateStageInfos();
	void Destroy();

	static VkShaderStageFlagBits ToVulkanStage(ShaderStage stage);

private:
	std::filesystem::path m_Path;

	std::vector<uint32_t> m_SpirV;
	std::vector<ShaderStage> m_Stages;

	VkShaderModule m_ShaderModule = VK_NULL_HANDLE;

	std::vector<VkPipelineShaderStageCreateInfo> m_StageInfos;
};
