#include "Swapchain.hpp"

#include "RendererContext.hpp"

#include <SDL3/SDL_vulkan.h>
#include <SDL3/SDL_events.h>

#include <cassert>
#include <algorithm>
#include <print>

void SwapChain::Initialize()
{
	assert(m_WindowHandle && "SwapChain window handle is null!");

	CreateSurface();

	// Block on (0,0) extent (launched-minimized); Vulkan rejects zero-sized swapchains.
	int windowWidth = 0;
	int windowHeight = 0;

	while (windowWidth == 0 || windowHeight == 0)
	{
		SDL_GetWindowSizeInPixels(m_WindowHandle, &windowWidth, &windowHeight);

		if (windowWidth == 0 || windowHeight == 0)
			SDL_WaitEvent(nullptr);
	}

	uint32_t width = static_cast<uint32_t>(windowWidth);
	uint32_t height = static_cast<uint32_t>(windowHeight);

	CreateSwapchain(&width, &height);
}

void SwapChain::Shutdown()
{
	DestroySwapChain();

	if (m_Surface)
	{
		vkDestroySurfaceKHR(RendererContext::GetInstance(), m_Surface, nullptr);
		m_Surface = VK_NULL_HANDLE;
	}
}

void SwapChain::OnResize(uint32_t width, uint32_t height)
{
	if (width == 0 || height == 0)
		return;

	vkDeviceWaitIdle(RendererContext::GetDevice()->GetDevice());

	DestroySwapChain();

	CreateSwapchain(&width, &height);

	m_NeedsResize = false;
}

uint32_t SwapChain::AcquireNextImage(VkSemaphore signalSemaphore)
{
	// Consume a deferred rebuild requested by Present().
	if (m_NeedsResize)
	{
		int windowWidth = 0;
		int windowHeight = 0;

		SDL_GetWindowSizeInPixels(m_WindowHandle, &windowWidth, &windowHeight);

		if (windowWidth > 0 && windowHeight > 0)
		{
			OnResize(static_cast<uint32_t>(windowWidth), static_cast<uint32_t>(windowHeight));
		}

		m_NeedsResize = false;

		return UINT32_MAX;
	}

	VkResult result = vkAcquireNextImageKHR(RendererContext::GetDevice()->GetDevice(), m_SwapChain, UINT64_MAX, signalSemaphore, VK_NULL_HANDLE, &m_CurrentImageIndex);

	if (result == VK_ERROR_OUT_OF_DATE_KHR)
	{
		int windowWidth = 0;
		int windowHeight = 0;

		SDL_GetWindowSizeInPixels(m_WindowHandle, &windowWidth, &windowHeight);

		if (windowWidth > 0 && windowHeight > 0)
		{
			OnResize(static_cast<uint32_t>(windowWidth), static_cast<uint32_t>(windowHeight));
		}

		return UINT32_MAX;
	}

	if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
	{
		VK_CHECK(result);
		return UINT32_MAX;
	}

	return m_CurrentImageIndex;
}

void SwapChain::Present(VkSemaphore waitSemaphore)
{
	VkPresentInfoKHR presentInfo
	{
		.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &waitSemaphore,
		.swapchainCount = 1,
		.pSwapchains = &m_SwapChain,
		.pImageIndices = &m_CurrentImageIndex
	};

	VkResult result = vkQueuePresentKHR(RendererContext::GetDevice()->GetGraphicsQueue(), &presentInfo);

	// Defer recreation until AcquireNextImage().
	if (result == VK_ERROR_OUT_OF_DATE_KHR)
	{
		m_NeedsResize = true;
	}
	else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
	{
		VK_CHECK(result);
	}
}

void SwapChain::CreateSurface()
{
	VkPhysicalDevice physicalDevice = RendererContext::GetPhysicalDevice()->GetPhysicalDevice();

	SDL_Vulkan_CreateSurface(m_WindowHandle, RendererContext::GetInstance(), nullptr, &m_Surface);

	VkBool32 presentSupport = VK_FALSE;

	VK_CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, static_cast<uint32_t>(RendererContext::GetPhysicalDevice()->GetQueueFamilyIndices().Graphics), m_Surface, &presentSupport));

	assert(presentSupport && "Graphics queue family does not support presentation on this surface!");

	FindImageFormatAndColorSpace();
}

