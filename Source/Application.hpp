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

#include <string>
#include <array>

struct SDL_Window;

struct CameraData
{
	glm::mat4 ViewProjection{ 1.0f };
};

struct PushConstants
{
	VkDeviceAddress CameraAddress = 0;

	alignas(16) glm::mat4 Model{ 1.0f };
};

struct Camera
{
	glm::vec3 Position{ 0.0f, 0.0f, 3.0f };
	glm::vec3 Target  { 0.0f, 0.0f,  0.0f };

	float FovY = glm::radians(60.0f);
	float Near = 0.1f;
	float Far  = 1000.0f;

	CameraData GetData(float aspectRatio) const
	{
		glm::mat4 view = glm::lookAt(Position, Target, glm::vec3(0.0f, 1.0f, 0.0f));
		glm::mat4 proj = glm::perspective(FovY, aspectRatio, Near, Far);

		proj[1][1] *= -1.0f;

		return CameraData{ proj * view };
	}
};

class Application
{
public:
	bool Initialize();
	void Shutdown();
	void Run();
private:
	void ShowError(const std::string& errorMessage) const;

	bool           InitializeVulkan();
	bool           CreateShader();
	bool           CreateGraphicsPipeline();
	bool LoadMesh();

	void CreateDepthImage();
	void DestroyDepthImage();

	void           Render();
private:
	SDL_Window* m_Window  = nullptr;
	uint32_t    m_Width   = 1280;
	uint32_t    m_Height  = 720;
	bool        m_Running = false;

	Renderer  m_Renderer;

	Pipeline m_Pipeline;

	std::shared_ptr<Shader> m_Shader;

	Mesh m_Mesh;

	Camera        m_Camera;
	UniformBuffer m_CameraBuffer;

	Image m_DepthImage;
};
