#pragma once

#include "Vulkan.hpp"
#include "Shader.hpp"

struct ComputePipelineSpecification
{
	std::shared_ptr<Shader> Shader;
	std::string DebugName;
};

class ComputePipeline
{
public:
	void Create(const ComputePipelineSpecification& specification);
	void Shutdown();

	void Bind(VkCommandBuffer commandBuffer) const;

	const ComputePipelineSpecification& GetSpecification() const { return m_Specification;}

	VkPipeline GetPipeline() const { return m_Pipeline; }
	VkPipelineLayout GetLayout() const { return m_PipelineLayout; }

private:
	void CreatePipelineLayout();
private:
	ComputePipelineSpecification m_Specification;

	VkPipeline m_Pipeline = VK_NULL_HANDLE;
	VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
};
