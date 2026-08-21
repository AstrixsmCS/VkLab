#include "Descriptors.hpp"

#include "RendererContext.hpp"

#include <cassert>

void Descriptor::Initialize()
{
	VkDevice device = RendererContext::Get().GetDevice();

	VkDescriptorSetLayoutBinding bindings[3]
	{
		{
			.binding = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
			.descriptorCount = MAX_TEXTURES,
			.stageFlags = VK_SHADER_STAGE_ALL
		},
		{
			.binding = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
			.descriptorCount = MAX_SAMPLERS,
			.stageFlags = VK_SHADER_STAGE_ALL
		},
		{
			.binding = 2,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			.descriptorCount = MAX_STORAGE_IMAGES,
			.stageFlags = VK_SHADER_STAGE_ALL
		}
	};

	VkDescriptorBindingFlags bindingFlags[3]
	{
		VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,

		VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,

		VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT
	};

	VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo
	{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
		.bindingCount = 3,
		.pBindingFlags = bindingFlags
	};

	VkDescriptorSetLayoutCreateInfo layoutInfo
	{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.pNext = &bindingFlagsInfo,
		.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
		.bindingCount = 3,
		.pBindings = bindings
	};

	VK_CHECK(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &s_Layout));

	VkDescriptorPoolSize poolSizes[3]
	{
		{
			.type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
			.descriptorCount = MAX_TEXTURES
		},
		{
			.type = VK_DESCRIPTOR_TYPE_SAMPLER,
			.descriptorCount = MAX_SAMPLERS
		},
		{
			.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			.descriptorCount = MAX_STORAGE_IMAGES
		}
	};

	VkDescriptorPoolCreateInfo poolInfo
	{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
		.maxSets = 1,
		.poolSizeCount = 3,
		.pPoolSizes = poolSizes
	};

	VK_CHECK(vkCreateDescriptorPool(device, &poolInfo, nullptr, &s_Pool));

	VkDescriptorSetAllocateInfo allocateInfo
	{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = s_Pool,
		.descriptorSetCount = 1,
		.pSetLayouts = &s_Layout
	};

	VK_CHECK(vkAllocateDescriptorSets(device, &allocateInfo, &s_Set));

	s_FreeTextureIndices.reserve(MAX_TEXTURES);
	for (uint32_t i = MAX_TEXTURES; i > 0; --i)
		s_FreeTextureIndices.push_back(i - 1);

	s_FreeSamplerIndices.reserve(MAX_SAMPLERS);
	for (uint32_t i = MAX_SAMPLERS; i > 0; --i)
		s_FreeSamplerIndices.push_back(i - 1);

	s_FreeStorageImageIndices.reserve(MAX_STORAGE_IMAGES);
	for (uint32_t i = MAX_STORAGE_IMAGES; i > 0; --i)
		s_FreeStorageImageIndices.push_back(i - 1);
}

void Descriptor::Shutdown()
{
	VkDevice device = RendererContext::Get().GetDevice();

	s_FreeTextureIndices.clear();
	s_FreeSamplerIndices.clear();
	s_FreeStorageImageIndices.clear();

	if (s_Pool)
	{
		vkDestroyDescriptorPool(device, s_Pool, nullptr);
		s_Pool = VK_NULL_HANDLE;
	}

	if (s_Layout)
	{
		vkDestroyDescriptorSetLayout(device, s_Layout, nullptr);
		s_Layout = VK_NULL_HANDLE;
	}

	s_Set = VK_NULL_HANDLE;
}

uint32_t Descriptor::RegisterTexture(VkImageView imageView)
{
	assert(imageView != VK_NULL_HANDLE);
	assert(!s_FreeTextureIndices.empty());

	const uint32_t index = s_FreeTextureIndices.back();
	s_FreeTextureIndices.pop_back();

	VkDescriptorImageInfo imageInfo
	{
		.sampler = VK_NULL_HANDLE,
		.imageView = imageView,
		.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
	};

	VkWriteDescriptorSet write
	{
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = s_Set,
		.dstBinding = 0,
		.dstArrayElement = index,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
		.pImageInfo = &imageInfo
	};

	vkUpdateDescriptorSets(RendererContext::Get().GetDevice(), 1, &write, 0, nullptr);

	return index;
}

uint32_t Descriptor::RegisterSampler(VkSampler sampler)
{
	assert(sampler != VK_NULL_HANDLE);
	assert(!s_FreeSamplerIndices.empty());

	const uint32_t index = s_FreeSamplerIndices.back();
	s_FreeSamplerIndices.pop_back();

	VkDescriptorImageInfo imageInfo
	{
		.sampler = sampler,
		.imageView = VK_NULL_HANDLE,
		.imageLayout = VK_IMAGE_LAYOUT_UNDEFINED
	};

	VkWriteDescriptorSet write
	{
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = s_Set,
		.dstBinding = 1,
		.dstArrayElement = index,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
		.pImageInfo = &imageInfo
	};

	vkUpdateDescriptorSets(RendererContext::Get().GetDevice(), 1, &write, 0, nullptr);

	return index;
}

uint32_t Descriptor::RegisterStorageImage(VkImageView imageView)
{
	assert(imageView != VK_NULL_HANDLE);
	assert(!s_FreeStorageImageIndices.empty());

	const uint32_t index = s_FreeStorageImageIndices.back();
	s_FreeStorageImageIndices.pop_back();

	VkDescriptorImageInfo imageInfo
	{
		.sampler = VK_NULL_HANDLE,
		.imageView = imageView,
		.imageLayout = VK_IMAGE_LAYOUT_GENERAL
	};

	VkWriteDescriptorSet write
	{
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = s_Set,
		.dstBinding = 2,
		.dstArrayElement = index,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		.pImageInfo = &imageInfo
	};

	vkUpdateDescriptorSets(RendererContext::Get().GetDevice(), 1, &write, 0, nullptr);

	return index;
}

void Descriptor::UnregisterTexture(uint32_t index)
{
	if (index == INVALID_INDEX)
		return;

	assert(index < MAX_TEXTURES);

	s_FreeTextureIndices.push_back(index);
}

void Descriptor::UnregisterSampler(uint32_t index)
{
	if (index == INVALID_INDEX)
		return;

	assert(index < MAX_SAMPLERS);

	s_FreeSamplerIndices.push_back(index);
}

void Descriptor::UnregisterStorageImage(uint32_t index)
{
	if (index == INVALID_INDEX)
		return;

	assert(index < MAX_STORAGE_IMAGES);

	s_FreeStorageImageIndices.push_back(index);
}
