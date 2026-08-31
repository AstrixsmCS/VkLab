#pragma once

#include "Renderer/RendererContext.hpp"
#include "Renderer/Allocator.hpp"
#include "Renderer/SwapChain.hpp"
#include "Renderer/Shader.hpp"
#include "Renderer/TimelineSemaphore.hpp"
#include "Renderer/DynamicRendering.hpp"
#include "Renderer/GraphicsPipeline.hpp"
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

struct CameraData
{
	glm::mat4 ViewProjection{ 1.0f };
	glm::vec3 CameraPosition{ 0.0f };
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

struct PushConstants
{
	glm::mat4       Model{ 1.0f };

	VkDeviceAddress Camera = 0;

	VkDeviceAddress MaterialBuffer = 0;
	uint32_t        MaterialIndex  = 0;

	uint32_t        _Pad0;
	uint32_t        _Pad1;
	uint32_t        _Pad2;

	glm::vec4       LightDirection;
	glm::vec4       LightColorIntensity;

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
	bool CreateShader();
	bool CreateGraphicsPipeline();
	bool LoadMesh();

	void CreateDefaultSamplers();
	void DestroyDefaultSamplers();

	const std::shared_ptr<Sampler>& GetDefaultSampler(DefaultSampler sampler) const { return m_DefaultSamplers[static_cast<size_t>(sampler)]; }

	void CreateDepthImage();
	void DestroyDepthImage();

	void Render();
private:
	SDL_Window* m_Window  = nullptr;
	uint32_t    m_Width   = 1280;
	uint32_t    m_Height  = 720;
	bool        m_Running = false;

	Renderer  m_Renderer;

	Pipeline m_Pipeline;

	std::shared_ptr<Shader> m_Shader;

	Mesh m_Mesh;

	std::vector<uint32_t> m_MaterialIndices; // Material indices parallel to m_Mesh.GetMaterials()

	DirectionalLight m_DirectionalLight;

	ImGuiLayer m_ImGui;

	Camera        m_Camera;
	UniformBuffer m_CameraBuffer;

	Image m_DepthImage;

	std::array<std::shared_ptr<Sampler>, static_cast<size_t>(DefaultSampler::Count)> m_DefaultSamplers;
};
