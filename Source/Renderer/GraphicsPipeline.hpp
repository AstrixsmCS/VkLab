#pragma once

#include "Vulkan.hpp"
#include "Shader.hpp"
#include "Buffer.hpp"

struct PipelineSpecification
{
	std::shared_ptr<Shader> Shader;

	std::vector<Format> ColorFormats;
	Format DepthFormat = Format::Invalid;
	bool DepthTest = true;
	bool DepthWrite = true;
	CompareOp DepthCompareOp = CompareOp::Less;

	bool BlendEnabled = false;
	BlendFactor SrcColorBlendFactor = BlendFactor::SrcAlpha;
	BlendFactor DstColorBlendFactor = BlendFactor::OneMinusSrcAlpha;

	VertexBufferLayout Layout;

	CullMode CullMode = CullMode::Back;
	WindingMode FrontFace = WindingMode::CCW;
	Topology Topology = Topology::Triangle;
	PolygonMode PolygonMode = PolygonMode::Fill;

	std::string DebugName;
};

class Pipeline
{
public:
	void Create(const PipelineSpecification& specification);
	void Shutdown();

	void Bind(VkCommandBuffer commandBuffer) const;

	const PipelineSpecification& GetSpecification() const { return m_Specification; }

	VkPipeline GetPipeline() const { return m_Pipeline; }
	VkPipelineLayout GetLayout() const { return m_PipelineLayout; }
private:
	void CreatePipelineLayout();
private:
	PipelineSpecification m_Specification;

	VkPipeline m_Pipeline = VK_NULL_HANDLE;
	VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
};
