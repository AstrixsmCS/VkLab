#include "RendererContext.hpp"

#include <SDL3/SDL_vulkan.h>

#define VOLK_IMPLEMENTATION
#include <Volk/volk.h>

#include <print>
#include <vector>

#ifndef VK_API_VERSION_1_4
#error Wrong Vulkan SDK!
#endif

#ifdef NDEBUG
static bool s_Validation = false;
#else
static bool s_Validation = true;
#endif

constexpr const char* VkDebugUtilsMessageType(VkDebugUtilsMessageTypeFlagsEXT type)
{
	if (type & VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT)     return "General";
	if (type & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT)  return "Validation";
	if (type & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) return "Performance";
	return "Unknown";
}

constexpr const char* VkDebugUtilsMessageSeverity(VkDebugUtilsMessageSeverityFlagBitsEXT severity)
{
	switch (severity)
	{
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:   return "Error";
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT: return "Warning";
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:    return "Info";
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT: return "Verbose";
		default:                                               return "Unknown";
	}
}

static VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDebugUtilsMessengerCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData)
{
	(void)pUserData;

	if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT)
		return VK_FALSE;

	std::println("[Vulkan] {} {} message:", VkDebugUtilsMessageType(messageType), VkDebugUtilsMessageSeverity(messageSeverity));
	std::println("{}", pCallbackData->pMessage);

	if (pCallbackData->cmdBufLabelCount > 0)
	{
		std::println("Command Buffer Labels ({}):", pCallbackData->cmdBufLabelCount);
		for (uint32_t i = 0; i < pCallbackData->cmdBufLabelCount; ++i)
		{
			const auto& label = pCallbackData->pCmdBufLabels[i];
			std::println("  [{}] {}", i, label.pLabelName ? label.pLabelName : "NULL");
		}
	}

	if (pCallbackData->objectCount > 0)
	{
		std::println("Objects ({}):", pCallbackData->objectCount);
		for (uint32_t i = 0; i < pCallbackData->objectCount; ++i)
		{
			const auto& object = pCallbackData->pObjects[i];
			std::println("  [{}] {} - handle: {:#x}", i, object.pObjectName ? object.pObjectName : "NULL", object.objectHandle);
		}
	}

	return VK_FALSE;
}

static bool CheckDriverAPIVersionSupport(uint32_t minimumSupportedVersion)
{
	uint32_t instanceVersion = VK_API_VERSION_1_0;
	if (vkEnumerateInstanceVersion(&instanceVersion) != VK_SUCCESS)
	{
		std::println("Failed to query Vulkan instance version.");
		return false;
	}

	if (instanceVersion < minimumSupportedVersion)
	{
		std::println("Incompatible Vulkan driver version!");
		std::println("  You have {}.{}.{}", VK_API_VERSION_MAJOR(instanceVersion), VK_API_VERSION_MINOR(instanceVersion), VK_API_VERSION_PATCH(instanceVersion));
		std::println("  You need at least {}.{}.{}", VK_API_VERSION_MAJOR(minimumSupportedVersion), VK_API_VERSION_MINOR(minimumSupportedVersion), VK_API_VERSION_PATCH(minimumSupportedVersion));
		return false;
	}

	return true;
}

void RendererContext::Initialize()
{
	if (volkInitialize() != VK_SUCCESS)
		throw std::runtime_error("Failed to initialize volk!");

	if (!CheckDriverAPIVersionSupport(VK_API_VERSION_1_4))
		throw std::runtime_error("Incompatible Vulkan driver. Update your GPU drivers!");

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Extensions and Validation
	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	uint32_t sdlExtensionCount = 0;
	const char* const* sdlExtensions = SDL_Vulkan_GetInstanceExtensions(&sdlExtensionCount);
	if (!sdlExtensions)
		throw std::runtime_error(std::string("SDL_Vulkan_GetInstanceExtensions failed: ") + SDL_GetError());

	std::vector<const char*> instanceExtensions(sdlExtensions, sdlExtensions + sdlExtensionCount);
	if (s_Validation)
		instanceExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Validation Layers
	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	std::vector<const char*> requestedLayers;

	if (s_Validation)
	{
		constexpr const char* validationLayerName = "VK_LAYER_KHRONOS_validation";

		uint32_t layerCount = 0;
		vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
		std::vector<VkLayerProperties> availableLayers(layerCount);
		vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

		bool found = false;
		for (const auto& layer : availableLayers)
		{
			if (std::strcmp(layer.layerName, validationLayerName) == 0)
			{
				found = true;
				break;
			}
		}

		if (found)
		{
			requestedLayers.push_back(validationLayerName);
		}
		else
		{
			std::println("[Renderer] Validation layer {} not found. Validation disabled.", validationLayerName);
		}
	}

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Instance Creation
	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	const VkApplicationInfo appInfo
	{
		.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.pApplicationName   = "VkLab",
		.pEngineName        = "VkLab",
		.apiVersion         = VK_API_VERSION_1_4,
	};

	const VkDebugUtilsMessengerCreateInfoEXT debugInfo
		{
			.sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
			.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
			.messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
			.pfnUserCallback = VulkanDebugUtilsMessengerCallback,
		};

	const VkInstanceCreateInfo instCreateInfo
	{
		.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pNext                   = s_Validation ? &debugInfo : nullptr,
		.pApplicationInfo        = &appInfo,
		.enabledLayerCount       = static_cast<uint32_t>(requestedLayers.size()),
		.ppEnabledLayerNames     = requestedLayers.data(),
		.enabledExtensionCount   = static_cast<uint32_t>(instanceExtensions.size()),
		.ppEnabledExtensionNames = instanceExtensions.data(),
	};

	VK_CHECK(vkCreateInstance(&instCreateInfo, nullptr, &s_VulkanInstance));
	volkLoadInstance(s_VulkanInstance);

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Debug Messenger
	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	if (s_Validation)
	{
		VK_CHECK(vkCreateDebugUtilsMessengerEXT(s_VulkanInstance, &debugInfo, nullptr, &m_DebugMessenger));
	}
}

void RendererContext::Shutdown()
{
	if (m_DebugMessenger)
	{
		vkDestroyDebugUtilsMessengerEXT(s_VulkanInstance, m_DebugMessenger, nullptr);
		m_DebugMessenger = VK_NULL_HANDLE;
	}

	if (s_VulkanInstance)
	{
		vkDestroyInstance(s_VulkanInstance, nullptr);
		s_VulkanInstance = VK_NULL_HANDLE;
	}
	volkFinalize();
}
