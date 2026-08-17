#include "Device.hpp"

#include "RendererContext.hpp"

#include <print>
#include <cassert>

VkPhysicalDevice PhysicalDevice::FindPhysicalDevice()
{
	auto vkInstance = RendererContext::GetInstance();

	uint32_t gpuCount = 0;

	vkEnumeratePhysicalDevices(vkInstance, &gpuCount, nullptr);
	assert(gpuCount > 0 && "Could not find any physical devices!");
	std::vector<VkPhysicalDevice> physicalDevices(gpuCount);
	VK_CHECK(vkEnumeratePhysicalDevices(vkInstance, &gpuCount, physicalDevices.data()));

	// Default to the first available GPU
	VkPhysicalDevice selectedPhysicalDevice = physicalDevices[0];

	// Prefer a discrete GPU if one is available
	for (VkPhysicalDevice physicalDevice : physicalDevices)
	{
		VkPhysicalDeviceProperties properties{};
		vkGetPhysicalDeviceProperties(physicalDevice, &properties);

		if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
		{
			selectedPhysicalDevice = physicalDevice;
			break;
		}
	}
	m_PhysicalDevice = selectedPhysicalDevice;

	// Get physical device information
	vkGetPhysicalDeviceProperties(m_PhysicalDevice, &m_Properties);
	vkGetPhysicalDeviceFeatures(m_PhysicalDevice, &m_Features);

	return m_PhysicalDevice;
}

void PhysicalDevice::FindGraphicsQueue()
{
	uint32_t queueFamilyCount = 0;

	vkGetPhysicalDeviceQueueFamilyProperties2(m_PhysicalDevice, &queueFamilyCount, nullptr);
	assert(queueFamilyCount > 0 && "Physical device has no queue families!");
	m_QueueFamilyProperties.resize(queueFamilyCount, VkQueueFamilyProperties2{.sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2});
	vkGetPhysicalDeviceQueueFamilyProperties2(m_PhysicalDevice, &queueFamilyCount, m_QueueFamilyProperties.data());

	for (uint32_t i = 0; i < m_QueueFamilyProperties.size(); i++)
	{
		const auto& properties = m_QueueFamilyProperties[i].queueFamilyProperties;

		if (properties.queueFlags & VK_QUEUE_GRAPHICS_BIT)
		{
			m_QueueFamilyIndices.Graphics = static_cast<int32_t>(i);
			break;
		}
	}

	assert(m_QueueFamilyIndices.Graphics >= 0 && "Could not find a graphics queue family!");

	static constexpr float queuePriority = 1.0f;

	VkDeviceQueueCreateInfo queueInfo
	{
		.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
		.queueFamilyIndex = static_cast<uint32_t>(m_QueueFamilyIndices.Graphics),
		.queueCount = 1,
		.pQueuePriorities = &queuePriority
	};

	m_QueueCreateInfos.push_back(queueInfo);
}

std::unique_ptr<PhysicalDevice> PhysicalDevice::Select()
{
	auto physicalDevice = std::make_unique<PhysicalDevice>();

	physicalDevice->FindPhysicalDevice();
	physicalDevice->FindGraphicsQueue();

	return physicalDevice;
}

