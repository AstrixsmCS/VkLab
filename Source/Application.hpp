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

#include <string>
#include <array>

struct SDL_Window;

struct Vertex
{
	float Position[3];
	float Color[3];
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
	bool           CreateGeometry();

	void CreateDepthImage();
	void DestroyDepthImage();

	void           Render();
private:
	SDL_Window* m_Window  = nullptr;
	uint32_t    m_Width   = 1280;
	uint32_t    m_Height  = 720;
	bool        m_Running = false;

	// Renderer
	Renderer  m_Renderer;

	// Pipeline
	Pipeline m_Pipeline;

	// Shader
	std::shared_ptr<Shader> m_Shader;

	// Geometry
	VertexBuffer m_VertexBuffer;
	IndexBuffer  m_IndexBuffer;

	struct DepthImage
	{
		VkImage Image = VK_NULL_HANDLE;
		VmaAllocation Allocation = VK_NULL_HANDLE;
		VkImageView ImageView = VK_NULL_HANDLE;
	} m_DepthStencil;
};