void SwapChain::CreateSwapchain(uint32_t* width, uint32_t* height)
{
	VkPhysicalDevice physicalDevice = RendererContext::GetPhysicalDevice()->GetPhysicalDevice();
	VkDevice device = RendererContext::GetDevice()->GetDevice();

	VkSurfaceCapabilitiesKHR capabilities{};
	VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, m_Surface, &capabilities));

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Present Mode
	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	uint32_t presentModeCount = 0;
	VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, m_Surface, &presentModeCount, nullptr));
	std::vector<VkPresentModeKHR> presentModes(presentModeCount);
	VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, m_Surface, &presentModeCount, presentModes.data()));

	// FIFO is always supported.
	VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;

	// Prefer mailbox when available.
	for (VkPresentModeKHR availablePresentMode : presentModes)
	{
		if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR)
		{
			presentMode = availablePresentMode;
			break;
		}
	}

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Extent
	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	if (capabilities.currentExtent.width != UINT32_MAX)
	{
		m_Extent = capabilities.currentExtent;

		*width = m_Extent.width;
		*height = m_Extent.height;
	}
	else
	{
		m_Extent =
		{
			.width = std::clamp(*width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
			.height = std::clamp(*height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height)
		};

		*width = m_Extent.width;
		*height = m_Extent.height;
	}

	if (*width == 0 || *height == 0)
		return;

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Image Count
	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	uint32_t imageCount = std::max(2u, capabilities.minImageCount + 1);

	if (capabilities.maxImageCount > 0)
		imageCount = std::min(imageCount, capabilities.maxImageCount);

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Composite Alpha
	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	VkCompositeAlphaFlagBitsKHR compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;

	constexpr VkCompositeAlphaFlagBitsKHR compositeAlphaFlags[]
	{
		VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
		VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
		VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR
	};

	for (VkCompositeAlphaFlagBitsKHR flag : compositeAlphaFlags)
	{
		if (capabilities.supportedCompositeAlpha & flag)
		{
			compositeAlpha = flag;
			break;
		}
	}

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Swapchain
	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	VkImageUsageFlags imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

	// Useful for blitting/clearing to the swapchain.
	if (capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT)
		imageUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;

	VkSwapchainCreateInfoKHR swapchainInfo
	{
		.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		.surface = m_Surface,
		.minImageCount = imageCount,
		.imageFormat = m_ColorFormat,
		.imageColorSpace = m_ColorSpace,
		.imageExtent = m_Extent,
		.imageArrayLayers = 1,
		.imageUsage = imageUsage,
		.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.preTransform = capabilities.currentTransform,
		.compositeAlpha = compositeAlpha,
		.presentMode = presentMode,
		.clipped = VK_TRUE,
		.oldSwapchain = VK_NULL_HANDLE
	};

	VK_CHECK(vkCreateSwapchainKHR(device, &swapchainInfo, nullptr, &m_SwapChain));

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Images
	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	VK_CHECK(vkGetSwapchainImagesKHR(device, m_SwapChain, &imageCount, nullptr));
	std::vector<VkImage> images(imageCount);
	VK_CHECK(vkGetSwapchainImagesKHR(device, m_SwapChain, &imageCount, images.data()));
	m_Images.resize(imageCount);

	for (uint32_t i = 0; i < imageCount; i++)
		m_Images[i].Image = images[i];

	CreateImageViews();

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Depth Image
	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	VkImageCreateInfo depthImageInfo
	{
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = VK_FORMAT_D32_SFLOAT,
		.extent =
		{
			.width = m_Extent.width,
			.height = m_Extent.height,
			.depth = 1
		},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
	};

	VmaAllocationCreateInfo allocationInfo
	{
		.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
	};

	VK_CHECK(vmaCreateImage(Allocator::GetAllocator(), &depthImageInfo, &allocationInfo, &m_DepthStencil.Image, &m_DepthStencil.Allocation, nullptr));

	VkImageViewCreateInfo depthViewInfo
	{
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = m_DepthStencil.Image,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = VK_FORMAT_D32_SFLOAT,
		.subresourceRange =
		{
			.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1
		}
	};

	VK_CHECK(vkCreateImageView(device, &depthViewInfo, nullptr, &m_DepthStencil.ImageView));
}

