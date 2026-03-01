#pragma once

#include "VkWindow.h"
#include <ddraw.h>
#include <cstdint>
#include <vector>

namespace gvlk {
namespace texture {

uint32_t calcMipLevels(uint32_t w, uint32_t h);
void convertToRgba(const void* src, std::vector<unsigned char>& dst,
                   DWORD w, DWORD h, DWORD pitch, const DDPIXELFORMAT& fmt);
VkFormat fourCCToVkFormat(DWORD fourCC);
DWORD fourCCBlockSize(DWORD fourCC);
void transitionImageLayout(VkCommandBuffer cmd, VkImage image,
                           VkImageLayout oldLayout, VkImageLayout newLayout,
                           uint32_t baseMip, uint32_t mipCount);
void uploadImageData(VkImage image, DWORD w, DWORD h, uint32_t mipLevel,
                     const void* data, DWORD dataSize,
                     VkDevice device, VkBuffer stagingBuffer, void* stagingMapped,
                     VmaAllocator allocator, VkCommandPool cmdPool, VkQueue queue);
void generateMipmaps(VkImage image, uint32_t w, uint32_t h, uint32_t mipLevels, uint32_t fromLevel,
                     VkDevice device, VkCommandPool cmdPool, VkQueue queue);
VkSampler createSampler(VkDevice device, uint32_t mipLevels);
VkDescriptorSet allocateDescriptorSet(VkDevice device, VkDescriptorPool pool, VkDescriptorSetLayout layout);
void updateDescriptorSet(VkDevice device, VkDescriptorSet set, VkImageView view, VkSampler sampler);

} // namespace texture
} // namespace gvlk
