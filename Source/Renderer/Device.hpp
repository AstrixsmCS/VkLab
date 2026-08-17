#pragma once

#include "Vulkan.hpp"

#include <cstdint>
#include <vector>
#include <unordered_set>
#include <string>
#include <memory>

class PhysicalDevice
	{
	public:
		struct QueueFamilyIndices
		{
			int32_t Graphics = -1;
		};
	public:
		VkPhysicalDevice FindPhysicalDevice();
		void FindGraphicsQueue();

		VkPhysicalDevice GetPhysicalDevice() const { return m_PhysicalDevice; }
		const QueueFamilyIndices& GetQueueFamilyIndices() const { return m_QueueFamilyIndices; }

		const VkPhysicalDeviceProperties& GetProperties() const { return m_Properties; }
		const VkPhysicalDeviceLimits& GetLimits() const { return m_Properties.limits; }

		static std::unique_ptr<PhysicalDevice> Select();
	private:
		QueueFamilyIndices m_QueueFamilyIndices{};

		VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;

		VkPhysicalDeviceProperties m_Properties{};
		VkPhysicalDeviceFeatures m_Features{};

		std::vector<VkQueueFamilyProperties2> m_QueueFamilyProperties;
		std::vector<VkDeviceQueueCreateInfo> m_QueueCreateInfos;

		friend class LogicalDevice;
	};

	// Represents a logical device
	class LogicalDevice
	{
	public:
		void Create(const PhysicalDevice& physicalDevice);
		void Destroy();

		VkQueue GetGraphicsQueue() const { return m_GraphicsQueue; }
		const PhysicalDevice* GetPhysicalDevice() const { return m_PhysicalDevice; }
		VkDevice GetDevice() const { return m_LogicalDevice; }

		bool IsExtensionSupported(const std::string& extensionName) const;
	private:
		VkDevice m_LogicalDevice = VK_NULL_HANDLE;
		const PhysicalDevice* m_PhysicalDevice = nullptr;

		VkQueue m_GraphicsQueue = VK_NULL_HANDLE;

		std::unordered_set<std::string> m_SupportedExtensions;

		bool m_EnableDebugMarkers = false;
	};
