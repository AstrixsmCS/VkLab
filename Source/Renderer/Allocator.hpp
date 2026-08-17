#pragma once

#include "Device.hpp"

#include <vma/vk_mem_alloc.h>

class Allocator
{
public:
	static void Initialize(const LogicalDevice& device);
	static void Shutdown();

	static VmaAllocator& GetAllocator() { return s_VmaAllocator; }
private:
	static VmaAllocator s_VmaAllocator;
};