void SwapChain::CreateImageViews()
{
	VkDevice device = RendererContext::GetDevice()->GetDevice();

	for (auto& image : m_Images)
	{
		VkImageViewCreateInfo createInfo
		{
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = image.Image,
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = m_ColorFormat,
			.components =
			{
				.r = VK_COMPONENT_SWIZZLE_IDENTITY,
				.g = VK_COMPONENT_SWIZZLE_IDENTITY,
				.b = VK_COMPONENT_SWIZZLE_IDENTITY,
				.a = VK_COMPONENT_SWIZZLE_IDENTITY
			},
			.subresourceRange =
			{
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1
			}
		};

		VK_CHECK(vkCreateImageView(device, &createInfo, nullptr, &image.ImageView));
	}
}

void SwapChain::DestroySwapChain()
{
	VkDevice device = RendererContext::GetDevice()->GetDevice();

	if (m_DepthStencil.ImageView)
	{
		vkDestroyImageView(device, m_DepthStencil.ImageView, nullptr);

		m_DepthStencil.ImageView = VK_NULL_HANDLE;
	}

	if (m_DepthStencil.Image)
	{
		vmaDestroyImage(Allocator::GetAllocator(), m_DepthStencil.Image, m_DepthStencil.Allocation);

		m_DepthStencil.Image = VK_NULL_HANDLE;
		m_DepthStencil.Allocation = VK_NULL_HANDLE;
	}

	for (auto& image : m_Images)
	{
		if (image.ImageView)
		{
			vkDestroyImageView(device, image.ImageView, nullptr);
			image.ImageView = VK_NULL_HANDLE;
		}
	}

	m_Images.clear();

	if (m_SwapChain)
	{
		vkDestroySwapchainKHR(device, m_SwapChain, nullptr);
		m_SwapChain = VK_NULL_HANDLE;
	}
}

void SwapChain::FindImageFormatAndColorSpace()
{
	VkPhysicalDevice physicalDevice = RendererContext::GetPhysicalDevice()->GetPhysicalDevice();

	// Get list of supported surface formats
	uint32_t formatCount;
	VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, m_Surface, &formatCount, nullptr));
	assert(formatCount > 0);
	std::vector<VkSurfaceFormatKHR> surfaceFormats(formatCount);
	VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, m_Surface, &formatCount, surfaceFormats.data()));

	// If the surface format list only includes one entry with VK_FORMAT_UNDEFINED,
	// there is no preferred format, so we assume VK_FORMAT_B8G8R8A8_UNORM
	if ((formatCount == 1) && (surfaceFormats[0].format == VK_FORMAT_UNDEFINED))
	{
		m_ColorFormat = VK_FORMAT_B8G8R8A8_UNORM;
		m_ColorSpace = surfaceFormats[0].colorSpace;
	}
	else
	{
		// iterate over the list of available surface format and
		// check for the presence of VK_FORMAT_B8G8R8A8_UNORM
		bool found_B8G8R8A8_UNORM = false;
		for (const auto& surfaceFormat : surfaceFormats)
		{
			if (surfaceFormat.format == VK_FORMAT_B8G8R8A8_UNORM)
			{
				m_ColorFormat = surfaceFormat.format;
				m_ColorSpace = surfaceFormat.colorSpace;
				found_B8G8R8A8_UNORM = true;
				break;
			}
		}

		// in case VK_FORMAT_B8G8R8A8_UNORM is not available
		// select the first available color format
		if (!found_B8G8R8A8_UNORM)
		{
			m_ColorFormat = surfaceFormats[0].format;
			m_ColorSpace = surfaceFormats[0].colorSpace;
		}
	}
}
