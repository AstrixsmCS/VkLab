#include "ComputePipeline.hpp"

#include "RendererContext.hpp"

#include "Descriptors.hpp"

#include <cassert>
#include <vector>

void ComputePipeline::Create(
	const ComputePipelineSpecification& specification
)
{
	m_Specification = specification;

	VkDevice device = RendererContext::Get().GetDevice();

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Shader Stage
	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	assert(m_Specification.Shader);
	assert(m_Specification.Shader->IsValid());

	const auto& shaderStages = m_Specification.Shader->GetStageInfos();

	assert(shaderStages.size() == 1 && "Compute pipeline requires exactly one shader stage!");

	const VkPipelineShaderStageCreateInfo& shaderStage = shaderStages.front();

	assert(shaderStage.stage == VK_SHADER_STAGE_COMPUTE_BIT && "Compute pipeline requires a compute shader!");

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Pipeline Layout
	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	CreatePipelineLayout();

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Compute Pipeline
	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	const VkComputePipelineCreateInfo pipelineInfo
	{
		.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,

		.stage = shaderStage,
		.layout = m_PipelineLayout
	};

	VK_CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_Pipeline));

	if (!m_Specification.DebugName.empty())
	{
		SetDebugUtilsObjectName(device, VK_OBJECT_TYPE_PIPELINE, m_Specification.DebugName, m_Pipeline);
	}
}

void ComputePipeline::CreatePipelineLayout()
{
	VkDevice device = RendererContext::Get().GetDevice();

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Push Constants
	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	const auto& reflectedPushConstants = m_Specification.Shader->GetPushConstantRanges();

	std::vector<VkPushConstantRange> pushConstantRanges;
	pushConstantRanges.reserve(reflectedPushConstants.size());

	for (const PushConstantRange& range : reflectedPushConstants)
	{
		pushConstantRanges.push_back(
		{
			.stageFlags = range.StageFlags,
			.offset = range.Offset,
			.size = range.Size
		});
	}

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Descriptor Layout
	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	const VkDescriptorSetLayout descriptorSetLayout = Descriptor::GetLayout();

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Pipeline Layout
	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	const VkPipelineLayoutCreateInfo layoutInfo
	{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &descriptorSetLayout,
		.pushConstantRangeCount = static_cast<uint32_t>(pushConstantRanges.size()),
		.pPushConstantRanges = pushConstantRanges.empty() ? nullptr : pushConstantRanges.data()
	};

	VK_CHECK(vkCreatePipelineLayout(device, &layoutInfo, nullptr,&m_PipelineLayout));

	if (!m_Specification.DebugName.empty())
	{
		const std::string layoutName = m_Specification.DebugName + " Layout";
		SetDebugUtilsObjectName(device, VK_OBJECT_TYPE_PIPELINE_LAYOUT, layoutName, m_PipelineLayout);
	}
}

void ComputePipeline::Bind(VkCommandBuffer commandBuffer) const
{
	assert(m_Pipeline != VK_NULL_HANDLE);

	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_Pipeline);
}

void ComputePipeline::Shutdown()
{
	VkDevice device = RendererContext::Get().GetDevice();

	if (m_Pipeline != VK_NULL_HANDLE)
	{
		vkDestroyPipeline(device, m_Pipeline, nullptr);
		m_Pipeline = VK_NULL_HANDLE;
	}

	if (m_PipelineLayout != VK_NULL_HANDLE)
	{
		vkDestroyPipelineLayout(device, m_PipelineLayout, nullptr);
		m_PipelineLayout = VK_NULL_HANDLE;
	}
}
