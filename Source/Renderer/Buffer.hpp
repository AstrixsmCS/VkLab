#pragma once

#include "Vulkan.hpp"
#include "Allocator.hpp"

#include <cstdint>
#include <string>
#include <utility>

enum class BufferUsage : uint32_t
{
	None          = 0,
	TransferSrc   = 1 << 0,
	TransferDst   = 1 << 1,
	Vertex        = 1 << 2,
	Index         = 1 << 3,
	Uniform       = 1 << 4,
	Storage       = 1 << 5,
	Indirect      = 1 << 6,
	DeviceAddress = 1 << 7,
};

inline BufferUsage operator|(BufferUsage a, BufferUsage b) { return static_cast<BufferUsage>(std::to_underlying(a) | std::to_underlying(b)); }
inline BufferUsage operator&(BufferUsage a, BufferUsage b) { return static_cast<BufferUsage>(std::to_underlying(a) & std::to_underlying(b)); }
inline bool        HasFlag(BufferUsage value, BufferUsage flag) { return (value & flag) != BufferUsage::None; }

enum class BufferMemory : uint8_t
{
	GPU = 0,
	CPUToGPU,
	GPUToCPU,
};

struct BufferSpecification
{
	uint64_t     Size   = 0;
	BufferUsage  Usage  = BufferUsage::None;
	BufferMemory Memory = BufferMemory::GPU;
	bool         Mapped = false;
	std::string  DebugName;
};

class Buffer
{
public:
	void Create(const BufferSpecification& specification);
	void Shutdown();

	void  SetData(const void* data, uint64_t size, uint64_t offset = 0);
	void* Map();
	void  Unmap();

	VkDeviceAddress GetDeviceAddress() const;

	const BufferSpecification& GetSpecification() const { return m_Specification; }
	VkBuffer      GetBuffer()     const { return m_Buffer; }
	VmaAllocation GetAllocation() const { return m_Allocation; }
	uint64_t      GetSize()       const { return m_Specification.Size; }
	bool          IsMapped()      const { return m_MappedData != nullptr; }

private:
	static VkBufferUsageFlags ToVulkanUsage(BufferUsage usage);

	BufferSpecification m_Specification;
	VkBuffer            m_Buffer     = VK_NULL_HANDLE;
	VmaAllocation       m_Allocation = VK_NULL_HANDLE;
	void*               m_MappedData = nullptr;
};
