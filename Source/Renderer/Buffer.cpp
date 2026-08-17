#include "Buffer.hpp"

#include "RendererContext.hpp"

#include <cassert>
#include <cstring>

void Buffer::Create(const BufferSpecification& specification)
{
	assert(specification.Size > 0);
	assert(specification.Usage != BufferUsage::None);

	m_Specification = specification;

	VkBufferCreateInfo bufferInfo
	{
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = specification.Size,
		.usage = ToVulkanUsage(specification.Usage),
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE
	};

	VmaAllocationCreateInfo allocationInfo{};

	switch (specification.Memory)
	{
		case BufferMemory::GPU:
		{
			allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
			break;
		}

		case BufferMemory::CPUToGPU:
		{
			allocationInfo.usage = VMA_MEMORY_USAGE_AUTO;
			allocationInfo.flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

			break;
		}

		case BufferMemory::GPUToCPU:
		{
			allocationInfo.usage = VMA_MEMORY_USAGE_AUTO;
			allocationInfo.flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;

			break;
		}
	}

	if (specification.Mapped)
	{
		allocationInfo.flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
	}

	VmaAllocationInfo allocationResult{};

	VK_CHECK(vmaCreateBuffer(Allocator::GetAllocator(), &bufferInfo, &allocationInfo, &m_Buffer, &m_Allocation, &allocationResult));

	if (specification.Mapped)
	{
		m_MappedData = allocationResult.pMappedData;
	}

	if (!specification.DebugName.empty())
	{
		SetDebugUtilsObjectName(RendererContext::GetDevice()->GetDevice(), VK_OBJECT_TYPE_BUFFER, specification.DebugName, m_Buffer);
	}
}

void Buffer::Shutdown()
{
	if (m_Buffer == VK_NULL_HANDLE)
		return;

	vmaDestroyBuffer(Allocator::GetAllocator(), m_Buffer, m_Allocation);

	m_Buffer = VK_NULL_HANDLE;
	m_Allocation = VK_NULL_HANDLE;
	m_MappedData = nullptr;

	m_Specification = {};
}

void Buffer::SetData(const void* data, uint64_t size, uint64_t offset)
{
	assert(data);
	assert(size > 0);
	assert(offset + size <= m_Specification.Size);

	void* destination = m_MappedData;
	bool temporaryMapping = false;

	if (!destination)
	{
		VK_CHECK(vmaMapMemory(Allocator::GetAllocator(), m_Allocation, &destination));

		temporaryMapping = true;
	}

	std::memcpy(static_cast<uint8_t*>(destination) + offset, data, size);

	if (temporaryMapping)
	{
		vmaUnmapMemory(Allocator::GetAllocator(), m_Allocation);
	}
}

void* Buffer::Map()
{
	if (m_MappedData)
		return m_MappedData;

	VK_CHECK(vmaMapMemory(Allocator::GetAllocator(), m_Allocation, &m_MappedData));

	return m_MappedData;
}

void Buffer::Unmap()
{
	if (!m_MappedData)
		return;

	if (m_Specification.Mapped)
		return;

	vmaUnmapMemory(Allocator::GetAllocator(), m_Allocation);

	m_MappedData = nullptr;
}

VkDeviceAddress Buffer::GetDeviceAddress() const
{
	assert(HasFlag(m_Specification.Usage, BufferUsage::DeviceAddress));

	VkBufferDeviceAddressInfo addressInfo
	{
		.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
		.buffer = m_Buffer
	};

	return vkGetBufferDeviceAddress(RendererContext::GetDevice()->GetDevice(), &addressInfo);
}

VkBufferUsageFlags Buffer::ToVulkanUsage(BufferUsage usage)
{
	VkBufferUsageFlags flags = 0;

	if (HasFlag(usage, BufferUsage::TransferSrc))
		flags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

	if (HasFlag(usage, BufferUsage::TransferDst))
		flags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;

	if (HasFlag(usage, BufferUsage::Vertex))
		flags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

	if (HasFlag(usage, BufferUsage::Index))
		flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;

	if (HasFlag(usage, BufferUsage::Uniform))
		flags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

	if (HasFlag(usage, BufferUsage::Storage))
		flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

	if (HasFlag(usage, BufferUsage::Indirect))
		flags |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;

	if (HasFlag(usage, BufferUsage::DeviceAddress))
		flags |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

	return flags;
}
