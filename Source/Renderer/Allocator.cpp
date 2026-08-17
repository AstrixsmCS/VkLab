#include "Allocator.hpp"

#include "RendererContext.hpp"

#define VMA_IMPLEMENTATION
#include <vma/vk_mem_alloc.h>

VmaAllocator Allocator::s_VmaAllocator = VK_NULL_HANDLE;

void Allocator::Initialize(const LogicalDevice& device)
{
	VmaVulkanFunctions vmaFuncInfo{};
	VmaAllocatorCreateInfo vmaAllocInfo
	{
		.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
		.physicalDevice = device.GetPhysicalDevice()->GetPhysicalDevice(),
		.device = device.GetDevice(),
		.pVulkanFunctions = &vmaFuncInfo,
		.instance = RendererContext::GetInstance(),
		.vulkanApiVersion = VK_API_VERSION_1_4
	};

	vmaImportVulkanFunctionsFromVolk(&vmaAllocInfo, &vmaFuncInfo);

	VK_CHECK(vmaCreateAllocator(&vmaAllocInfo, &s_VmaAllocator));
}

void Allocator::Shutdown()
{
	vmaDestroyAllocator(s_VmaAllocator);
	s_VmaAllocator = VK_NULL_HANDLE;
}
