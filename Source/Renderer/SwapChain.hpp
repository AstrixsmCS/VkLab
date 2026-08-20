#pragma once

#include "Allocator.hpp"

#include "Vulkan.hpp"

#include <cstdint>
#include <vector>

struct SDL_Window;

class SwapChain
{
public:
	SwapChain(SDL_Window* windowHandle);
	~SwapChain();

	void Initialize();
	void Cleanup();

	void OnResize(uint32_t width, uint32_t height);

	uint32_t AcquireNextImage(VkSemaphore signalSemaphore);
	void Present(VkSemaphore waitSemaphore);

	uint32_t GetImageCount() const { return static_cast<uint32_t>(m_Images.size()); }
	VkImage GetImage(uint32_t index) const { return m_Images[index].Image; }
	VkImageView GetImageView(uint32_t index) const { return m_Images[index].ImageView; }

	VkFormat GetColorFormat() const { return m_ColorFormat; }
	VkExtent2D GetExtent() const {return m_Extent;}

	uint32_t GetCurrentImageIndex() const {return m_CurrentImageIndex;}

	VkImage GetCurrentImage() const { return m_Images[m_CurrentImageIndex].Image; }
	VkImageView GetCurrentImageView() const { return m_Images[m_CurrentImageIndex].ImageView; }
private:
	void CreateSurface();
	void CreateSwapchain(uint32_t* width, uint32_t* height);
	void CreateImageViews();

	void FindImageFormatAndColorSpace();
private:
	struct SwapchainImage
	{
		VkImage Image = VK_NULL_HANDLE;
		VkImageView ImageView = VK_NULL_HANDLE;
	};
	std::vector<SwapchainImage> m_Images;

	SDL_Window* m_WindowHandle = nullptr;

	VkSurfaceKHR m_Surface = VK_NULL_HANDLE;
	VkSwapchainKHR m_SwapChain = VK_NULL_HANDLE;

	VkFormat m_ColorFormat = VK_FORMAT_UNDEFINED;
	VkColorSpaceKHR m_ColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
	VkExtent2D m_Extent{};

	uint32_t m_CurrentImageIndex = 0;    // Index of the current swapchain image.  Can be different from frame index

	bool m_NeedsResize = false;
};