// LogicalDevice
void LogicalDevice::Create(const PhysicalDevice& physicalDevice)
{
	m_PhysicalDevice = &physicalDevice;

	// Query supported Vulkan features
	VkPhysicalDeviceVulkan14Features supportedFeatures14
	{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES,
		.pNext = nullptr
	};

	VkPhysicalDeviceVulkan13Features supportedFeatures13
	{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
		.pNext = &supportedFeatures14
	};

	VkPhysicalDeviceVulkan12Features supportedFeatures12
	{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
		.pNext = &supportedFeatures13
	};

	VkPhysicalDeviceVulkan11Features supportedFeatures11
	{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
		.pNext = &supportedFeatures12
	};

	VkPhysicalDeviceFeatures2 supportedFeatures
	{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
		.pNext = &supportedFeatures11
	};

	vkGetPhysicalDeviceFeatures2(physicalDevice.GetPhysicalDevice(), &supportedFeatures);

	assert(supportedFeatures13.dynamicRendering && "Dynamic rendering is not supported!");
	assert(supportedFeatures13.synchronization2 &&"Synchronization2 is not supported!");
	assert(supportedFeatures12.timelineSemaphore && "Timeline semaphores are not supported!");
	assert(supportedFeatures12.bufferDeviceAddress && "Buffer device address is not supported!");

	// Features to enable
	VkPhysicalDeviceVulkan14Features features14
	{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES,
		.pNext = nullptr
	};

	VkPhysicalDeviceVulkan13Features features13
	{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
		.pNext = &features14,
		.synchronization2 = VK_TRUE,
		.dynamicRendering = VK_TRUE
	};

	VkPhysicalDeviceVulkan12Features features12
	{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
		.pNext = &features13,
		.timelineSemaphore = VK_TRUE,
		.bufferDeviceAddress = VK_TRUE
	};

	VkPhysicalDeviceVulkan11Features features11
	{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
		.pNext = &features12,
	};

	VkPhysicalDeviceFeatures2 features
	{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
		.pNext = &features11,
		.features = {}
	};

	// Enumerate device extensions
	uint32_t extensionCount = 0;
	VK_CHECK(vkEnumerateDeviceExtensionProperties(physicalDevice.GetPhysicalDevice(), nullptr, &extensionCount, nullptr));
	std::vector<VkExtensionProperties> extensions(extensionCount);
	VK_CHECK(vkEnumerateDeviceExtensionProperties(physicalDevice.GetPhysicalDevice(), nullptr, &extensionCount, extensions.data()));

	std::println("[Renderer] Physical device '{}' has {} extensions:", physicalDevice.GetProperties().deviceName,extensions.size());

	for (const auto& extension : extensions)
	{
		m_SupportedExtensions.emplace(extension.extensionName);
		std::println("[Renderer]   {}", extension.extensionName);
	}

	// Required extensions
	std::vector<const char*> deviceExtensions
	{
		VK_KHR_SWAPCHAIN_EXTENSION_NAME
	};

	assert(IsExtensionSupported(VK_KHR_SWAPCHAIN_EXTENSION_NAME) && "VK_KHR_swapchain is not supported!");

	// Optional debug marker support
	if (IsExtensionSupported(VK_EXT_DEBUG_MARKER_EXTENSION_NAME))
	{
		deviceExtensions.push_back(VK_EXT_DEBUG_MARKER_EXTENSION_NAME);
		m_EnableDebugMarkers = true;
	}

	VkDeviceCreateInfo deviceCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.pNext = &features,
		.queueCreateInfoCount = static_cast<uint32_t>(physicalDevice.m_QueueCreateInfos.size()),
		.pQueueCreateInfos = physicalDevice.m_QueueCreateInfos.data(),
		.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size()),
		.ppEnabledExtensionNames = deviceExtensions.data(),
		.pEnabledFeatures = nullptr
	};

	VK_CHECK(vkCreateDevice(physicalDevice.GetPhysicalDevice(), &deviceCreateInfo, nullptr, &m_LogicalDevice));

	volkLoadDevice(m_LogicalDevice);

	vkGetDeviceQueue(m_LogicalDevice, static_cast<uint32_t>(physicalDevice.m_QueueFamilyIndices.Graphics), 0, &m_GraphicsQueue);

	assert(m_GraphicsQueue && "Could not get graphics queue!");

	if (m_EnableDebugMarkers)
		std::println("[Renderer] Debug markers enabled.");
}

void LogicalDevice::Destroy()
{
	vkDeviceWaitIdle(m_LogicalDevice);
	vkDestroyDevice(m_LogicalDevice, nullptr);
}

bool LogicalDevice::IsExtensionSupported(const std::string& extensionName) const
{
	return m_SupportedExtensions.find(extensionName) != m_SupportedExtensions.end();
}
