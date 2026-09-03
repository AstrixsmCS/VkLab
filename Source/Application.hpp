#pragma once

#include "Renderer/RendererContext.hpp"
#include "Renderer/Allocator.hpp"
#include "Renderer/SwapChain.hpp"
#include "Renderer/Shader.hpp"
#include "Renderer/TimelineSemaphore.hpp"
#include "Renderer/DynamicRendering.hpp"
#include "Renderer/GraphicsPipeline.hpp"
#include "Renderer/ComputePipeline.hpp"
#include "Renderer/Buffer.hpp"
#include "Renderer/Renderer.hpp"
#include "Renderer/Mesh.hpp"

#include "Renderer/Image.hpp"
#include "Renderer/Texture.hpp"
#include "Renderer/Descriptors.hpp"
#include "Renderer/Camera.hpp"
#include "Renderer/MaterialSystem.hpp"
#include "Renderer/ImGui.hpp"

#include <string>
#include <array>

struct SDL_Window;

struct UBCamera
{
	glm::mat4 ViewProjection{ 1.0f };
	glm::mat4 InverseViewProjection{ 1.0f };

	glm::vec3 CameraPosition{ 0.0f };
	float _Pad0 = 0.0f;
};

enum class DefaultSampler
{
	LinearRepeat = 0,
	LinearClamp,
	NearestClamp,
	AnisotropicRepeat,
	ShadowCompare,

	Count
};

struct DirectionalLight
{
	glm::vec3 Direction{ -1.0f, -1.0f, -1.0f };
	glm::vec3 Color{ 1.0f, 1.0f, 1.0f };
	float Intensity = 3.0f;
};

enum class ToneMapper : uint32_t
{
	Reinhard = 0,
	ACES,
	Uncharted2
};

class Application
{
public:
	bool Initialize();
	void Shutdown();
	void Run();
private:
	void ShowError(const std::string& errorMessage) const;

	bool InitializeVulkan();

	bool CreateGeometryPass();
	bool CreateSkyboxPass();
	bool CreateToneMappingPass();
	void RenderGeometryPass(VkCommandBuffer cmd, const UniformBuffer& cameraBuffer);
	void RenderSkyboxPass(VkCommandBuffer cmd, const UniformBuffer& cameraBuffer);
	void RenderToneMappingPass(VkCommandBuffer cmd, const VkExtent2D& extent);

	bool LoadMesh();

	void CreateDefaultSamplers();
	void DestroyDefaultSamplers();

	const std::shared_ptr<Sampler>& GetDefaultSampler(DefaultSampler sampler) const { return m_DefaultSamplers[static_cast<size_t>(sampler)]; }

	void CreateDepthImage();
	void DestroyDepthImage();

	void CreateSceneImage();
	void DestroySceneImage();

	void Render();
private:
	SDL_Window* m_Window  = nullptr;
	uint32_t    m_Width   = 1280;
	uint32_t    m_Height  = 720;
	bool        m_Running = false;

	ToneMapper m_ToneMapper = ToneMapper::ACES;
	float m_Exposure = 1.0f;
	float m_WhitePoint = 4.0f;

	int m_SkyboxLod = 0.0f;

	Renderer  m_Renderer;

	Pipeline m_GeometryPipeline;
	Pipeline m_SkyboxPipeline;
	ComputePipeline m_EquirectangularToCubemapPipeline;
	Pipeline m_ToneMappingPipeline;

	Material m_GeometryMaterial;
	Material m_SkyboxMaterial;
	Material m_EquirectangularToCubemapMaterial;
	Material m_ToneMappingMaterial;

	Texture m_EnvironmentTexture;

	Mesh m_Mesh;

	std::vector<uint32_t> m_MaterialIndices; // Material indices parallel to m_Mesh.GetMaterials()

	DirectionalLight m_DirectionalLight;

	ImGuiLayer m_ImGui;

	Camera m_Camera;
	std::array<UniformBuffer, Renderer::GetFramesInFlight()> m_CameraBuffers;

	Image m_DepthImage;
	Image m_SceneImage;

	std::array<std::shared_ptr<Sampler>, static_cast<size_t>(DefaultSampler::Count)> m_DefaultSamplers;
};
