#pragma once

#include "Device.hpp"

struct SDL_Window;

class RendererContext
{
public:
	static void Initialize();
	static void Shutdown();

	static VkInstance GetInstance() { return s_VulkanInstance; }

	static void SetPhysicalDevice(const PhysicalDevice* device) { s_PhysicalDevice = device; }
	static void SetLogicalDevice(const LogicalDevice* device)   { s_LogicalDevice = device; }

	static const PhysicalDevice* GetPhysicalDevice() { return s_PhysicalDevice; }
	static const LogicalDevice*  GetDevice()         { return s_LogicalDevice; }
private:
	inline static VkInstance s_VulkanInstance = VK_NULL_HANDLE;
	inline static VkDebugUtilsMessengerEXT m_DebugMessenger = VK_NULL_HANDLE;

	inline static const PhysicalDevice* s_PhysicalDevice;
	inline static const LogicalDevice* s_LogicalDevice;
};
