#pragma once

#include "Vulkan.hpp"

#include <cstdint>
#include <vector>

class Descriptor
{
public:
	static constexpr uint32_t INVALID_INDEX = UINT32_MAX;
	static constexpr uint32_t NULL_TEXTURE = 0;

	static void Initialize();
	static void Shutdown();

	static uint32_t RegisterTexture(VkImageView imageView);
	static uint32_t RegisterSampler(VkSampler sampler);
	static uint32_t RegisterStorageImage(VkImageView imageView);

	static void UnregisterTexture(uint32_t index);
	static void UnregisterSampler(uint32_t index);
	static void UnregisterStorageImage(uint32_t index);

	static VkDescriptorSetLayout GetLayout() { return s_Layout; }
	static VkDescriptorSet GetSet() { return s_Set; }
private:
	static constexpr uint32_t MAX_TEXTURES = 4096;
	static constexpr uint32_t MAX_SAMPLERS = 1024;
	static constexpr uint32_t MAX_STORAGE_IMAGES = 1024;

	static inline VkDescriptorSetLayout s_Layout = VK_NULL_HANDLE;
	static inline VkDescriptorPool      s_Pool   = VK_NULL_HANDLE;
	static inline VkDescriptorSet       s_Set    = VK_NULL_HANDLE;

	static inline std::vector<uint32_t> s_FreeTextureIndices;
	static inline std::vector<uint32_t> s_FreeSamplerIndices;
	static inline std::vector<uint32_t> s_FreeStorageImageIndices;
};
